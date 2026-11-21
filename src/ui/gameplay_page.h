// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config/host_config.h"
#include "ui/hostui/menu.h"

namespace fm4_static_recomp::ui {

// The tabs after MOUSE, kept in gameplay.toml, as ui/settings_layout.h lays them out: GAME, and MOD MENU once its
// switch at the bottom of GAME is on. SAVE & APPLY (Y) writes a page's settings and sets them at once.
struct SettingsPageInfo {
  std::string title;
  bool mod = false;
};
std::vector<SettingsPageInfo> SettingsPages();
std::unique_ptr<hostui::MenuScreen> CreateSettingsPage(bool mod, config::HostConfig& config,
                                                       const std::filesystem::path& game_data_root = {});
// Called after SAVE & APPLY changes which tabs there are.
void SetSettingsTabsChanged(std::function<void()> changed);

// Every setting on one page, whatever the layout (the unit tests).
std::unique_ptr<hostui::MenuScreen> CreateGameplayPage(config::HostConfig& config,
                                                       const std::filesystem::path& game_data_root = {});

// Opens a folder chooser starting at `current` and calls `done` with the folder (nullopt when
// cancelled), from any thread. The app sets it (a native dialog); without one the row does nothing.
using FolderPicker = std::function<void(const std::string& current,
                                        std::function<void(std::optional<std::string>)> done)>;
void SetFolderPicker(FolderPicker picker);

// Sets the saved values at startup, below a launch option for the same setting.
void ApplySavedGameplaySettings(const config::HostConfig& config);

// FAST POST-RACE as set now, safe from any thread: 0 off, 1 globe, 2 all.
int FastPostRaceMode();

}  // namespace fm4_static_recomp::ui
