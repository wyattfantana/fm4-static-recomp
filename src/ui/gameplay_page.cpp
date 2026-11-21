// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "ui/gameplay_page.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <atomic>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "narrator_mute.h"
#include "quick_race_laps.h"

REXCVAR_DEFINE_BOOL(fm4_hide_online_items, false, "Forza Motorsport 4",
                    "Leaves the menu items that need Xbox LIVE or the Forza servers off the "
                    "menus (COMMUNITY, MARKETPLACE, RIVALS, BUY TUNING SETUPS, BUY DESIGNS), from "
                    "the next time each menu is built");
#include "custom_decals.h"
#include "quick_race_laps.h"

REXCVAR_DEFINE_STRING(fm4_custom_decals_folder, "", "Forza Motorsport 4",
                      "CUSTOM DECALS: a folder whose PNG images become decals of the livery editor, on "
                      "every car (taken the next time the paint editor opens); empty for none");
REXCVAR_DEFINE_BOOL(fm4_all_sponsor_decals, false, "Forza Motorsport 4",
                    "ALL SPONSOR DECALS: every decal the game has is offered on every car, not only "
                    "the car maker's usual ones (taken the next time the paint editor opens)");
REXCVAR_DEFINE_STRING(fm4_fast_post_race, "off", "Forza Motorsport 4",
                      "FAST POST-RACE: off; globe (the World Tour globe's spin and zoom to the "
                      "next event play fast); all (also the post-race XP bars fill fast)");

namespace fm4_static_recomp::ui {
namespace {

// fm4_fast_post_race as the guest's threads read it: the cvar is a string the UI thread sets.
std::atomic<int> g_fast_post_race_mode{0};  // 0 off, 1 globe, 2 all

void SetFastPostRaceMode(std::string_view mode) {
  g_fast_post_race_mode.store(mode == "all" ? 2 : mode == "globe" ? 1 : 0, std::memory_order_relaxed);
}

using hostui::MenuRow;
using hostui::MenuScreen;

constexpr const char* kLaps = "fm4_quick_race_laps";
constexpr const char* kQuickRestart = "fm4_quick_restart";
constexpr const char* kRestartTrigger = "fm4_quick_restart_trigger";

// The pad buttons QUICK RESTART can use, as fm4_quick_restart names them, and as shown: the
// D-pad's left and right, which every controller layout leaves unused while driving, so the
// button does nothing else.
constexpr std::array<std::pair<std::string_view, std::string_view>, 2> kButtons = {{
    {"dpad_left", "D-PAD LEFT"},
    {"dpad_right", "D-PAD RIGHT"},
}};
constexpr std::string_view kDefaultButton = "dpad_left";

int ButtonIndex(std::string_view name) {
  for (size_t i = 0; i < kButtons.size(); ++i) {
    if (kButtons[i].first == name) return int(i);
  }
  return -1;
}
constexpr const char* kMuteNarrator = "fm4_mute_narrator";
constexpr const char* kHideOnlineItems = "fm4_hide_online_items";
constexpr const char* kDecalsFolder = "fm4_custom_decals_folder";
constexpr const char* kAllSponsors = "fm4_all_sponsor_decals";

std::mutex g_picker_mutex;
FolderPicker g_picker;

// A folder the picker chose, waiting for the page's next update (the picker answers on its own
// thread).
struct PickedFolder {
  std::mutex mutex;
  std::optional<std::string> folder;
  bool refused = false;  // the last folder chosen had '#' in its path (read only on the page's thread)
};
constexpr const char* kFastPostRace = "fm4_fast_post_race";

// FAST POST-RACE's values, in the order the row steps through them, and their labels.
constexpr std::pair<std::string_view, std::string_view> kFastPostRaceValues[] = {
    {"off", "OFF"}, {"globe", "GLOBE"}, {"all", "GLOBE + XP BARS"}};

// A saved count the game takes, or nullopt.
std::optional<int> ParseLaps(const std::string& value) {
  const int laps = std::atoi(value.c_str());
  if (std::to_string(laps) != value || laps < 0 || laps > kQuickRaceLapsMax) {
    return std::nullopt;
  }
  return laps;
}

bool FromCommandLine(const char* name) {
  return rex::cvar::GetFlagSource(name) == rex::cvar::Source::kCommandLine;
}

bool IsFastPostRaceValue(std::string_view value) {
  return std::any_of(std::begin(kFastPostRaceValues), std::end(kFastPostRaceValues),
                     [value](const auto& entry) { return entry.first == value; });
}

bool FromCommandLine(const char* name) {
  return rex::cvar::GetFlagSource(name) == rex::cvar::Source::kCommandLine;
}

std::string LaunchOptionBadge(const char* name) {
  return FromCommandLine(name) ? std::string("Set by a launch option for this session.") : std::string();
}

std::string CurrentValue(const char* name) {
  return rex::cvar::GetFlagByName(name);
}

// The page's settings: each drafted value (as fm4_* takes it) until SAVE & APPLY.
struct Draft {
  Draft(config::HostConfig& config, std::filesystem::path game_data_root)
      : config(config), game_data_root(std::move(game_data_root)) {}

