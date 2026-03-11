#include "window_system.h"
#include "data/blueprint.h"
#include <spdlog/spdlog.h>

WindowSystem::WindowSystem()
    : type_registry_(an24::load_type_registry())
    , inspector_()
{
    createDocument();
}

Document& WindowSystem::createDocument() {
    auto doc = std::make_unique<Document>();
    Document* doc_ptr = doc.get();

    documents_.push_back(std::move(doc));
    setActiveDocument(doc_ptr);

    spdlog::info("[WindowSystem] Created new document (total: {})", documents_.size());

    return *doc_ptr;
}

Document* WindowSystem::openDocument(const std::string& path) {
    // Check if already open
    if (Document* existing = findDocumentByPath(path)) {
        setActiveDocument(existing);
        spdlog::info("[WindowSystem] Document already open: {}", path);
        return existing;
    }

    auto doc = std::make_unique<Document>();
    if (!doc->load(path)) {
        spdlog::error("[WindowSystem] Failed to load document: {}", path);
        return nullptr;
    }

    Document* doc_ptr = doc.get();
    documents_.push_back(std::move(doc));
    setActiveDocument(doc_ptr);

    spdlog::info("[WindowSystem] Opened document: {} (total: {})", path, documents_.size());

    return doc_ptr;
}

bool WindowSystem::closeDocument(Document& doc) {
    if (doc.isModified()) {
        // Caller should handle save prompt - return false to indicate cancelled
        return false;
    }

    // Find the document to remove
    auto it = std::find_if(documents_.begin(), documents_.end(),
                            [&doc](const auto& ptr) { return ptr.get() == &doc; });
    if (it == documents_.end()) return false;

    spdlog::info("[WindowSystem] Closing document: {}", doc.displayName());

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
    if (contextMenu.source_doc == &doc) contextMenu.source_doc = nullptr;
    if (nodeContextMenu.source_doc == &doc) nodeContextMenu.source_doc = nullptr;
    if (colorPicker.source_doc == &doc) {
        colorPicker.source_doc = nullptr;
        colorPicker.show = false;
    }

    documents_.erase(it);

    // If no documents remain, create a fresh one
    if (!active_document_) {
        if (documents_.empty()) {
            createDocument();
        } else {
            setActiveDocument(documents_.front().get());
        }
    } else {
        setActiveDocument(active_document_);  // update inspector
    }

    return true;
}

bool WindowSystem::closeAllDocuments() {
    for (auto& doc : documents_) {
        if (doc->isModified()) {
            return false;  // Cancel if any document is modified
        }
    }

    documents_.clear();
    active_document_ = nullptr;
    createDocument();

    return true;
}

void WindowSystem::setActiveDocument(Document* doc) {
    if (active_document_ != doc) {
        active_document_ = doc;
        if (doc) {
            inspector_.setScene(doc->scene());
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

void WindowSystem::removeClosedDocuments() {
    // This is a no-op for now - documents are removed immediately in closeDocument
    // This method exists for future deferred removal if needed
}

void WindowSystem::openPropertiesForNode(size_t node_index, Document& doc) {
    if (node_index >= doc.blueprint().nodes.size()) return;
    Node& node = doc.blueprint().nodes[node_index];
    Document* doc_ptr = &doc;
    properties_window_.open(node, [this, doc_ptr](const std::string& node_id) {
        // Verify document still exists before using the pointer
        for (const auto& d : documents_) {
            if (d.get() == doc_ptr) {
                doc_ptr->scene().cache().invalidate(node_id);
                inspector_.markDirty();
                doc_ptr->rebuildSimulation();
                doc_ptr->markModified();
                return;
            }
        }
        // Document was closed — just mark inspector dirty
        inspector_.markDirty();
    });
}

void WindowSystem::openColorPickerForNode(size_t node_index, const std::string& group_id, Document& doc) {
    if (node_index >= doc.blueprint().nodes.size()) return;

    colorPicker.node_index = node_index;
    colorPicker.group_id = group_id;
    colorPicker.source_doc = &doc;
    colorPicker.show = true;

    const Node& node = doc.blueprint().nodes[node_index];
    if (node.color.has_value()) {
        colorPicker.rgba[0] = node.color->r;
        colorPicker.rgba[1] = node.color->g;
        colorPicker.rgba[2] = node.color->b;
        colorPicker.rgba[3] = node.color->a;
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
        contextMenu.source_doc = &doc;
    }
    if (action.show_node_context_menu) {
        nodeContextMenu.show = true;
        nodeContextMenu.node_index = action.context_menu_node_index;
        nodeContextMenu.group_id = action.node_context_menu_group_id;
        nodeContextMenu.source_doc = &doc;
    }
}
