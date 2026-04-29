#pragma once

#include "document.h"
#include "editor_settings.h"
#include "editor/rendering_resources.h"
#include "focus_scope.h"
#include "visual/inspector/inspector.h"
#include "editor/input/editing_host.h"
#include "window/properties_window.h"
#include "window/script_editor_window.h"
#include "window/window_scope_id.h"
#include "core/model/component_registry.h"
#include "blueprint_v2/library/library_index.h"
#include "commands/extract_blueprint.h"
#include "pi_zn_tuner.h"
#include "oscilloscope.h"
#include <cstring>
#include <memory>
#include <optional>
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
    Document* findDocumentById(const editor::DocumentId& id);
    const Document* findDocumentById(const editor::DocumentId& id) const;

    // ── Focus scope (menu context) ──

    FocusScope focus_scope;

    /// Reset focus to root of active document. Called on tab switch.
    void resetFocusToRoot();

    /// Resolve focus_scope IDs to live pointers.
    /// Returns {nullptr, nullptr} if document was closed or window removed.
    FocusScope::Resolved resolve_focus();

    // ── Global panels ──

    Inspector& inspector() { return inspector_; }
    PropertiesWindow& propertiesWindow() { return properties_window_; }
    ScriptEditorWindow& scriptEditorWindow() { return script_editor_window_; }
    ComponentRegistry& typeRegistry() { return type_registry_; }
    const bp2::LibraryIndex& libraryIndex() const { return library_index_; }
    editor::RenderingResources& renderingResources() { return rendering_resources_; }

    // ── Context menu state (with source document) ──

    struct ContextMenuState {
        bool show = false;
        Pt position;
        WindowScopeId scope_id = WindowScopeId::root();
        std::optional<editor::DocumentId> source_document_id;
    } contextMenu;

    struct NodeContextMenuState {
        bool show = false;
        core::InternedId node_id;
        WindowScopeId scope_id = WindowScopeId::root();
        std::optional<editor::DocumentId> source_document_id;
    } nodeContextMenu;

    struct ColorPickerState {
        bool show = false;
        core::InternedId node_id;
        WindowScopeId scope_id = WindowScopeId::root();
        std::optional<editor::DocumentId> source_document_id;
        float rgba[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    } colorPicker;

    struct PendingBakeIn {
        bool show_confirmation = false;
        std::optional<editor::DocumentId> document_id;
        WindowScopeId scope_id = WindowScopeId::root();
        core::InternedId node_id;

        void reset() {
            show_confirmation = false;
            document_id.reset();
            scope_id = WindowScopeId::root();
            node_id = {};
        }
    } pendingBakeIn;

    struct SetNameState {
        bool show = false;
        std::optional<editor::DocumentId> document_id;
        bool save_after = false;       ///< If true, trigger save after name is confirmed
        char buf[128] = {};            ///< ImGui input buffer
    } setName;

    struct PendingExtractToBlueprint {
        bool show_dialog = false;
        std::optional<editor::DocumentId> document_id;
        WindowScopeId scope_id = WindowScopeId::root();
        std::vector<core::InternedId> selected_node_ids;
        char name_buf[128] = {};
        bool has_preview = false;
        editor::commands::ExtractToBlueprintPreview preview;
        std::string preview_error;
        std::string preview_name;
        bool allow_nonembedded_descendant_refs = false;
        bool preview_allow_nonembedded_descendant_refs = false;

        void reset() {
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
    } pendingExtract;

    struct ZNTuneState {
        bool show_result_popup = false;
        bool last_ok = false;
        bool last_was_preview = false;
        char pi_node[128] = "pi_1";
        char feedback_signal[128] = "bus_1.v";
        float cfg_dt_sec = 1.0f / 60.0f;
        float cfg_run_time_sec = 16.0f;
        float cfg_settle_time_sec = 3.0f;
        float cfg_kp_lo = 0.01f;
        float cfg_kp_hi = 80.0f;
        int cfg_max_expand = 10;
        int cfg_binary_iters = 14;
        int cfg_min_peaks = 4;
        float Ku = 0.0f;
        float Tu = 0.0f;
        float Kp = 0.0f;
        float Ki = 0.0f;
        char error[256] = {};
        ZNTuneConfig last_cfg{};
    } znTune;

    bool showInspector = true;
    bool showOscilloscope = true;
    bool showDebugLayoutBounds = false;
    bool showDebugPaintBounds = false;
    EditorSettings settings;
    OscilloscopeModel oscilloscope;

    struct InlineValueEditorState {
        bool open = false;
        std::optional<editor::DocumentId> document_id;
        core::InternedId node_id;
        WindowScopeId scope_id = WindowScopeId::root();
        std::string buffer;
        std::string error;
        Pt anchor_screen;
        bool has_anchor = false;

        /// Cached EditingHost for embedded scopes — created once when the
        /// dialog opens, reused every render frame. Null for root scope
        /// (root uses doc->root().host directly).
        std::unique_ptr<EditingHost> cached_host;

        void close() {
            open = false;
            document_id.reset();
            cached_host.reset();
        }
    } inlineValueEditor;

    // ── Utility ──

    /// Remove documents that were marked closed. Call at end of frame.
    void removeClosedDocuments();

    /// Open properties for a node in the active document
    void openPropertiesForNode(core::InternedId node_id, const WindowScopeId& scope_id, Document& doc);

    void openScriptEditorForNode(core::InternedId node_id, const WindowScopeId& scope_id, Document& doc);

    /// Open color picker for a node
    void openColorPickerForNode(core::InternedId node_id, const WindowScopeId& scope_id, Document& doc);

    /// Open inline value editor for a Value node
    void openInlineValueEditorForNode(core::InternedId node_id, const WindowScopeId& scope_id, Document& doc,
                                      const ui::Pt* anchor_screen = nullptr);

    /// Dispatch InputResultAction from a document to the window system
    void handleInputAction(const Document::InputResultAction& action, Document& doc);

    /// Reconcile all owner-bound transient UI against the current model state.
    /// Any UI whose owner document/node/scope no longer exists must self-close.
    void reconcile_owner_bound_ui();

private:
    std::vector<std::unique_ptr<Document>> documents_;
    Document* active_document_ = nullptr;
    Document* pending_tab_focus_ = nullptr;  ///< One-shot: set by create/open, consumed by tab bar
    ComponentRegistry type_registry_;
    bp2::LibraryIndex library_index_;
    Inspector inspector_;
    PropertiesWindow properties_window_;
    ScriptEditorWindow script_editor_window_;
    editor::RenderingResources rendering_resources_;
};
