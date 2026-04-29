#include "window_system.h"
#include "visual/scene_mutations.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "editor/input/editing_host.h"
#include <spdlog/spdlog.h>
#include <cstdio>
#include <filesystem>

// =====================================================================
// Helpers (file-local)
// =====================================================================

namespace {

std::string find_library_index_path() {
    static const char* candidates[] = {
        "library/library_index.json",
        "../library/library_index.json",
        "../../library/library_index.json",
        "../../../library/library_index.json",
    };
    for (const char* p : candidates) {
        if (std::filesystem::exists(p)) {
            return p;
        }
    }
    return "library/library_index.json";
}

std::unique_ptr<EditingHost> create_scoped_host(Document& doc, const WindowScopeId& scope_id) {
    const ComponentRegistry* reg = doc.type_registry();
    core::StringInterner* interner = &doc.interner();
    const bp2::PathArena* arena = &doc.arena();
    if (scope_id.is_external()) {
        return nullptr;
    }
    if (scope_id.is_root()) {
        return create_editor_model_host(doc.model(), reg, interner, arena);
    }

    return create_pathful_embedded_host(doc.model(),
        std::vector<core::InternedId>(scope_id.path().begin(), scope_id.path().end()),
        reg, interner, arena);
}

bool scoped_node_still_exists(Document& doc,
                              const WindowScopeId& scope_id,
                              core::InternedId node_id) {
    return doc.find_node_in_scope(scope_id, node_id) != nullptr;
}

} // namespace

// =====================================================================
// Transient UI struct protocol — out-of-line definitions
// =====================================================================

// -- ContextMenuState --

bool WindowSystem::ContextMenuState::is_open() const { return show; }
void WindowSystem::ContextMenuState::close() { show = false; source_document_id.reset(); }
bool WindowSystem::ContextMenuState::owns_document(const editor::DocumentId& id) const {
    return source_document_id.has_value() && *source_document_id == id;
}
bool WindowSystem::ContextMenuState::still_valid(WindowSystem& ws) const {
    return !source_document_id.has_value() || ws.findDocumentById(*source_document_id) != nullptr;
}

// -- NodeContextMenuState --

bool WindowSystem::NodeContextMenuState::is_open() const { return show; }
void WindowSystem::NodeContextMenuState::close() { show = false; source_document_id.reset(); }
bool WindowSystem::NodeContextMenuState::owns_document(const editor::DocumentId& id) const {
    return source_document_id.has_value() && *source_document_id == id;
}
bool WindowSystem::NodeContextMenuState::still_valid(WindowSystem& ws) const {
    return !source_document_id.has_value() || ws.findDocumentById(*source_document_id) != nullptr;
}

// -- ColorPickerState --

bool WindowSystem::ColorPickerState::is_open() const { return show; }
void WindowSystem::ColorPickerState::close() { show = false; source_document_id.reset(); }
bool WindowSystem::ColorPickerState::owns_document(const editor::DocumentId& id) const {
    return source_document_id.has_value() && *source_document_id == id;
}
bool WindowSystem::ColorPickerState::still_valid(WindowSystem& ws) const {
    if (!source_document_id) return true;
    Document* doc = ws.findDocumentById(*source_document_id);
    if (!doc) return false;
    return scoped_node_still_exists(*doc, scope_id, node_id);
}

// -- PendingBakeIn --

bool WindowSystem::PendingBakeIn::is_open() const { return show_confirmation; }
void WindowSystem::PendingBakeIn::close() {
    show_confirmation = false;
    document_id.reset();
    scope_id = WindowScopeId::root();
    node_id = {};
}
bool WindowSystem::PendingBakeIn::owns_document(const editor::DocumentId& id) const {
    return document_id.has_value() && *document_id == id;
}
bool WindowSystem::PendingBakeIn::still_valid(WindowSystem& ws) const {
    if (!document_id) return true;
    Document* doc = ws.findDocumentById(*document_id);
    if (!doc || scope_id.is_external()) return false;
    return scoped_node_still_exists(*doc, scope_id, node_id);
}

// -- SetNameState --

bool WindowSystem::SetNameState::is_open() const { return show; }
void WindowSystem::SetNameState::close() { show = false; document_id.reset(); }
bool WindowSystem::SetNameState::owns_document(const editor::DocumentId& id) const {
    return document_id.has_value() && *document_id == id;
}
bool WindowSystem::SetNameState::still_valid(WindowSystem& ws) const {
    return !document_id.has_value() || ws.findDocumentById(*document_id) != nullptr;
}

// -- PendingExtractToBlueprint --

