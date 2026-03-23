#include "window_system.h"
#include "visual/scene_mutations.h"
#include <spdlog/spdlog.h>

WindowSystem::WindowSystem()
    : type_registry_(load_type_registry())
    , inspector_()
{
    createDocument();
}

Document& WindowSystem::createDocument() {
    auto doc = std::make_unique<Document>();
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

    auto doc = std::make_unique<Document>();
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

    // Clear any context menu / color picker references to the closing doc
    const std::string& closing_id = doc.id();
    if (contextMenu.source_doc_id == closing_id) contextMenu.source_doc_id.clear();
    if (nodeContextMenu.source_doc_id == closing_id) nodeContextMenu.source_doc_id.clear();
    if (colorPicker.source_doc_id == closing_id) {
        colorPicker.source_doc_id.clear();
        colorPicker.show = false;
    }
    if (pendingBakeIn.doc_id == closing_id) {
        pendingBakeIn.doc_id.clear();
        pendingBakeIn.show_confirmation = false;
    }
    if (setName.doc_id == closing_id) {
        setName.doc_id.clear();
        setName.show = false;
    }
    if (pendingExtract.doc_id == closing_id) {
        pendingExtract.doc_id.clear();
        pendingExtract.group_id.clear();
        pendingExtract.selected_node_ids.clear();
        pendingExtract.show_dialog = false;
    }
    if (pending_tab_focus_ == &doc) {
        pending_tab_focus_ = nullptr;
    }

    // Close properties window if it targets this document's blueprint
    if (properties_window_.is_open() && !properties_window_.target_node_id_str().empty()) {
        // PropertiesWindow stores raw pointers into the document's model/interner.
        // If the node it targets exists in the closing document, those pointers
        // will dangle after destruction — close the window to be safe.
        ui::InternedId iid = doc.interner().lookup(properties_window_.target_node_id_str());
        if (!iid.empty() && doc.blueprint().find_node(iid)) {
            properties_window_.close();
        }
    }

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
                                active_document_->interner());
        inspector_.markDirty();
    }

    return true;
}

bool WindowSystem::closeAllDocuments() {
    // Close properties window (holds raw pointers into document state)
    if (properties_window_.is_open()) {
        properties_window_.close();
    }

    // Clear all dangling references to documents being destroyed
    contextMenu.source_doc_id.clear();
    contextMenu.show = false;
    nodeContextMenu.source_doc_id.clear();
    nodeContextMenu.show = false;
    colorPicker.source_doc_id.clear();
    colorPicker.show = false;
    pendingBakeIn.doc_id.clear();
    pendingBakeIn.show_confirmation = false;
    pendingExtract.doc_id.clear();
    pendingExtract.group_id.clear();
    pendingExtract.selected_node_ids.clear();
    pendingExtract.show_dialog = false;
    pending_tab_focus_ = nullptr;

    documents_.clear();
    active_document_ = nullptr;
    createDocument();

    return true;
}

void WindowSystem::setActiveDocument(Document* doc) {
    if (active_document_ != doc) {
        active_document_ = doc;
        if (doc) {
            inspector_.setBlueprint(doc->blueprint(), doc->arena(), doc->interner());
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

Document* WindowSystem::findDocumentById(const std::string& id) {
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

void WindowSystem::openPropertiesForNode(const std::string& node_id, Document& doc) {
    ui::InternedId iid = doc.interner().lookup(node_id);
    if (iid.empty()) return;
    const bp2::Blueprint::Node* node = doc.blueprint().find_node(iid);
    if (!node) return;

    Document* doc_ptr = &doc;
    properties_window_.open(*node, node_id, doc.model(), doc.interner(),
        [this, doc_ptr](const std::string& nid) {
            // Verify document still exists before using the pointer
            for (const auto& d : documents_) {
                if (d.get() == doc_ptr) {
                    // Rebuild all windows (cancels gestures, syncs visual + simulation)
                    doc_ptr->rebuildAllWindows();
                    inspector_.markDirty();
                    return;
                }
            }
            // Document was closed — just mark inspector dirty
            inspector_.markDirty();
        });
}

void WindowSystem::openColorPickerForNode(const std::string& node_id, const std::string& group_id, Document& doc) {
    ui::InternedId iid = doc.interner().lookup(node_id);
    if (iid.empty()) return;
    const bp2::Blueprint::Node* node = doc.blueprint().find_node(iid);
    if (!node) return;

    colorPicker.node_id = node_id;
    colorPicker.group_id = group_id;
    colorPicker.source_doc_id = doc.id();
    colorPicker.show = true;

    if (node->has_color) {
        colorPicker.rgba[0] = node->color_r;
        colorPicker.rgba[1] = node->color_g;
        colorPicker.rgba[2] = node->color_b;
        colorPicker.rgba[3] = node->color_a;
    } else {
        colorPicker.rgba[0] = 0.19f;
        colorPicker.rgba[1] = 0.19f;
        colorPicker.rgba[2] = 0.25f;
        colorPicker.rgba[3] = 1.0f;
    }
}

void WindowSystem::handleInputAction(const Document::InputResultAction& action, Document& doc) {
    if (action.show_context_menu) {
        contextMenu.show = true;
        contextMenu.position = action.context_menu_pos;
        contextMenu.group_id = action.context_menu_group_id;
        contextMenu.source_doc_id = doc.id();
    }
    if (action.show_node_context_menu) {
        nodeContextMenu.show = true;
        nodeContextMenu.node_id = action.context_menu_node_id;
        nodeContextMenu.group_id = action.node_context_menu_group_id;
        nodeContextMenu.source_doc_id = doc.id();
    }
    if (!action.toggle_probe_wire_id.empty()) {
        const ui::Pt* click = action.has_toggle_probe_world_pos ? &action.toggle_probe_world_pos : nullptr;
        oscilloscope.toggle_probe(doc, action.toggle_probe_group_id, action.toggle_probe_wire_id, click);
    }
}
