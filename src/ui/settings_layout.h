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

inline constexpr size_t kActiveSettingsLayout = 0;

inline const std::array<SettingsLayout, 3>& SettingsLayouts() {
  using enum SettingRow;
  static const std::array<SettingsLayout, 3> layouts = {{
      // 0. GAME for the everyday settings; the mod features behind a MOD MENU switch at its bottom.
      {{"GAME",
        {{"RACING", {kQuickRaceLaps, kQuickRestart, kQuickRestartButton, kQuickRestartTrigger}},
         {"MENUS", {kOnlineItems, kNarrator}},
         {"MODS", {kModMenu}}}},
       {"MOD MENU",
        {{"HARDCORE", {kRacePayouts, kRepairCosts, kResaleValue}},
         {"AFTER THE RACE", {kFastPostRace}},
         {"LIVERY", {kDecalsFolder, kAllSponsors}},
         {"TRACKS", {kTrackSignage}}}}},
      // 1. Everything on one tab, under headings.
      {{"GAME",
        {{"RACING", {kQuickRaceLaps, kQuickRestart, kQuickRestartButton, kQuickRestartTrigger}},
         {"HARDCORE", {kRacePayouts, kRepairCosts, kResaleValue}},
         {"MENUS", {kOnlineItems, kNarrator, kFastPostRace}},
         {"LIVERY", {kDecalsFolder, kAllSponsors}},
         {"TRACKS", {kTrackSignage}}}},
       {"", {}}},
      // 2. Two tabs by what they touch: racing, and the garage and menus.
      {{"RACING",
        {{nullptr, {kQuickRaceLaps, kQuickRestart, kQuickRestartButton, kQuickRestartTrigger}},
         {"HARDCORE", {kRacePayouts, kRepairCosts, kResaleValue}},
         {"AFTER THE RACE", {kFastPostRace}}}},
       {"GARAGE",
        {{"LIVERY", {kDecalsFolder, kAllSponsors}},
         {"TRACKS", {kTrackSignage}},
         {"MENUS", {kOnlineItems, kNarrator}}}}},
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