bool WindowSystem::PendingExtractToBlueprint::is_open() const { return show_dialog; }
void WindowSystem::PendingExtractToBlueprint::close() {
    show_dialog = false;
    document_id.reset();
    scope_id = WindowScopeId::root();
    selected_node_ids.clear();
    std::memset(name_buf, 0, sizeof(name_buf));
    has_preview = false;
    preview = {};
    preview_error.clear();
    preview_name.clear();
    allow_nonembedded_descendant_refs = false;
    preview_allow_nonembedded_descendant_refs = false;
}
bool WindowSystem::PendingExtractToBlueprint::owns_document(const editor::DocumentId& id) const {
    return document_id.has_value() && *document_id == id;
}
bool WindowSystem::PendingExtractToBlueprint::still_valid(WindowSystem& ws) const {
    if (!document_id) return true;
    Document* doc = ws.findDocumentById(*document_id);
    if (!doc || scope_id.is_external()) return false;
    auto host = create_scoped_host(*doc, scope_id);
    if (!host) return false;
    for (auto iid : selected_node_ids) {
        if (!host->find_node(iid)) return false;
    }
    return true;
}

// -- ZNTuneState --

bool WindowSystem::ZNTuneState::is_open() const { return show_result_popup; }
void WindowSystem::ZNTuneState::close() { show_result_popup = false; }
bool WindowSystem::ZNTuneState::owns_document(const editor::DocumentId&) const { return false; }
bool WindowSystem::ZNTuneState::still_valid(WindowSystem&) const { return true; }

// -- InlineValueEditorState --

bool WindowSystem::InlineValueEditorState::is_open() const { return open; }
void WindowSystem::InlineValueEditorState::close() {
    open = false;
    document_id.reset();
    cached_host.reset();
}
bool WindowSystem::InlineValueEditorState::owns_document(const editor::DocumentId& id) const {
    return document_id.has_value() && *document_id == id;
}
bool WindowSystem::InlineValueEditorState::still_valid(WindowSystem& ws) const {
    if (!document_id) return true;
    Document* doc = ws.findDocumentById(*document_id);
    if (!doc || scope_id.is_external()) return false;
    return scoped_node_still_exists(*doc, scope_id, node_id);
}

// =====================================================================
// WindowSystem
// =====================================================================

WindowSystem::WindowSystem()
    : type_registry_(load_component_registry())
    , library_index_(bp2::load_library_index(find_library_index_path()))
    , inspector_()
{
    register_transient_ui();
    createDocument();
}

void WindowSystem::register_transient_ui() {
    transient_ui_.register_entry(contextMenu);
    transient_ui_.register_entry(nodeContextMenu);
    transient_ui_.register_entry(colorPicker);
    transient_ui_.register_entry(pendingBakeIn);
    transient_ui_.register_entry(setName);
    transient_ui_.register_entry(pendingExtract);
    transient_ui_.register_entry(znTune);
    transient_ui_.register_entry(inlineValueEditor);
    transient_ui_.register_entry(properties_window_);
    transient_ui_.register_entry(script_editor_window_);
}

Document& WindowSystem::createDocument() {
    auto doc = std::make_unique<Document>(&type_registry_, &library_index_, &rendering_resources_);
    Document* doc_ptr = doc.get();

    documents_.push_back(std::move(doc));
    setActiveDocument(doc_ptr);
    pending_tab_focus_ = doc_ptr;

    spdlog::info("[WindowSystem] Created new document (total: {})", documents_.size());

    return *doc_ptr;
}

Document* WindowSystem::openDocument(const std::string& path) {
    if (Document* existing = findDocumentByPath(path)) {
        setActiveDocument(existing);
        pending_tab_focus_ = existing;
        settings.addRecentFile(path);
        spdlog::info("[WindowSystem] Document already open: {}", path);
        return existing;
    }

    if (documents_.size() == 1 && documents_.front()->isPristine()) {
        Document* pristine = documents_.front().get();
        if (pristine->load(path)) {
            settings.addRecentFile(path);
            settings.addOpenTab(path);
            pending_tab_focus_ = pristine;
            spdlog::info("[WindowSystem] Replaced pristine Untitled with: {}", path);
            return pristine;
        }
        spdlog::error("[WindowSystem] Failed to load document: {}", path);
        return nullptr;
    }

    auto doc = std::make_unique<Document>(&type_registry_, &library_index_, &rendering_resources_);
    if (!doc->load(path)) {
        spdlog::error("[WindowSystem] Failed to load document: {}", path);
        return nullptr;
    }

    Document* doc_ptr = doc.get();
    documents_.push_back(std::move(doc));
    setActiveDocument(doc_ptr);
    pending_tab_focus_ = doc_ptr;
    settings.addRecentFile(path);
    settings.addOpenTab(path);

    spdlog::info("[WindowSystem] Opened document: {} (total: {})", path, documents_.size());

    return doc_ptr;
}

