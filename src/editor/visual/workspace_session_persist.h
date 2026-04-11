#pragma once

#include "workspace_session.h"
#include <optional>
#include <string>

/// Save workspace/session state to a separate .workspace.json file.
/// Input path should be "filename.blueprint", output will be "filename.workspace.json".
/// Returns true on success.
[[nodiscard]] bool save_workspace_session(
    const WorkspaceSession& ws,
    const char* blueprint_path);

/// Load workspace/session state from a .workspace.json file.
/// Input path should be "filename.blueprint", will look for "filename.workspace.json".
/// Returns empty optional if file doesn't exist or is invalid.
[[nodiscard]] std::optional<WorkspaceSession> load_workspace_session(
    const char* blueprint_path);
