#pragma once

#include "document.h"
#include "editor_settings.h"
#include "visual/inspector/inspector.h"
#include "window/properties_window.h"
#include "json_parser/json_parser.h"
#include "commands/extract_blueprint.h"
#include "oscilloscope.h"
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

    // ── Tab focus (one-shot programmatic selection) ──

    /// Returns the document that should receive tab focus this frame, or nullptr.
    Document* pendingTabFocus() const { return pending_tab_focus_; }

    /// Clear the pending tab focus after it has been applied. Call once per frame.
    void consumeTabFocus() { pending_tab_focus_ = nullptr; }

    /// Set pending tab focus (used when restoring active tab on startup)
    void setPendingTabFocus(Document* doc) { pending_tab_focus_ = doc; }

    // ── Document access ──

    const std::vector<std::unique_ptr<Document>>& documents() const { return documents_; }
    size_t documentCount() const { return documents_.size(); }
    Document* findDocumentByPath(const std::string& path);
    Document* findDocumentById(const std::string& id);

    // ── Global panels ──

    Inspector& inspector() { return inspector_; }
    PropertiesWindow& propertiesWindow() { return properties_window_; }
    TypeRegistry& typeRegistry() { return type_registry_; }

    // ── Context menu state (with source document) ──

    struct ContextMenuState {
        bool show = false;
        Pt position;
        std::string group_id;
        std::string source_doc_id;  ///< Resolved via findDocumentById()
    } contextMenu;

    struct NodeContextMenuState {
        bool show = false;
        std::string node_id;
        std::string group_id;
        std::string source_doc_id;  ///< Resolved via findDocumentById()
    } nodeContextMenu;

    struct ColorPickerState {
        bool show = false;
        std::string node_id;
        std::string group_id;
        std::string source_doc_id;  ///< Resolved via findDocumentById()
        float rgba[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    } colorPicker;

    struct PendingBakeIn {
        bool show_confirmation = false;
        std::string doc_id;  ///< Resolved via findDocumentById()
        std::string sub_blueprint_id;
    } pendingBakeIn;

    struct SetNameState {
        bool show = false;
        std::string doc_id;            ///< Document whose blueprint name to set
        bool save_after = false;       ///< If true, trigger save after name is confirmed
        char buf[128] = {};            ///< ImGui input buffer
    } setName;

    struct PendingExtractToBlueprint {
        bool show_dialog = false;
        std::string doc_id;
        std::string group_id;
        std::vector<ui::InternedId> selected_node_ids;
        char name_buf[128] = {};
        bool has_preview = false;
        editor::commands::ExtractToBlueprintPreview preview;
        std::string preview_error;
    } pendingExtract;

    bool showInspector = true;
    bool showOscilloscope = true;
    EditorSettings settings;
    OscilloscopeModel oscilloscope;

    // ── Utility ──

    /// Remove documents that were marked closed. Call at end of frame.
    void removeClosedDocuments();

    /// Open properties for a node in the active document
    void openPropertiesForNode(const std::string& node_id, Document& doc);

    /// Open color picker for a node
    void openColorPickerForNode(const std::string& node_id, const std::string& group_id, Document& doc);

    /// Dispatch InputResultAction from a document to the window system
    void handleInputAction(const Document::InputResultAction& action, Document& doc);

private:
    std::vector<std::unique_ptr<Document>> documents_;
    Document* active_document_ = nullptr;
    Document* pending_tab_focus_ = nullptr;  ///< One-shot: set by create/open, consumed by tab bar
    TypeRegistry type_registry_;
    Inspector inspector_;
    PropertiesWindow properties_window_;
};