bool WindowSystem::closeDocument(Document& doc) {
    auto it = std::find_if(documents_.begin(), documents_.end(),
                            [&doc](const auto& ptr) { return ptr.get() == &doc; });
    if (it == documents_.end()) return false;

    spdlog::info("[WindowSystem] Closing document: {}", doc.displayName());

    if (!doc.filepath().empty()) {
        settings.removeOpenTab(doc.filepath());
    }

    if (active_document_ == &doc) {
        active_document_ = nullptr;
        auto next = std::next(it);
        if (next != documents_.end()) {
            active_document_ = next->get();
        } else if (it != documents_.begin()) {
            active_document_ = std::prev(it)->get();
        }
    }

    const editor::DocumentId closing_id = doc.id();

    transient_ui_.close_for_document(closing_id);

    if (pending_tab_focus_ == &doc) {
        pending_tab_focus_ = nullptr;
    }

    oscilloscope.purge_for(closing_id);

    documents_.erase(it);

    if (!active_document_) {
        if (documents_.empty()) {
            createDocument();
        } else {
            setActiveDocument(documents_.front().get());
        }
    } else {
        resetFocusToRoot();
        inspector_.setBlueprint(active_document_->blueprint(),
                                active_document_->arena(),
                                active_document_->interner(),
                                WindowScopeId::root(),
                                active_document_->type_registry());
        inspector_.markDirty();
    }

    return true;
}

bool WindowSystem::closeAllDocuments() {
    transient_ui_.close_all();

    focus_scope.clear();
    oscilloscope.purge_all();
    pending_tab_focus_ = nullptr;

    documents_.clear();
    active_document_ = nullptr;
    createDocument();

    return true;
}

void WindowSystem::setActiveDocument(Document* doc) {
    if (active_document_ != doc) {
        active_document_ = doc;
        resetFocusToRoot();
        if (doc) {
            inspector_.setBlueprint(doc->blueprint(), doc->arena(), doc->interner(),
                                    WindowScopeId::root(), doc->type_registry());
            inspector_.markDirty();
            spdlog::debug("[WindowSystem] Active document: {}", doc->displayName());
        }
    }
}

void WindowSystem::resetFocusToRoot() {
    if (active_document_) {
        focus_scope.document_id = active_document_->id();
        focus_scope.scope_id = WindowScopeId::root();
    } else {
        focus_scope.clear();
    }
}

FocusScope::Resolved WindowSystem::resolve_focus() {
    FocusScope::Resolved r;
    if (!focus_scope.is_set()) return r;

    r.document = findDocumentById(focus_scope.document_id);
    if (!r.document) return r;

    if (focus_scope.scope_id.is_root()) {
        r.window = &r.document->root();
    } else {
        r.window = r.document->windowManager().find(focus_scope.scope_id);
        if (!r.window) {
            r.window = &r.document->root();
        }
    }
    return r;
}

Document* WindowSystem::findDocumentByPath(const std::string& path) {
    for (auto& doc : documents_) {
        if (doc->filepath() == path) {
            return doc.get();
        }
    }
    return nullptr;
}

Document* WindowSystem::findDocumentById(const editor::DocumentId& id) {
    if (id.empty()) return nullptr;
    for (auto& doc : documents_) {
        if (doc->id() == id) {
            return doc.get();
        }
    }
    return nullptr;
}

const Document* WindowSystem::findDocumentById(const editor::DocumentId& id) const {
    if (id.empty()) return nullptr;
    for (const auto& doc : documents_) {
        if (doc->id() == id) {
            return doc.get();
        }
    }
    return nullptr;
}

void WindowSystem::removeClosedDocuments() {
}

void WindowSystem::openPropertiesForNode(core::InternedId node_id,
                                         const WindowScopeId& scope_id,
                                         Document& doc) {
    if (scope_id.is_external()) return;

    const bp2::Blueprint::Node* node = doc.find_node_in_scope(scope_id, node_id);
    if (!node) return;

    std::unique_ptr<EditingHost> owned_host = create_scoped_host(doc, scope_id);
    if (!owned_host) return;

    const editor::DocumentId owner_id = doc.id();
    properties_window_.open(*node, node_id, std::move(owned_host), doc.interner(),
        doc.type_registry(),
        [this, owner_id](core::InternedId nid) {
            (void)nid;
            if (Document* owner = findDocumentById(owner_id)) {
                owner->rebuildAllWindows();
                inspector_.markDirty();
                return;
            }
            properties_window_.clear_owner_document_id();
            inspector_.markDirty();
        });
    properties_window_.set_owner_document_id(doc.id());
}

