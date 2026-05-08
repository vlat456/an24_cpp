#include "flattener.h"

#include "blueprint_v2/interface/bridge_port_interface.h"
#include "core/utils/union_find.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace bp2 {

// ==================================================================
// ARCHITECTURE: Иерархический Flattening
//
// Blueprint описывает схему иерархически: компоненты могут быть листьями
// (реальные примитивы типа Resistor) или blueprint-инстансами
// (подсхемы). Flattener раскрывает эту иерархию в плоский FlatNetlist.
//
// Ключевая проблема: wire в родительской схеме соединяет порт
// blueprint-инстанса с чем-то снаружи. Чтобы понять, куда это wire
// "идёт внутрь", мы используем bridge nodes.
//
// Bridge node — это мост между внешним и внутренним миром инстанса.
// У него два порта: "ext" (смотрит наружу) и "port" (смотрит внутрь).
// Когда wire извне приходит к инстансу, мы заменяем endpoint
// на путь: instance / bridge_node / ext.
// ==================================================================

Flattener::Flattener(BlueprintLibrary const& library)
    : library_(library) {}

FlatNetlist Flattener::flatten(Blueprint const& root, PathArena& arena) {
    FlatNetlist out;
    std::unordered_map<Path, SignalIndex> signals;

    // UnionFind — временная структура для этого вызова flatten().
    // Пока мы обходим схему, провижнальные (временные) индексы сигналов
    // могут оказаться на самом деле одним и тем же сигналом (через wire).
    // UnionFind отслеживает эквивалентность сигналов по портам.
    core::utils::UnionFind uf{0};

    // Фаза 1: обработать все wire на верхнем уровне и рекурсивно внутри
    // каждого инстанса. Для каждого wire две точки (source/target)
    // получают провижнальные индексы сигналов, и эти индексы объединяются
    // через uf.unite().
    process_wires(root, arena.root(), signals, uf, out, arena);

    // Фаза 2: обойти все ноды, эмитировать листовые компоненты
    // и рекурсивно раскрыть blueprint-инстансы.
    visit_blueprint(root, arena.root(), signals, uf, out, arena);

    // Фаза 3: UnionFind знает, какие провижнальные индексы эквивалентны.
    // Заменяем их на плотные компактные индексы (0, 1, 2, ...).
    compact_signals(uf, out);

    return out;
}

// ==================================================================
// Вспомогательные функции для понятных сообщений об ошибках
// ==================================================================

void Flattener::throw_unresolved_blueprint_instance(
    Blueprint::Node const& node, Path prefix, PathArena& arena) {
    const std::string instance_path = arena.to_string(arena.make_node(prefix, node.semantic.id));
    const auto bp_id = node.blueprint_instance().source.blueprint_id();
    const auto bp_name = arena.resolve_id(bp_id);
    const std::string blueprint_id = bp_name.empty()
        ? std::string{"<empty>"}
        : std::string{bp_name};
    throw std::logic_error(
        "Flattener: unresolved blueprint for instance '" + instance_path
        + "' (blueprint_id='" + blueprint_id + "')");
}

void Flattener::throw_invalid_endpoint(Blueprint const& scope_bp,
                                       WireEndpoint const& ep,
                                       const char* reason, PathArena& arena) {
    const std::string scope_path = arena.to_string(scope_bp.id().empty()
        ? arena.root()
        : arena.make_node(arena.root(), scope_bp.id()));
    const std::string node_name = ep.node.empty()
        ? std::string{"<empty>"}
        : std::string{arena.resolve_id(ep.node)};
    const std::string port_name = ep.port.empty()
        ? std::string{"<empty>"}
        : std::string{arena.resolve_id(ep.port)};
    throw std::logic_error(
        "Flattener: invalid endpoint '" + node_name + "." + port_name
        + "' in blueprint '" + scope_path + "': " + reason);
}

// ==================================================================
// find_bridge_for_port — найти bridge ноду для интерфейсного порта
//
// У инстанса есть интерфейс (iface()) — список портов, которые он
// экспонирует наружу. Для каждого такого порта во внутренней
// схеме должна быть bridge нода с exposed_port == port_name.
//
// Bridge нода — единственный способ "увидеть" порт изнутри.
// ==================================================================

Blueprint::Node const* Flattener::find_bridge_for_port(
    Blueprint const& inner_bp,
    core::InternedId port_name) {
    for (auto const& n : inner_bp.nodes()) {
        if (!n.is_bridge_port()) continue;
        if (n.bridge_port().exposed_port == port_name) {
            return &n;
        }
    }
    return nullptr;
}

