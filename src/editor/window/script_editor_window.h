#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "editor/identity.h"
#include "input/editing_host.h"
#include "window/window_callback.h"
#include <optional>
#include <string>

class ScriptEditorWindow {
public:
    void open(const bp2::Blueprint::Node& node, core::InternedId node_id,
              std::unique_ptr<EditingHost> owned_host, core::StringInterner& interner,
              WindowNodeCallback on_apply);

    void close();
    bool is_open() const { return open_; }

    const std::optional<editor::DocumentId>& owner_document_id() const { return owner_document_id_; }
    void set_owner_document_id(editor::DocumentId id) { owner_document_id_ = std::move(id); }
    void clear_owner_document_id() { owner_document_id_.reset(); }

    bool owns_document(const editor::DocumentId& id) const;
    bool still_valid(class WindowSystem& ws) const;

    void apply();
    void render();

    const std::string& pending_script() const { return pending_script_; }
    void set_pending_script(const std::string& s) { pending_script_ = s; }

private:
    bool open_ = false;
    std::unique_ptr<EditingHost> owned_host_;
    core::StringInterner* interner_ = nullptr;
    core::InternedId target_node_id_;
    std::optional<editor::DocumentId> owner_document_id_;
    WindowNodeCallback on_apply_;

    std::string pending_script_;
    std::string snapshot_script_;

    bool validation_checked_ = false;
    bool validation_ok_ = false;
    std::string validation_error_;

    const bp2::Blueprint::Node* resolve_target() const;
    void cancel_and_close();
};