void WindowSystem::openScriptEditorForNode(core::InternedId node_id,
                                            const WindowScopeId& scope_id,
                                            Document& doc) {
    if (scope_id.is_external()) return;

    const bp2::Blueprint::Node* node = doc.find_node_in_scope(scope_id, node_id);
    if (!node) return;

    std::unique_ptr<EditingHost> owned_host = create_scoped_host(doc, scope_id);
    if (!owned_host) return;

    const editor::DocumentId owner_id = doc.id();
    script_editor_window_.open(*node, node_id, std::move(owned_host), doc.interner(),
        [this, owner_id](core::InternedId) {
            if (Document* owner = findDocumentById(owner_id)) {
                owner->rebuildAllWindows();
                inspector_.markDirty();
                return;
            }
            script_editor_window_.clear_owner_document_id();
            inspector_.markDirty();
        });
    script_editor_window_.set_owner_document_id(doc.id());
}

void WindowSystem::openColorPickerForNode(core::InternedId node_id, const WindowScopeId& scope_id, Document& doc) {
    if (scope_id.is_external()) {
        return;
    }

    const bp2::Blueprint::Node* node = doc.find_node_in_scope(scope_id, node_id);
    if (!node) return;

    colorPicker.node_id = node_id;
    colorPicker.scope_id = scope_id;
    colorPicker.source_document_id = doc.id();
    colorPicker.show = true;

    if (const std::optional<editor::NodeColor> color = doc.node_color_for_scope(scope_id, node_id); color.has_value()) {
        colorPicker.rgba[0] = color->r;
        colorPicker.rgba[1] = color->g;
        colorPicker.rgba[2] = color->b;
        colorPicker.rgba[3] = color->a;
    } else {
        colorPicker.rgba[0] = 0.19f;
        colorPicker.rgba[1] = 0.19f;
        colorPicker.rgba[2] = 0.25f;
        colorPicker.rgba[3] = 1.0f;
    }
}

void WindowSystem::openInlineValueEditorForNode(core::InternedId node_id,
                                                const WindowScopeId& scope_id,
                                                Document& doc,
                                                const ui::Pt* anchor_screen) {
    const bp2::Blueprint::Node* node = doc.find_node_in_scope(scope_id, node_id);
    if (!node) return;
    if (node->semantic.type != doc.interner().intern("Value")) return;

    const core::InternedId value_key = doc.interner().intern("value");
    float current = 0.0f;
    auto it = node->semantic.params.find(value_key);
    if (it != node->semantic.params.end()) {
        current = it->second;
    }

    inlineValueEditor.cached_host.reset();
    if (scope_id.is_embedded()) {
        inlineValueEditor.cached_host = create_pathful_embedded_host(doc.model(),
            std::vector<core::InternedId>(scope_id.path().begin(), scope_id.path().end()),
            doc.type_registry(), &doc.interner(), &doc.arena());
    }

    inlineValueEditor.open = true;
    inlineValueEditor.document_id = doc.id();
    inlineValueEditor.node_id = node_id;
    inlineValueEditor.scope_id = scope_id;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(current));
    inlineValueEditor.buffer = buf;
    inlineValueEditor.error.clear();
    inlineValueEditor.has_anchor = (anchor_screen != nullptr);
    if (anchor_screen != nullptr) {
        inlineValueEditor.anchor_screen = *anchor_screen;
    }
}

void WindowSystem::handleInputAction(const Document::InputResultAction& action, Document& doc) {
    if (action.show_context_menu) {
        contextMenu.show = true;
        contextMenu.position = action.context_menu_pos;
        contextMenu.scope_id = action.context_menu_scope_id;
        contextMenu.source_document_id = doc.id();
    }
    if (action.show_node_context_menu) {
        nodeContextMenu.show = true;
        nodeContextMenu.node_id = action.context_menu_node_id;
        nodeContextMenu.scope_id = action.node_context_menu_scope_id;
        nodeContextMenu.source_document_id = doc.id();
    }
    if (!action.toggle_probe_wire_id.empty()) {
        const ui::Pt* click = action.has_toggle_probe_world_pos ? &action.toggle_probe_world_pos : nullptr;
        oscilloscope.toggle_probe(doc, action.toggle_probe_scope_id,
                                  action.toggle_probe_wire_id, click);
    }
    if (action.open_inline_value_editor && !action.inline_value_editor_node_id.empty()) {
        const ui::Pt* anchor = action.has_inline_value_editor_screen_pos
            ? &action.inline_value_editor_screen_pos
            : nullptr;
        openInlineValueEditorForNode(action.inline_value_editor_node_id, action.inline_value_editor_scope_id, doc, anchor);
    }
}

void WindowSystem::reconcile_owner_bound_ui() {
    transient_ui_.reconcile(*this);
}