// ==================================================================
// resolve_endpoint — разрешение endpoint в абсолютный путь
//
// WireEndpoint — это пара (node, port) в некотором скоупе (scope_bp).
// Эта функция превращает её в полный Path в PathArena.
//
// Сценарий A — node является листовым компонентом:
//   Просто возвращаем prefix / node / port.
//
// Сценарий B — node является blueprint-инстансом:
//   Нужно "зайти" внутрь инстанса и найти bridge для порта port.
//   Результат: prefix / instance / bridge_node / ext
//   (ext — это порт bridge ноды, смотрящий наружу).
//
// Почему именно ext? Потому что wire извне к инстансу физически
// подключается к внешнему порту bridge, а уже внутренняя сторона
// bridge (port) соединена с внутренней логикой.
// ==================================================================

Path Flattener::resolve_endpoint(
    Blueprint const& scope_bp,
    Path scope_prefix,
    WireEndpoint const& ep,
    PathArena& arena) const
{

    if (ep.node.empty()) {
        throw_invalid_endpoint(scope_bp, ep, "missing node id", arena);
    }
    if (ep.port.empty()) {
        throw_invalid_endpoint(scope_bp, ep, "missing port id", arena);
    }

    auto const* node = scope_bp.find_node(ep.node);
    if (!node) {
        throw_invalid_endpoint(scope_bp, ep, "node not found", arena);
    }
    if (!node->is_blueprint_instance()) {
        // Листовой компонент — путь строится напрямую
        const Path node_path = arena.make_node(scope_prefix, ep.node);
        return arena.make_port(node_path, ep.port);
    }

    // Blueprint-инстанс — разрешаем через bridge
    const Path instance_path = arena.make_node(scope_prefix, ep.node);

    Blueprint const* inner = nullptr;
    assert(node != nullptr);
    if (auto* def = node->blueprint_instance().source.inline_def()) {
        inner = def;
    } else {
        inner = library_.find(node->blueprint_instance().source.blueprint_id());
    }
    if (!inner) {
        throw_unresolved_blueprint_instance(*node, scope_prefix, arena);
    }
    assert(inner != nullptr);
    Blueprint::Node const* bridge = find_bridge_for_port(*inner, ep.port);
    if (!bridge) {
        const std::string inst_str(arena.resolve_id(ep.node));
        const std::string port_str(arena.resolve_id(ep.port));
        throw std::logic_error(
            "Flattener: no bridge node found for interface port '" + port_str
            + "' in blueprint instance '" + inst_str + "'");
    }

    const Path bridge_path = arena.make_node(instance_path, bridge->semantic.id);

    // У bridge ноды ищем порт "ext" — это внешняя сторона моста
    core::InternedId ext_port_id{};
    BridgePortNames const ports(arena.interner());
    for (auto const& p : inner->resolve_node_iface(*bridge, Blueprint::NodeIfaceAuthority{arena.interner()})) {
        if (p.name == ports.ext) {
            ext_port_id = p.name;
            break;
        }
    }
    if (ext_port_id == core::InternedId{}) {
        const std::string bridge_str(arena.resolve_id(bridge->semantic.id));
        throw std::logic_error(
            "Flattener: bridge node '" + bridge_str + "' has no 'ext' port");
    }

    return arena.make_port(bridge_path, ext_port_id);
}

// ==================================================================
// visit_blueprint — обход нод, рекурсия в инстансы
//
// Для каждой ноды в blueprint:
//   - если это blueprint-инстанс → рекурсивно раскрываем
//   - если это лист → эмитируем компонент
// ==================================================================

void Flattener::visit_blueprint(
    Blueprint const& bp,
    const Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    core::utils::UnionFind& uf,
    FlatNetlist& out,
    PathArena& arena) {

    for (auto const& node : bp.nodes()) {
        if (node.is_blueprint_instance()) {
            visit_blueprint_instance(node, prefix, signals, uf, out, arena);
        } else {
            emit_component(bp, node, prefix, signals, uf, out, arena);
        }
    }
}

// ==================================================================
// emit_component — эмитировать листовой компонент
//
// Для каждого порта компонента создаём сигнал (или используем
// существующий, если путь уже встречался через wire).
//
// Для bridge нод: порты "ext" и "port" представляют одну и ту же
// физическую точку (bridge — сквозной pass-through). Поэтому после
// эмиссии компонента объединяем их сигналы через UnionFind.
// ==================================================================

