#include "script_editor_window.h"
#include "editor/window_system.h"
#include "core/solvers/jit/components/lua_script_validate.h"
#include "blueprint_v2/blueprint/blueprint_replace.h"

#ifndef EDITOR_TESTING
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#endif

void ScriptEditorWindow::open(const bp2::Blueprint::Node& node,
                               core::InternedId node_id,
                               std::unique_ptr<EditingHost> owned_host,
                               core::StringInterner& interner,
                               WindowNodeCallback on_apply) {
    if (!owned_host) {
        open_ = false;
        owned_host_.reset();
        return;
    }

    owned_host_ = std::move(owned_host);
    target_node_id_ = node_id;
    owner_document_id_.reset();
    interner_ = &interner;
    on_apply_ = std::move(on_apply);

    auto it = node.semantic.string_params.find("script");
    snapshot_script_ = (it != node.semantic.string_params.end()) ? it->second : "";
    pending_script_ = snapshot_script_;

    validation_checked_ = false;
    validation_ok_ = false;
    validation_error_.clear();

    open_ = true;
}

const bp2::Blueprint::Node* ScriptEditorWindow::resolve_target() const {
    if (!owned_host_ || target_node_id_.empty()) return nullptr;
    return owned_host_->find_node(target_node_id_);
}

void ScriptEditorWindow::close() {
    cancel_and_close();
}

bool ScriptEditorWindow::owns_document(const editor::DocumentId& id) const {
    return owner_document_id_.has_value() && *owner_document_id_ == id;
}

bool ScriptEditorWindow::still_valid(WindowSystem& ws) const {
#ifndef EDITOR_TESTING
    if (!owner_document_id_.has_value()) return true;
    return ws.findDocumentById(*owner_document_id_) != nullptr;
#else
    (void)ws;
    return true;
#endif
}

void ScriptEditorWindow::cancel_and_close() {
    open_ = false;
    owner_document_id_.reset();
}

void ScriptEditorWindow::apply() {
    const bp2::Blueprint::Node* target = resolve_target();
    if (!target || !owned_host_ || !interner_) {
        open_ = false;
        owner_document_id_.reset();
        return;
    }

    if (pending_script_ != snapshot_script_) {
        bp2::Blueprint::Node updated = *target;
        updated.semantic.string_params["script"] = pending_script_;

        bp2::Blueprint next_bp = bp2::replace_node_preserve_order(owned_host_->current_blueprint(), std::move(updated));
        owned_host_->push_checkpoint();
        owned_host_->replace_current(std::move(next_bp));
    }

    if (on_apply_) {
        on_apply_(target_node_id_);
    }

    open_ = false;
    owner_document_id_.reset();
}

void ScriptEditorWindow::render() {
    if (!open_) return;

    const bp2::Blueprint::Node* target = resolve_target();
    if (!target) {
        cancel_and_close();
        return;
    }

#ifndef EDITOR_TESTING
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    bool window_open = true;
    std::string node_id_label(interner_->resolve(target_node_id_));
    if (ImGui::Begin(("Script Editor: " + node_id_label).c_str(), &window_open)) {
        ImGui::Text("Lua Script — %s", node_id_label.c_str());
        ImGui::Separator();

        ImGui::InputTextMultiline("##script", &pending_script_,
            ImVec2(-1, -(ImGui::GetFrameHeightWithSpacing() * 3.0f)),
            ImGuiInputTextFlags_AllowTabInput);

        ImGui::Separator();

        if (ImGui::Button("Check Errors")) {
            auto result = lua_validate_script(pending_script_);
            validation_checked_ = true;
            if (result.has_value()) {
                validation_ok_ = false;
                validation_error_ = std::move(*result);
            } else {
                validation_ok_ = true;
                validation_error_.clear();
            }
        }

        ImGui::SameLine();

        if (validation_checked_) {
            if (validation_ok_) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "OK");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", validation_error_.c_str());
            }
        }

        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            apply();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            cancel_and_close();
        }
    }
    ImGui::End();

    if (!window_open) {
        cancel_and_close();
    }
#endif
}
