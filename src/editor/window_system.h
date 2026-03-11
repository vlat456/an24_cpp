#pragma once

#include "document.h"
#include "visual/inspector/inspector.h"
#include "window/properties_window.h"
#include "json_parser/json_parser.h"
#include <memory>
#include <vector>
#include <string>

/// Manages all open documents and global panels.
/// Replaces EditorApp as the top-level controller.
class WindowSystem {
public:
    WindowSystem();

    // ── Document lifecycle ──

    Document& createDocument();
    Document* openDocument(const std::string& path);
    bool closeDocument(Document& doc);
    bool closeAllDocuments();

    // ── Active document ──

    Document* activeDocument() { return active_document_; }
    const Document* activeDocument() const { return active_document_; }
    void setActiveDocument(Document* doc);

    // ── Document access ──

    const std::vector<std::unique_ptr<Document>>& documents() const { return documents_; }
    size_t documentCount() const { return documents_.size(); }
    Document* findDocumentByPath(const std::string& path);

    // ── Global panels ──

    Inspector& inspector() { return inspector_; }
    PropertiesWindow& propertiesWindow() { return properties_window_; }
    an24::TypeRegistry& typeRegistry() { return type_registry_; }

    // ── Context menu state (with source document) ──

    struct ContextMenuState {
        bool show = false;
        Pt position;
        std::string group_id;
        Document* source_doc = nullptr;
    } contextMenu;

    struct NodeContextMenuState {
        bool show = false;
        size_t node_index = 0;
        std::string group_id;
        Document* source_doc = nullptr;
    } nodeContextMenu;

    struct ColorPickerState {
        bool show = false;
        size_t node_index = 0;
        std::string group_id;
        Document* source_doc = nullptr;
        float rgba[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    } colorPicker;

    bool showInspector = true;

    // ── Utility ──

    /// Remove documents that were marked closed. Call at end of frame.
    void removeClosedDocuments();

    /// Open properties for a node in the active document
    void openPropertiesForNode(size_t node_index, Document& doc);

    /// Open color picker for a node
    void openColorPickerForNode(size_t node_index, const std::string& group_id, Document& doc);

    /// Dispatch InputResultAction from a document to the window system
    void handleInputAction(const Document::InputResultAction& action, Document& doc);

private:
    std::vector<std::unique_ptr<Document>> documents_;
    Document* active_document_ = nullptr;
    an24::TypeRegistry type_registry_;
    Inspector inspector_;
    PropertiesWindow properties_window_;
};