void Flattener::emit_component(
    Blueprint const& bp,
    Blueprint::Node const& node,
    const Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    core::utils::UnionFind& uf,
    FlatNetlist& out,
    PathArena& arena) {

    const Path node_path = arena.make_node(prefix, node.semantic.id);

    FlatNetlist::Component comp;
    comp.path = node_path;
    comp.type = node.semantic.type;
    comp.exposed_port_name = {};
    comp.params = node.semantic.params;
    comp.string_params = node.semantic.string_params;

    if (node.is_bridge_port()) {
        comp.exposed_port_name = node.bridge_port().exposed_port;
    }

    SignalIndex ext_sig = UINT32_MAX;
    SignalIndex port_sig = UINT32_MAX;

    const BridgePortNames ports(arena.interner());
    for (auto const& port : bp.resolve_node_iface(node, Blueprint::NodeIfaceAuthority{arena.interner()})) {
        const Path port_path = arena.make_port(node_path, port.name);
        SignalIndex const sig = get_or_create_signal(
            port_path, port.domain, signals, uf, out);
        comp.ports.push_back(port);
        comp.port_signals.emplace_back(std::make_pair(port.name, sig));

        if (port.name == ports.ext) ext_sig = sig;
        else if (port.name == ports.port) port_sig = sig;
    }

    out.components.push_back(std::move(comp));

    // Объединяем ext и port bridge: это один электрический потенциал
    if (node.is_bridge_port()
        && ext_sig != UINT32_MAX && port_sig != UINT32_MAX
        && ext_sig != port_sig) {
        uf.unite(ext_sig, port_sig);
    }
}

// ==================================================================
// process_wires — обработка wire и объединение сигналов
//
// Для каждого wire разрешаем оба endpoint'a в абсолютные пути.
// Каждый путь получает (или переиспользует) провижнальный сигнал.
// Затем объединяем эти два сигнала через UnionFind — они
// физически соединены проводом.
// ==================================================================

void Flattener::process_wires(
    Blueprint const& bp,
    const Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    core::utils::UnionFind& uf,
    FlatNetlist& out,
    PathArena& arena) const
{

    for (auto const& wire : bp.wires()) {
        const SignalIndex src_sig = get_or_create_signal(
            resolve_endpoint(bp, prefix, wire.source, arena),
            wire.domain, signals, uf, out);
        const SignalIndex tgt_sig = get_or_create_signal(
            resolve_endpoint(bp, prefix, wire.target, arena),
            wire.domain, signals, uf, out);

        if (src_sig != tgt_sig) {
            uf.unite(src_sig, tgt_sig);
        }
    }
}

// ==================================================================
// visit_blueprint_instance — рекурсивное раскрытие blueprint-инстанса
//
// Это самая сложная часть. Инстанс — это "окно" в другую схему.
// Нужно:
//   1. Найти сигналы, которые уже проведены к внешним портам
//      инстанса из родительской схемы, и передать их внутрь
//      (seed boundary signals).
//   2. Обработать внутренние wire инстанса.
//   3. Рекурсивно обойти внутренние ноды.
//
// Boundary seeding (шаг 1):
//   Для каждого порта интерфейса inner схемы ищем bridge ноду.
//   У bridge ищем ext порт. Если к этому ext пути уже есть сигнал
//   в родительском signals (значит, снаружи подведён wire),
//   копируем его в nested_signals. Это позволяет внутренним
//   компонентам "видеть" внешние сигналы.
// ==================================================================

void Flattener::visit_blueprint_instance(
    Blueprint::Node const& node,
    const Path prefix,
    std::unordered_map<Path, SignalIndex>& signals,
    core::utils::UnionFind& uf,
    FlatNetlist& out,
    PathArena& arena) {

    const Path node_path = arena.make_node(prefix, node.semantic.id);

    Blueprint const* inner = nullptr;
    if (auto* def = node.blueprint_instance().source.inline_def()) {
        inner = def;
    } else {
        inner = library_.find(node.blueprint_instance().source.blueprint_id());
    }
    if (!inner) {
        throw_unresolved_blueprint_instance(node, prefix, arena);
    }

    // Шаг 1: Seed boundary signals
    // Для каждого интерфейсного порта inner найти bridge и его ext.
    // Если ext уже имеет сигнал (подведён извне) — скопировать во внутренний скоуп.
    std::unordered_map<Path, SignalIndex> nested_signals;
    assert(inner != nullptr);
    for (auto const& port : inner->iface()) {
        Blueprint::Node const* bridge = find_bridge_for_port(*inner, port.name);
        if (!bridge) continue;

        const Path bridge_path = arena.make_node(node_path, bridge->semantic.id);

        core::InternedId ext_id{};
        const BridgePortNames ports(arena.interner());
        for (auto const& p : inner->resolve_node_iface(*bridge, Blueprint::NodeIfaceAuthority{arena.interner()})) {
            if (p.name == ports.ext) {
                ext_id = p.name;
                break;
            }
        }
        if (ext_id == core::InternedId{}) continue;

        Path const ext_path = arena.make_port(bridge_path, ext_id);
        if (auto it = signals.find(ext_path); it != signals.end()) {
            nested_signals[ext_path] = it->second;
        }
    }

    // Шаг 2: Обработать внутренние wire инстанса.
    // resolve_endpoint работает относительно node_path — то есть
    // все пути внутри инстанса получают префикс node_path.
    for (auto const& wire : inner->wires()) {
        const Path src = resolve_endpoint(*inner, node_path, wire.source, arena);
        const Path tgt = resolve_endpoint(*inner, node_path, wire.target, arena);

        const SignalIndex src_sig = get_or_create_signal(
            src, wire.domain, nested_signals, uf, out);
        const SignalIndex tgt_sig = get_or_create_signal(
            tgt, wire.domain, nested_signals, uf, out);

        if (src_sig != tgt_sig) {
            uf.unite(src_sig, tgt_sig);
        }
    }

    // Шаг 3: Слить внутренние сигналы обратно в родительский скоуп.
    // Это нужно, чтобы соседние компоненты на том же уровне могли
    // ссылаться на внутренние пути инстанса (например, для мониторинга).
    for (auto const& [path, sig] : nested_signals) {
        signals[path] = sig;
    }

    // Шаг 4: Рекурсивно эмитировать внутренние ноды
    visit_blueprint(*inner, node_path, nested_signals, uf, out, arena);
}