  config::HostConfig& config;
  std::map<std::string, std::string> pending;  // cvar -> drafted value
  // The button QUICK RESTART goes back to when it is switched on again.
  std::string last_button;
  std::filesystem::path game_data_root;
  std::map<std::string, std::string> pending;  // cvar -> drafted value
  bool leave_warned = false;
  bool applied = false;
  bool save_failed = false;

  std::string Value(const char* name) const {
    const auto found = pending.find(name);
    return found != pending.end() ? found->second : CurrentValue(name);
  }

  void Change(const char* name, std::string value) {
    if (value == CurrentValue(name)) {
      pending.erase(name);
  void Change(int value) {
    if (value == Laps()) return;  // a press at the row's end
    if (value == QuickRaceLapsSetting()) {
      laps.reset();
    } else {
      pending[name] = std::move(value);
    }
    leave_warned = false;
    applied = false;
    save_failed = false;
  }

  int Laps() const { return std::atoi(Value(kLaps).c_str()); }

  std::string RestartButton() const {
    const std::string value = Value(kQuickRestart);
    if (ButtonIndex(value) >= 0) return value;
    if (ButtonIndex(last_button) >= 0) return last_button;
    return std::string(kDefaultButton);
  }
  bool RestartOn() const { return ButtonIndex(Value(kQuickRestart)) >= 0; }

  void SaveAndApply() {
    if (pending.empty()) {
      return;
    }
    for (const auto& [name, value] : pending) {
      if (name == kLaps) {
        if (value == "0") {
          config.Remove(kLaps);
        } else {
          config.Set(kLaps, value);
        }
      } else if (name == kQuickRestart) {
        config.Set(kQuickRestart, "\"" + value + "\"");
      } else if (name == kRestartTrigger) {
        config.Set(kRestartTrigger, "\"" + value + "\"");
      } else if (name == kMuteNarrator) {
        config.Set(kMuteNarrator, value);
      } else if (name == kHideOnlineItems) {
        if (value == "false") {  // the default: no line
          config.Remove(kHideOnlineItems);
        } else {
          config.Set(kHideOnlineItems, value);
        }
      } else if (name == kDecalsFolder) {
        if (value.empty()) {
          config.Remove(kDecalsFolder);
        } else {
          config.Set(kDecalsFolder, config::Quote(value));
        }
      } else if (name == kAllSponsors) {
        config.Set(kAllSponsors, value);
      } else if (name == kFastPostRace) {
        if (value == "off") {
          config.Remove(kFastPostRace);
        } else {
          config.Set(kFastPostRace, value);
        }
      }
    }
    if (!config.Save()) {
      REXLOG_ERROR("Gameplay page: could not write {}", config.path().string());
      save_failed = true;
      return;
    }
    for (const auto& [name, value] : pending) {
      rex::cvar::SetFlagByName(name, value);
      REXLOG_INFO("Gameplay page: {} = {}", name, value);
      if (name == kMuteNarrator && !game_data_root.empty()) {
        // The game loaded the narrator's bank as it started: it is changed where it is.
        RequestNarratorMute(game_data_root, value == "true");
      }
    bool decals_changed = false;
    for (const auto& [name, value] : pending) {
      rex::cvar::SetFlagByName(name, value);
      REXLOG_INFO("Gameplay page: {} = {}", name, value);
      decals_changed = decals_changed || name == kDecalsFolder || name == kAllSponsors;
    }
    if (decals_changed) {
      decals::SetSettings({REXCVAR_GET(fm4_custom_decals_folder), REXCVAR_GET(fm4_all_sponsor_decals)});
      decals::ApplyCustomDecals();
      if (name == kFastPostRace) SetFastPostRaceMode(value);
    }
    pending.clear();
    leave_warned = false;
    applied = true;
  }
};

std::string LapsLabel(int laps) { return laps ? std::to_string(laps) : std::string("DEFAULT"); }

MenuRow LapsRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "QUICK RACE LAPS";
  row.description =
      "Experimental: the number of laps of every Quick Race, on every track, up to 99. "
      "Default keeps each track's own count. Track select shows the number from the next "
      "time it opens. Long races are not tested yet: tyre wear, the AI and the results "
      "screen may not hold up.";
  row.value = [draft] { return LapsLabel(draft->Laps()); };
  row.adjust = [draft](int direction) {
    draft->Change(kLaps, std::to_string(std::clamp(draft->Laps() + direction, 0, kQuickRaceLapsMax)));
    draft->Change(std::clamp(draft->Laps() + std::clamp(direction, -1, 1), 0, kQuickRaceLapsMax));
  };
  row.badge = [] { return LaunchOptionBadge(kLaps); };
  row.unapplied = [draft] { return draft->pending.contains(kLaps); };
  return row;
}

MenuRow QuickRestartRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "QUICK RESTART";
  row.divider_above = true;
  row.description =
      "Press the QUICK RESTART BUTTON while racing (held for a moment, or one tap: see QUICK "
      "RESTART TRIGGER) to restart the race at once: the screen fades to black and comes back on "
      "the countdown, with no menus and a short start. Only while racing, never in menus or during "
      "a countdown.";
  row.value = [draft] { return std::string(draft->RestartOn() ? "ON" : "OFF"); };
  row.adjust = [draft](int) {
    if (draft->RestartOn()) {
      draft->last_button = draft->RestartButton();
      draft->Change(kQuickRestart, "off");
    } else {
      draft->Change(kQuickRestart, draft->RestartButton());
    }
  };
  row.badge = [] { return LaunchOptionBadge(kQuickRestart); };
  row.unapplied = [draft] { return draft->pending.contains(kQuickRestart); };
  return row;
}

MenuRow QuickRestartButtonRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "QUICK RESTART BUTTON";
  row.description =
      "The pad button for QUICK RESTART: the D-pad's left or right, which no controller layout "
      "uses while driving, so it does nothing else. An extra button on your pad mapped to one "
      "of them makes a dedicated restart button.";
  row.value = [draft] {
    return std::string(kButtons[size_t(std::max(ButtonIndex(draft->RestartButton()), 0))].second);
  };
  row.adjust = [draft](int direction) {
    const int count = int(kButtons.size());
    const int index = (ButtonIndex(draft->RestartButton()) + direction + count) % count;
    draft->last_button = std::string(kButtons[size_t(index)].first);
    if (draft->RestartOn()) draft->Change(kQuickRestart, draft->last_button);
  };
  row.enabled = [draft] { return draft->RestartOn(); };
  row.badge = [] { return LaunchOptionBadge(kQuickRestart); };
  row.unapplied = [draft] { return draft->pending.contains(kQuickRestart); };
  return row;
}

MenuRow RestartTriggerRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "QUICK RESTART TRIGGER";
  row.description =
      "HOLD: hold the button while the screen fades to black; let go during the fade to cancel. "
      "TAP: one press restarts, after a short fade, with no cancel.";
  row.value = [draft] { return std::string(draft->Value(kRestartTrigger) == "tap" ? "TAP" : "HOLD"); };
  row.adjust = [draft](int) {
    draft->Change(kRestartTrigger, draft->Value(kRestartTrigger) == "tap" ? "hold" : "tap");
  };
  row.enabled = [draft] { return draft->RestartOn(); };
  row.badge = [] { return LaunchOptionBadge(kRestartTrigger); };
  row.unapplied = [draft] { return draft->pending.contains(kRestartTrigger); };
MenuRow NarratorRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "WORLD TOUR NARRATOR";
  row.divider_above = true;
  row.description =
      "The voice that introduces World Tour events and tracks in the menus. MUTED silences "
      "it in every language; Jeremy Clarkson's Autovista commentary stays.";
  row.value = [draft] { return std::string(draft->Value(kMuteNarrator) == "true" ? "MUTED" : "ON"); };
  row.adjust = [draft](int) {
    draft->Change(kMuteNarrator, draft->Value(kMuteNarrator) == "true" ? "false" : "true");
  };
  row.badge = [] { return LaunchOptionBadge(kMuteNarrator); };
  row.unapplied = [draft] { return draft->pending.contains(kMuteNarrator); };
MenuRow OnlineItemsRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "ONLINE MENU ITEMS";
  row.divider_above = true;
  row.description =
      "COMMUNITY and MARKETPLACE on the main menu, RIVALS, BUY TUNING SETUPS and BUY DESIGNS "
      "need Xbox LIVE or the Forza servers, so here they only say the service is unavailable. "
      "HIDDEN leaves them off the menus. Each menu shows the change the next time the game "
      "builds it: the main menu after you've been into another screen, or at the next start.";
  row.value = [draft] {
    return std::string(draft->Value(kHideOnlineItems) == "true" ? "HIDDEN" : "SHOWN");
  };
  row.adjust = [draft](int) {
    draft->Change(kHideOnlineItems, draft->Value(kHideOnlineItems) == "true" ? "false" : "true");
  };
  row.badge = [] { return LaunchOptionBadge(kHideOnlineItems); };
  row.unapplied = [draft] { return draft->pending.contains(kHideOnlineItems); };
MenuRow Heading(const char* label) {
  MenuRow row;
  row.label = label;
  row.heading = true;
  row.divider_above = true;
  return row;
}

std::string FolderLabel(const std::string& folder) {
  if (folder.empty()) return "NONE";
  const std::string name = rex::path_to_utf8(rex::to_path(folder).filename());
  return name.empty() ? folder : name;
}

// What the decals of the applied settings came to. A problem that is a sentence (ends with '.')
// stands alone; the others name an image: "<file> (<why>)".
std::string DecalsNote() {
  const decals::Status status = decals::CurrentStatus();
  if (status.building) {
    return "Preparing your decals...";
  }
  std::string note;
  if (status.decals > 0) {
    note = std::to_string(status.decals) + (status.decals == 1 ? " custom decal" : " custom decals") + " ready.";
  } else if (status.problems.empty()) {
    note = "This folder has no PNG images yet.";
  }
  std::vector<std::string> images;
  for (const std::string& problem : status.problems) {
    if (problem.ends_with('.')) {
      note += (note.empty() ? "" : " ") + problem;
    } else {
      images.push_back(problem);
    }
  }
  for (size_t i = 0; i < images.size(); ++i) {
    note += (note.empty() ? "" : (i ? ", " : " ")) + std::string(i ? "" : "Couldn't use: ") + images[i];
  }
  if (!images.empty()) {
    note += ".";
  }
  return note;
}

MenuRow DecalsFolderRow(const std::shared_ptr<Draft>& draft, const std::shared_ptr<PickedFolder>& picked) {
  MenuRow row;
  row.label = "DECALS FOLDER";
  row.description =
      "Put your own logos and graphics on any car. Choose a folder of PNG images: each one becomes "
      "a decal, named after its file. In the livery editor, create a layer and choose APPLY A "
      "CUSTOM DECAL. Logos on a transparent background look best. Images you add or change later "
      "appear the next time you open the editor. Press A to choose a folder, or LEFT to stop using "
      "it.";
  row.value = [draft] { return FolderLabel(draft->Value(kDecalsFolder)); };
  row.adjust = [draft, picked](int direction) {
    if (direction < 0) {
      picked->refused = false;
      draft->Change(kDecalsFolder, "");
      return;
    }
    std::lock_guard lock(g_picker_mutex);
    if (!g_picker) return;
    g_picker(draft->Value(kDecalsFolder), [picked](std::optional<std::string> folder) {
      std::lock_guard picked_lock(picked->mutex);
      picked->folder = std::move(folder);
    });
  };
  row.note = [draft, picked] {
    if (picked->refused) {
      return std::string("This folder can't be used because its location contains a '#'. Rename the folder, "
                         "or choose a different one.");
    }
    const bool applied = !draft->pending.contains(kDecalsFolder);
    return applied && !draft->Value(kDecalsFolder).empty() ? DecalsNote() : std::string();
  };
  row.badge = [] { return LaunchOptionBadge(kDecalsFolder); };
  row.unapplied = [draft] { return draft->pending.contains(kDecalsFolder); };
  return row;
}

MenuRow AllSponsorsRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "ALL SPONSOR DECALS";
  row.description =
      "Each car normally offers its manufacturer's logo and a selection of sponsors. Turn this on "
      "to offer every sponsor decal in the game on every car, listed after the usual ones.";
  row.value = [draft] { return std::string(draft->Value(kAllSponsors) == "true" ? "ON" : "OFF"); };
  row.adjust = [draft](int) {
    draft->Change(kAllSponsors, draft->Value(kAllSponsors) == "true" ? "false" : "true");
  };
  row.badge = [] { return LaunchOptionBadge(kAllSponsors); };
  row.unapplied = [draft] { return draft->pending.contains(kAllSponsors); };
MenuRow FastPostRaceRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "FAST POST-RACE";
  row.divider_above = true;
  row.description =
      "Plays the waits after a race fast. GLOBE: the World Tour globe's spin and zoom to the "
      "next event, after a race and on opening World Tour, take a second or two. GLOBE + XP "
      "BARS: the post-race XP bars fill fast too. Level-up rewards, gift cars and every "
      "other screen stay as they are. After SAVE & APPLY it works from the next race, with no restart.";
  row.value = [draft] {
    const std::string value = draft->Value(kFastPostRace);
    for (const auto& [name, label] : kFastPostRaceValues) {
      if (name == value) return std::string(label);
    }
    return std::string("OFF");
  };
  row.adjust = [draft](int direction) {
    constexpr int count = int(std::size(kFastPostRaceValues));
    const std::string value = draft->Value(kFastPostRace);
    int index = 0;
    for (int i = 0; i < count; ++i) {
      if (kFastPostRaceValues[i].first == value) index = i;
    }
    index = std::clamp(index + direction, 0, count - 1);
    draft->Change(kFastPostRace, std::string(kFastPostRaceValues[index].first));
  };
  row.badge = [] { return LaunchOptionBadge(kFastPostRace); };
  row.unapplied = [draft] { return draft->pending.contains(kFastPostRace); };
  return row;
}

}  // namespace

