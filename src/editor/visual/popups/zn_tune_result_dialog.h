#pragma once

#include "editor/window_system.h"
#include "editor/pi_zn_tuner.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>

class ZnTuneResultDialog {
public:
    void render(WindowSystem& ws) {
        if (ws.znTune.show_result_popup) {
            ImGui::OpenPopup("ZN PI Tune Result");
            ws.znTune.show_result_popup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("ZN PI Tune Result", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        if (ws.znTune.last_ok) {
            ImGui::Text("Ku: %.6f", ws.znTune.Ku);
            ImGui::Text("Tu: %.6f s", ws.znTune.Tu);
            ImGui::Separator();
            ImGui::Text("PI tuned:");
            ImGui::Text("Kp: %.6f", ws.znTune.Kp);
            ImGui::Text("Ki: %.6f", ws.znTune.Ki);
        } else {
            ImGui::TextWrapped("ZN tune failed: %s", ws.znTune.error);
            ImGui::Spacing();
            ImGui::TextDisabled("Try: increase run time, lower settle time, or start with higher Kp range.");
        }

        if (ws.znTune.last_ok && ws.znTune.last_was_preview) {
            ImGui::Separator();
            if (ImGui::Button("Apply")) {
                if (Document* doc = ws.activeDocument()) {
                    std::string err;
                    bool ok = apply_pi_params(*doc, ws.znTune.last_cfg.pi_node,
                                              ws.znTune.Kp, ws.znTune.Ki, &err);
                    if (!ok) {
                        std::memset(ws.znTune.error, 0, sizeof(ws.znTune.error));
                        std::strncpy(ws.znTune.error, err.c_str(), sizeof(ws.znTune.error) - 1);
                        ws.znTune.last_ok = false;
                    } else {
                        ws.znTune.last_was_preview = false;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply + Restart Sim")) {
                if (Document* doc = ws.activeDocument()) {
                    std::string err;
                    bool ok = apply_pi_params(*doc, ws.znTune.last_cfg.pi_node,
                                              ws.znTune.Kp, ws.znTune.Ki, &err);
                    if (!ok) {
                        std::memset(ws.znTune.error, 0, sizeof(ws.znTune.error));
                        std::strncpy(ws.znTune.error, err.c_str(), sizeof(ws.znTune.error) - 1);
                        ws.znTune.last_ok = false;
                    } else {
                        doc->stopSimulation();
                        doc->startSimulation();
                        ws.znTune.last_was_preview = false;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy values")) {
                char buf[256];
                std::snprintf(buf, sizeof(buf), "Ku=%.6f Tu=%.6f Kp=%.6f Ki=%.6f",
                              ws.znTune.Ku, ws.znTune.Tu, ws.znTune.Kp, ws.znTune.Ki);
                ImGui::SetClipboardText(buf);
            }
            ImGui::SameLine();
            if (ImGui::Button("OK")) {
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::Separator();
            if (ImGui::Button("OK")) {
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
};