// ==================================================================
// get_or_create_signal — получить или создать провижнальный сигнал
//
// signals — мапа Path → провижнальный индекс. Если путь уже есть,
// возвращаем существующий индекс (это тот же порт, к которому
// уже обращались через wire или другой компонент).
//
// Если пути нет — создаём новый сигнал с индексом out.signal_count++.
// UnionFind расширяется, чтобы покрыть новый индекс.
// ==================================================================

SignalIndex Flattener::get_or_create_signal(
    const Path port_path,
    const Domain domain,
    std::unordered_map<Path, SignalIndex>& signals,
    core::utils::UnionFind& uf,
    FlatNetlist& out) {
    if (const auto it = signals.find(port_path); it != signals.end()) return it->second;

    const SignalIndex idx = out.signal_count++;
    signals[port_path] = idx;

    // Расширяем UnionFind, чтобы покрыть новый индекс
    if (idx >= static_cast<uint32_t>(uf.size())) {
        uf.grow(idx + 1);
    }

    FlatNetlist::Signal sig;
    sig.index = idx;
    sig.domain = domain;
    sig.connected_ports.push_back(port_path);
    out.signals.push_back(std::move(sig));

    return idx;
}

// ==================================================================
// compact_signals — компактификация провижнальных индексов
//
// После обхода схемы у нас есть:
//   - components с port_signals, содержащими провижнальные индексы
//   - signals[] с теми же провижнальными индексами
//   - UnionFind, знающий, какие индексы эквивалентны
//
// Эта функция:
//   1. Пробегает по всем компонентам, для каждого сигнала находит
//      корень UnionFind. Каждому новому корню назначает плотный
//      compact-индекс (0, 1, 2, ...) в порядке первого встреченного.
//   2. Переписывает signals[]: группирует connected_ports по compact-индексу.
//      Domain берётся из первого встреченного (first-wins), потому что
//      get_or_create_signal гарантирует, что первое создание определяет домен.
// ==================================================================

void Flattener::compact_signals(core::utils::UnionFind& uf, FlatNetlist& out) {
    // Фаза 1: корень UnionFind → compact-индекс
    std::unordered_map<uint32_t, uint32_t> root_to_compact;
    uint32_t next_compact = 0;

    for (auto& comp : out.components) {
        for (auto& [name, sig] : comp.port_signals) {
            uint32_t const root = uf.find(sig);
            auto [it, inserted] = root_to_compact.emplace(std::make_pair(root, next_compact));
            if (inserted) next_compact++;
            sig = it->second;
        }
    }

    // Фаза 2: перестроить signals[] по compact-индексам
    // first-wins для domain: get_or_create_signal уже гарантирует,
    // что первое обращение к пути определяет домен.
    std::vector<FlatNetlist::Signal> compacted(next_compact);
    for (auto& [index, domain, connected_ports] : out.signals) {
        uint32_t const root = uf.find(index);
        auto it = root_to_compact.find(root);
        if (it == root_to_compact.end()) continue;  // orphaned — никто не ссылается
        const uint32_t ci = it->second;
        if (compacted[ci].connected_ports.empty()) {
            compacted[ci].index = ci;
            compacted[ci].domain = domain;
        }
        for (auto& p : connected_ports) {
            compacted[ci].connected_ports.push_back(p);
        }
    }

    out.signals = std::move(compacted);
    out.signal_count = next_compact;
}

} // namespace bp2