std::unique_ptr<MenuScreen> CreateGameplayPage(config::HostConfig& config,
                                               const std::filesystem::path& game_data_root) {
  auto draft = std::make_shared<Draft>(config, game_data_root);
  std::vector<MenuRow> rows;
  rows.push_back(LapsRow(draft));
  rows.push_back(QuickRestartRow(draft));
  rows.push_back(QuickRestartButtonRow(draft));
  rows.push_back(RestartTriggerRow(draft));
  rows.push_back(NarratorRow(draft));
  rows.push_back(OnlineItemsRow(draft));
void SetFolderPicker(FolderPicker picker) {
  std::lock_guard lock(g_picker_mutex);
  g_picker = std::move(picker);
}

std::unique_ptr<MenuScreen> CreateGameplayPage(config::HostConfig& config) {
  auto draft = std::make_shared<Draft>(config);
  auto picked = std::make_shared<PickedFolder>();
  std::vector<MenuRow> rows;
  rows.push_back(LapsRow(draft));
  rows.push_back(Heading("CUSTOM DECALS"));
  rows.push_back(DecalsFolderRow(draft, picked));
  rows.push_back(AllSponsorsRow(draft));
  rows.push_back(FastPostRaceRow(draft));
  auto screen = std::make_unique<MenuScreen>("GAMEPLAY", std::move(rows));
  screen->set_apply([draft] { draft->SaveAndApply(); });
  screen->set_leave([draft] {
    if (draft->pending.empty() || draft->leave_warned) {
      return true;
    }
    draft->leave_warned = true;
    return false;
  });
  screen->set_status([draft] {
    if (draft->save_failed) {
      return hostui::MenuStatus{.text = "COULD NOT SAVE GAMEPLAY.TOML", .warning = true};
    }
    if (!draft->pending.empty()) {
      return hostui::MenuStatus{.text = "UNSAVED CHANGES", .warning = draft->leave_warned};
    }
    if (draft->applied) {
      return hostui::MenuStatus{.text = "SETTINGS SAVED AND APPLIED."};
    }
    return hostui::MenuStatus{};
  });
  screen->set_update([draft](bool closing) {
  screen->set_update([draft, picked](bool closing) {
    std::optional<std::string> folder;
    {
      std::lock_guard lock(picked->mutex);
      folder.swap(picked->folder);
    }
    if (folder && !folder->empty()) {
      // Kept with forward slashes (the settings file keeps no backslashes); '#' starts a comment there.
      std::string value = *folder;  // UTF-8
      std::replace(value.begin(), value.end(), '\\', '/');
      picked->refused = value.find('#') != std::string::npos;
      if (picked->refused) {
        REXLOG_WARN("Gameplay page: a decals folder with '#' in its path cannot be kept: {}", value);
      } else {
        draft->Change(kDecalsFolder, value);
      }
    }
    if (closing && !draft->pending.empty()) {
      REXLOG_INFO("Gameplay page: discarded an unapplied change");
    }
  });
  return screen;
}

int FastPostRaceMode() { return g_fast_post_race_mode.load(std::memory_order_relaxed); }

void ApplySavedGameplaySettings(const config::HostConfig& config) {
  if (const auto value = config.Get(kLaps)) {
    if (FromCommandLine(kLaps)) {
      REXLOG_INFO("Gameplay settings: {} comes from the command line, not {}", kLaps,
                  config.path().string());
    } else if (const auto laps = ParseLaps(*value);
               !laps || !rex::cvar::SetFlagByName(kLaps, std::to_string(*laps))) {
      REXLOG_WARN("Gameplay settings: ignored {} = {} in {}", kLaps, *value, config.path().string());
    } else {
      REXLOG_INFO("Gameplay settings: {} = {}", kLaps, *laps);
    }
  }
  if (const auto value = config.Get(kQuickRestart); value && !FromCommandLine(kQuickRestart)) {
    if (*value == "off" || ButtonIndex(*value) >= 0) {
      rex::cvar::SetFlagByName(kQuickRestart, *value);
      REXLOG_INFO("Gameplay settings: {} = {}", kQuickRestart, *value);
    } else {
      REXLOG_WARN("Gameplay settings: ignored {} = {}", kQuickRestart, *value);
    }
  }
  if (const auto value = config.Get(kRestartTrigger); value && !FromCommandLine(kRestartTrigger)) {
    rex::cvar::SetFlagByName(kRestartTrigger, *value == "tap" ? "tap" : "hold");
  if (const auto value = config.Get(kMuteNarrator); value && !FromCommandLine(kMuteNarrator)) {
    if (*value == "true" || *value == "false") {
      rex::cvar::SetFlagByName(kMuteNarrator, *value);
      REXLOG_INFO("Gameplay settings: {} = {}", kMuteNarrator, *value);
    } else {
      REXLOG_WARN("Gameplay settings: ignored {} = {}", kMuteNarrator, *value);
  if (const auto value = config.Get(kHideOnlineItems)) {
    if (FromCommandLine(kHideOnlineItems)) {
      REXLOG_INFO("Gameplay settings: {} comes from the command line, not {}", kHideOnlineItems,
                  config.path().string());
    } else if ((*value != "true" && *value != "false") || !rex::cvar::SetFlagByName(kHideOnlineItems, *value)) {
      REXLOG_WARN("Gameplay settings: ignored {} = {} in {}", kHideOnlineItems, *value, config.path().string());
    } else {
      REXLOG_INFO("Gameplay settings: {} = {}", kHideOnlineItems, *value);
  if (const auto value = config.Get(kDecalsFolder); value && !FromCommandLine(kDecalsFolder)) {
    rex::cvar::SetFlagByName(kDecalsFolder, *value);
    REXLOG_INFO("Gameplay settings: {} = {}", kDecalsFolder, *value);
  }
  if (const auto value = config.Get(kAllSponsors); value && !FromCommandLine(kAllSponsors)) {
    if (*value == "true" || *value == "false") {
      rex::cvar::SetFlagByName(kAllSponsors, *value);
      REXLOG_INFO("Gameplay settings: {} = {}", kAllSponsors, *value);
    } else {
      REXLOG_WARN("Gameplay settings: ignored {} = {}", kAllSponsors, *value);
  if (const auto value = config.Get(kFastPostRace); value && !FromCommandLine(kFastPostRace)) {
    if (IsFastPostRaceValue(*value) && rex::cvar::SetFlagByName(kFastPostRace, *value)) {
      REXLOG_INFO("Gameplay settings: {} = {}", kFastPostRace, *value);
    } else {
      REXLOG_WARN("Gameplay settings: ignored {} = {}", kFastPostRace, *value);
    }
  }
  const std::string fast_post_race = REXCVAR_GET(fm4_fast_post_race);
  if (!IsFastPostRaceValue(fast_post_race)) {
    REXLOG_WARN("Gameplay settings: {} = {} is not one of off, globe, all: it stays off", kFastPostRace,
                fast_post_race);
  }
  SetFastPostRaceMode(fast_post_race);
}

}  // namespace fm4_static_recomp::ui
