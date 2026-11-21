// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
// How PC OPTIONS lays out its game settings. Nothing here shows in the game but the layout chosen: to try another,
// change kActiveSettingsLayout; to add one, add an entry to SettingsLayouts().
#pragma once

#include <array>
#include <vector>

namespace fm4_static_recomp::ui {

enum class SettingRow {
  kQuickRaceLaps,
  kQuickRestart,
  kQuickRestartButton,
  kQuickRestartTrigger,
  kNarrator,
  kOnlineItems,
  kFastPostRace,
  kRacePayouts,
  kRepairCosts,
  kResaleValue,
  kDecalsFolder,
  kAllSponsors,
  kTrackSignage,
  kModMenu,
};

struct SettingsSection {
  const char* heading;  // nullptr: no heading
  std::vector<SettingRow> rows;
};
struct SettingsTab {
  const char* title;
  std::vector<SettingsSection> sections;
};
// `mod` with no sections: no MOD MENU tab.
struct SettingsLayout {
  SettingsTab game;
  SettingsTab mod;
};

inline constexpr size_t kActiveSettingsLayout = 2;

inline const std::array<SettingsLayout, 3>& SettingsLayouts() {
  using enum SettingRow;
  static const std::array<SettingsLayout, 3> layouts = {{
      // 0. GAME kept to the game's own; all of our features in MODS, grouped by what they change.
      {{"GAME",
        {{"RACING", {kQuickRaceLaps}},
         {"MODS", {kModMenu}}}},
       {"MODS",
        {{"RACING", {kQuickRestart, kQuickRestartButton, kQuickRestartTrigger}},
         {"HARDCORE", {kRacePayouts, kRepairCosts, kResaleValue}},
         {"MENUS/UI", {kFastPostRace, kOnlineItems, kNarrator}},
         {"LIVERY", {kDecalsFolder, kAllSponsors}},
         {"TRACKS", {kTrackSignage}}}}},
      // 1. Quick restart stays with the everyday race settings; the rest in MODS.
      {{"GAME",
        {{"RACING", {kQuickRaceLaps, kQuickRestart, kQuickRestartButton, kQuickRestartTrigger}},
         {"MODS", {kModMenu}}}},
       {"MODS",
        {{"HARDCORE", {kRacePayouts, kRepairCosts, kResaleValue}},
         {"MENUS/UI", {kFastPostRace, kOnlineItems, kNarrator}},
         {"PAINT", {kDecalsFolder, kAllSponsors}},
         {"TRACKS", {kTrackSignage}}}}},
      // 2. MODS named after the game's own menus, so each is where a player expects it.
      {{"GAME",
        {{"RACING", {kQuickRaceLaps}},
         {"MODS", {kModMenu}}}},
       {"MODS",
        {{"GO RACE", {kQuickRestart, kQuickRestartButton, kQuickRestartTrigger}},
         {"CAREER", {kRacePayouts, kRepairCosts, kResaleValue}},
         {"MENUS/UI", {kFastPostRace, kOnlineItems, kNarrator}},
         {"PAINT", {kDecalsFolder, kAllSponsors}},
         {"TRACKS", {kTrackSignage}}}}},
  }};
  return layouts;
}

inline const SettingsLayout& ActiveSettingsLayout() { return SettingsLayouts()[kActiveSettingsLayout]; }

// Every row once, in one list (the unit tests).
inline SettingsTab EverySettingOnOnePage() {
  using enum SettingRow;
  return {"GAME",
          {{nullptr,
            {kQuickRaceLaps, kQuickRestart, kQuickRestartButton, kQuickRestartTrigger, kNarrator, kOnlineItems,
             kFastPostRace, kRacePayouts, kRepairCosts, kResaleValue, kDecalsFolder, kAllSponsors, kTrackSignage,
             kModMenu}}}};
}

}  // namespace fm4_static_recomp::ui
