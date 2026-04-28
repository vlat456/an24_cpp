#include "window_system.h"
#include "visual/scene_mutations.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "editor/input/editing_host.h"
#include <spdlog/spdlog.h>
#include <cstdio>
#include <filesystem>

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
    return "library/library_index.json";  // fallback (will produce a clear error)
}

} // namespace

namespace {

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

    // scope_id.path() already returns InternedId vector - use directly
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

WindowSystem::WindowSystem()
    : type_registry_(load_component_registry())
    , library_index_(bp2::load_library_index(find_library_index_path()))
    , inspector_()
{
    createDocument();
}

Document& WindowSystem::createDocument() {
    auto doc = std::make_unique<Document>(&type_registry_, &library_index_);
    Document* doc_ptr = doc.get();

    documents_.push_back(std::move(doc));
    setActiveDocument(doc_ptr);
    pending_tab_focus_ = doc_ptr;

    spdlog::info("[WindowSystem] Created new document (total: {})", documents_.size());

    return *doc_ptr;
}

Document* WindowSystem::openDocument(const std::string& path) {
    // Check if already open
    if (Document* existing = findDocumentByPath(path)) {
        setActiveDocument(existing);
        pending_tab_focus_ = existing;
        settings.addRecentFile(path);
        spdlog::info("[WindowSystem] Document already open: {}", path);
        return existing;
    }

    // If only one document exists and it's pristine (empty Untitled), replace it
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

    auto doc = std::make_unique<Document>(&type_registry_, &library_index_);
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

    // Track open tabs before closing
    if (!doc.filepath().empty()) {
        settings.removeOpenTab(doc.filepath());
    }

    // If this was the active document, pick a replacement BEFORE erasing
    if (active_document_ == &doc) {
        active_document_ = nullptr;
        // Prefer the next document, or the previous one
        auto next = std::next(it);
        if (next != documents_.end()) {
            active_document_ = next->get();
        } else if (it != documents_.begin()) {
            active_document_ = std::prev(it)->get();
        }
    }

    // Force-close all popups/menu state owned by the closing document.
    // If we only reset source_document_id without closing show, the render
    // path falls back to activeDocument() and can mutate the wrong doc.
    const editor::DocumentId& closing_id = doc.id();
    if (contextMenu.source_document_id == closing_id) {
        contextMenu.source_document_id.reset();
        contextMenu.show = false;
    }
    if (nodeContextMenu.source_document_id == closing_id) {
        nodeContextMenu.source_document_id.reset();
        nodeContextMenu.show = false;
    }
    if (colorPicker.source_document_id == closing_id) {
        colorPicker.source_document_id.reset();
        colorPicker.show = false;
    }
    if (pendingBakeIn.document_id == closing_id) {
        pendingBakeIn.reset();
    }
    if (setName.document_id == closing_id) {
        setName.document_id.reset();
        setName.show = false;
    }
    if (pendingExtract.document_id == closing_id) {
        pendingExtract.reset();
    }
    if (inlineValueEditor.document_id == closing_id) {
        inlineValueEditor.close();
    }
    if (pending_tab_focus_ == &doc) {
        pending_tab_focus_ = nullptr;
    }

    if (properties_window_.owner_document_id() == std::optional<editor::DocumentId>(closing_id)) {
        properties_window_.close();
        properties_window_.clear_owner_document_id();
    }

    // Purge oscilloscope probes and hover state for the closing document
    oscilloscope.purge_for(closing_id);

    documents_.erase(it);

    // Update inspector and ensure active_document_ is set
    if (!active_document_) {
        if (documents_.empty()) {
            createDocument();  // setActiveDocument called inside
        } else {
            setActiveDocument(documents_.front().get());
        }
    } else {
        // Force inspector update (setActiveDocument skips if pointer unchanged)
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
    // Close properties window (holds raw pointers into document state)
    if (properties_window_.is_open()) {
        properties_window_.close();
    }
    properties_window_.clear_owner_document_id();

    // Clear all dangling references to documents being destroyed
    contextMenu.source_document_id.reset();
    contextMenu.show = false;
    nodeContextMenu.source_document_id.reset();
    nodeContextMenu.show = false;
    colorPicker.source_document_id.reset();
    colorPicker.show = false;
    pendingBakeIn.reset();
    setName.document_id.reset();
    setName.show = false;
    pendingExtract.reset();
    inlineValueEditor.close();
    pending_tab_focus_ = nullptr;

    // Purge all oscilloscope state (probes + hover) for every document.
    oscilloscope.purge_all();

    documents_.clear();
    active_document_ = nullptr;
    createDocument();

    return true;
}

void WindowSystem::setActiveDocument(Document* doc) {
    if (active_document_ != doc) {
        active_document_ = doc;
        if (doc) {
            inspector_.setBlueprint(doc->blueprint(), doc->arena(), doc->interner(),
                                    WindowScopeId::root(), doc->type_registry());
            inspector_.markDirty();
            spdlog::debug("[WindowSystem] Active document: {}", doc->displayName());
        }
    }
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

void WindowSystem::removeClosedDocuments() {
    // This is a no-op for now - documents are removed immediately in closeDocument
    // This method exists for future deferred removal if needed
}

void WindowSystem::openPropertiesForNode(core::InternedId node_id,
                                         const WindowScopeId& scope_id,
                                         Document& doc) {
    // External-scope windows are read-only references — property editing is
    // rejected because their EditingHost resolves against the root model, not
    // the external blueprint's own identity space.
    if (scope_id.is_external()) {
        return;
    }

    const bp2::Blueprint::Node* node = doc.find_node_in_scope(scope_id, node_id);
    if (!node) return;

    std::unique_ptr<EditingHost> owned_host;
    const ComponentRegistry* reg = doc.type_registry();
    core::StringInterner* interner = &doc.interner();
    const bp2::PathArena* arena = &doc.arena();
    if (scope_id.is_root()) {
        owned_host = create_editor_model_host(doc.model(), reg, interner, arena);
    } else if (scope_id.is_embedded()) {
        // scope_id.path() already returns InternedId vector - use directly
        owned_host = create_pathful_embedded_host(doc.model(),
            std::vector<core::InternedId>(scope_id.path().begin(), scope_id.path().end()),
            reg, interner, arena);
    }

    if (!owned_host) {
        return;
    }

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

    // Pre-build the EditingHost for embedded scopes so the dialog doesn't
    // recreate it every render frame.
    inlineValueEditor.cached_host.reset();
    if (scope_id.is_embedded()) {
        // scope_id.path() already returns InternedId vector - use directly
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
    if (colorPicker.source_document_id) {
        Document* doc = findDocumentById(*colorPicker.source_document_id);
        if (!doc || !scoped_node_still_exists(*doc, colorPicker.scope_id, colorPicker.node_id)) {
            colorPicker.source_document_id.reset();
            colorPicker.show = false;
        }
    }

    if (pendingBakeIn.document_id) {
        Document* doc = findDocumentById(*pendingBakeIn.document_id);
        if (!doc || pendingBakeIn.scope_id.is_external()
            || !scoped_node_still_exists(*doc, pendingBakeIn.scope_id, pendingBakeIn.node_id)) {
            pendingBakeIn.reset();
        }
    }

    if (pendingExtract.document_id) {
        Document* doc = findDocumentById(*pendingExtract.document_id);
        if (!doc || pendingExtract.scope_id.is_external()) {
            pendingExtract.reset();
        } else {
            std::unique_ptr<EditingHost> host = create_scoped_host(*doc, pendingExtract.scope_id);
            if (!host) {
                pendingExtract.reset();
            } else {
                bool all_selected_exist = true;
                for (core::InternedId iid : pendingExtract.selected_node_ids) {
                    if (!host->find_node(iid)) {
                        all_selected_exist = false;
                        break;
                    }
                }
                if (!all_selected_exist) {
                    pendingExtract.reset();
                }
            }
        }
    }

    if (setName.document_id && !findDocumentById(*setName.document_id)) {
        setName.document_id.reset();
        setName.show = false;
    }

    if (inlineValueEditor.document_id) {
        Document* doc = findDocumentById(*inlineValueEditor.document_id);
        if (!doc || inlineValueEditor.scope_id.is_external()
            || !scoped_node_still_exists(*doc, inlineValueEditor.scope_id, inlineValueEditor.node_id)) {
            inlineValueEditor.close();
        }
    }

    if (properties_window_.is_open()) {
        const auto& owner_id = properties_window_.owner_document_id();
        if (!owner_id.has_value() || !findDocumentById(*owner_id)) {
            properties_window_.close();
            properties_window_.clear_owner_document_id();
        }
    }
}
