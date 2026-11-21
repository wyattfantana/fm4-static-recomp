// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "ui/gameplay_page.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "custom_decals.h"
#include "hardcore.h"
#include "narrator_mute.h"
#include "quick_race_laps.h"
#include "ui/settings_layout.h"
#include "ui/signage_page.h"

REXCVAR_DEFINE_BOOL(fm4_hide_online_items, false, "Forza Motorsport 4",
                    "Leaves the menu items that need Xbox LIVE or the Forza servers off the "
                    "menus (COMMUNITY, MARKETPLACE, RIVALS, BUY TUNING SETUPS, BUY DESIGNS), from "
                    "the next time each menu is built");
REXCVAR_DEFINE_STRING(fm4_custom_decals_folder, "", "Forza Motorsport 4",
                      "CUSTOM DECALS: a folder whose PNG images become decals of the livery editor, on "
                      "every car (taken the next time the paint editor opens); empty for none");
REXCVAR_DEFINE_BOOL(fm4_all_sponsor_decals, false, "Forza Motorsport 4",
                    "ALL SPONSOR DECALS: every decal the game has is offered on every car, not only "
                    "the car maker's usual ones (taken the next time the paint editor opens)");
REXCVAR_DEFINE_STRING(fm4_fast_post_race, "off", "Forza Motorsport 4",
                      "FAST POST-RACE: off; globe (the World Tour globe's spin and zoom to the "
                      "next event play fast); all (also the post-race XP bars fill fast)");
REXCVAR_DEFINE_BOOL(fm4_mod_menu, false, "Forza Motorsport 4",
                    "MOD MENU: shows the MOD MENU tab of PC OPTIONS, next to GAME. Hiding it leaves "
                    "the settings in it as they are");

namespace fm4_static_recomp::ui {
namespace {

using hostui::MenuRow;
using hostui::MenuScreen;

// fm4_fast_post_race as the guest's threads read it: the cvar is a string the UI thread sets.
std::atomic<int> g_fast_post_race_mode{0};  // 0 off, 1 globe, 2 all

void SetFastPostRaceMode(std::string_view mode) {
  g_fast_post_race_mode.store(mode == "all" ? 2 : mode == "globe" ? 1 : 0, std::memory_order_relaxed);
}

std::mutex g_tabs_changed_mutex;
std::function<void()> g_tabs_changed;

constexpr const char* kLaps = "fm4_quick_race_laps";
constexpr const char* kQuickRestart = "fm4_quick_restart";
constexpr const char* kRestartTrigger = "fm4_quick_restart_trigger";
constexpr const char* kAutoRestart = "fm4_auto_restart_dirty_lap";
constexpr const char* kMuteNarrator = "fm4_mute_narrator";
constexpr const char* kHideOnlineItems = "fm4_hide_online_items";
constexpr const char* kDecalsFolder = "fm4_custom_decals_folder";
constexpr const char* kAllSponsors = "fm4_all_sponsor_decals";
constexpr const char* kFastPostRace = "fm4_fast_post_race";
constexpr const char* kModMenu = "fm4_mod_menu";

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

// AUTO RESTART ON DIRTY LAP's settings, as fm4_auto_restart_dirty_lap names them, and as shown.
constexpr std::array<std::pair<std::string_view, std::string_view>, 4> kAutoRestarts = {{
    {"off", "OFF"},
    {"hot_lap", "HOT LAP"},
    {"races", "RACES"},
    {"all", "HOT LAP + RACES"},
}};

int AutoRestartIndex(std::string_view name) {
  for (size_t i = 0; i < kAutoRestarts.size(); ++i) {
    if (kAutoRestarts[i].first == name) return int(i);
  }
  return 0;
}

std::mutex g_picker_mutex;
FolderPicker g_picker;

// A folder the picker chose, waiting for the page's next update (the picker answers on its own
// thread).
struct PickedFolder {
  std::mutex mutex;
  std::optional<std::string> folder;
  bool refused = false;  // the last folder chosen had '#' in its path (read only on the page's thread)
};

// FAST POST-RACE's values, in the order the row steps through them, and their labels.
constexpr std::pair<std::string_view, std::string_view> kFastPostRaceValues[] = {
    {"off", "OFF"}, {"globe", "GLOBE"}, {"all", "GLOBE + XP BARS"}};

// The HARDCORE settings (src/hardcore.h), each a few choices, the first the default.
struct Choice {
  const char* value;  // as fm4_* takes it
  const char* label;
};
struct HardcoreSetting {
  const char* name;
  const char* label;
  const char* description;
  std::vector<Choice> choices;
};

const std::vector<HardcoreSetting>& HardcoreSettings() {
  static const std::vector<HardcoreSetting> settings = {
      {"fm4_race_payouts", "RACE PAYOUTS",
       "The credits a race pays by distance, and the bonuses for each car maker's affinity "
       "level. -25% and -50% pay that much less than the default (a mod's own included). From "
       "the next start.",
       {{"100", "DEFAULT"}, {"75", "-25%"}, {"50", "-50%"}}},
      {"fm4_repair_costs", "REPAIR COSTS",
       "What damage costs you from a race's winnings. +100% and +200% make each damaged part "
       "cost that much more than the default, a part at most the whole winnings. From the "
       "next start.",
       {{"100", "DEFAULT"}, {"200", "+100%"}, {"300", "+200%"}}},
      {"fm4_resale_value", "RESALE VALUE",
       "What selling a car pays. -25% and -50% pay that much less than the default (a mod's "
       "own included). From the next start.",
       {{"100", "DEFAULT"}, {"75", "-25%"}, {"50", "-50%"}}},
  };
  return settings;
}

int ChoiceIndex(const HardcoreSetting& setting, std::string_view value) {
  for (size_t i = 0; i < setting.choices.size(); ++i) {
    if (setting.choices[i].value == value) return int(i);
  }
  return -1;
}

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

std::string LaunchOptionBadge(const char* name) {
  return FromCommandLine(name) ? std::string("Set by a launch option for this session.") : std::string();
}

std::string CurrentValue(const char* name) {
  return rex::cvar::GetFlagByName(name);
}

// A page's settings: each drafted value (as fm4_* takes it) until SAVE & APPLY.
struct Draft {
  Draft(config::HostConfig& config, std::filesystem::path game_data_root)
      : config(config), game_data_root(std::move(game_data_root)) {}

  config::HostConfig& config;
  std::filesystem::path game_data_root;
  MenuScreen* screen = nullptr;
  std::map<std::string, std::string> pending;  // cvar -> drafted value
  // The button QUICK RESTART goes back to when it is switched on again.
  std::string last_button;
  bool leave_warned = false;
  bool applied = false;
  bool save_failed = false;

  std::string Value(const char* name) const {
    const auto found = pending.find(name);
    return found != pending.end() ? found->second : CurrentValue(name);
  }

  void Change(const char* name, std::string value) {
    if (value == Value(name)) return;  // a press at a row's end: the page's state stands
    if (value == CurrentValue(name)) {
      pending.erase(name);
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

  void Store(const std::string& name, const std::string& value) {
    // A setting at its default is no line in the file.
    const bool is_default = name == kLaps                                  ? value == "0"
                            : name == kQuickRestart || name == kFastPostRace || name == kAutoRestart ? value == "off"
                            : name == kRestartTrigger                       ? value == "hold"
                            : name == kDecalsFolder                         ? value.empty()
                            : name == kHideOnlineItems || name == kMuteNarrator || name == kAllSponsors ||
                                    name == kModMenu
                                ? value == "false"
                                : value == "100";  // a HARDCORE setting
    if (is_default) {
      config.Remove(name);
    } else if (name == kQuickRestart || name == kRestartTrigger || name == kAutoRestart) {
      config.Set(name, "\"" + value + "\"");
    } else if (name == kDecalsFolder) {
      config.Set(name, config::Quote(value));
    } else {
      config.Set(name, value);
    }
  }

  void SaveAndApply() {
    if (pending.empty()) {
      return;
    }
    for (const auto& [name, value] : pending) Store(name, value);
    if (!config.Save()) {
      REXLOG_ERROR("Gameplay page: could not write {}", config.path().string());
      save_failed = true;
      return;
    }
    bool decals_changed = false, tabs_changed = false;
    for (const auto& [name, value] : pending) {
      rex::cvar::SetFlagByName(name, value);
      REXLOG_INFO("Gameplay page: {} = {}", name, value);
      if (name == kMuteNarrator && !game_data_root.empty()) {
        // The game loaded the narrator's bank as it started: it is changed where it is.
        RequestNarratorMute(game_data_root, value == "true");
      }
      if (name == kFastPostRace) SetFastPostRaceMode(value);
      decals_changed = decals_changed || name == kDecalsFolder || name == kAllSponsors;
      tabs_changed = tabs_changed || name == kModMenu;
    }
    if (decals_changed) {
      decals::SetSettings({REXCVAR_GET(fm4_custom_decals_folder), REXCVAR_GET(fm4_all_sponsor_decals)});
      decals::ApplyCustomDecals();
    }
    pending.clear();
    leave_warned = false;
    applied = true;
    if (tabs_changed) {
      std::function<void()> changed;
      {
        std::lock_guard lock(g_tabs_changed_mutex);
        changed = g_tabs_changed;
      }
      if (changed) changed();
    }
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
    draft->Change(kLaps, std::to_string(std::clamp(draft->Laps() + std::clamp(direction, -1, 1), 0, kQuickRaceLapsMax)));
  };
  row.badge = [] { return LaunchOptionBadge(kLaps); };
  row.unapplied = [draft] { return draft->pending.contains(kLaps); };
  return row;
}

MenuRow QuickRestartRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "QUICK RESTART";
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
    const int index = hostui::StepIndex(ButtonIndex(draft->RestartButton()), int(kButtons.size()), direction);
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
  return row;
}

MenuRow NarratorRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "WORLD TOUR NARRATOR";
  row.description =
      "The voice that introduces World Tour events and tracks in the menus. MUTED silences "
      "it in every language; Jeremy Clarkson's Autovista commentary stays.";
  row.value = [draft] { return std::string(draft->Value(kMuteNarrator) == "true" ? "MUTED" : "ON"); };
  row.adjust = [draft](int) {
    draft->Change(kMuteNarrator, draft->Value(kMuteNarrator) == "true" ? "false" : "true");
  };
  row.badge = [] { return LaunchOptionBadge(kMuteNarrator); };
  row.unapplied = [draft] { return draft->pending.contains(kMuteNarrator); };
  return row;
}

MenuRow OnlineItemsRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "ONLINE MENU ITEMS";
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
  return row;
}

MenuRow FastPostRaceRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "FAST POST-RACE";
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
    index = hostui::StepIndex(index, count, direction);
    draft->Change(kFastPostRace, std::string(kFastPostRaceValues[index].first));
  };
  row.badge = [] { return LaunchOptionBadge(kFastPostRace); };
  row.unapplied = [draft] { return draft->pending.contains(kFastPostRace); };
  return row;
}

MenuRow HardcoreRow(const std::shared_ptr<Draft>& draft, const HardcoreSetting& setting) {
  MenuRow row;
  row.label = setting.label;
  row.description = setting.description;
  row.value = [draft, &setting] {
    const int index = ChoiceIndex(setting, draft->Value(setting.name));
    return std::string(setting.choices[size_t(std::max(index, 0))].label);
  };
  row.adjust = [draft, &setting](int direction) {
    const int index = hostui::StepIndex(std::max(ChoiceIndex(setting, draft->Value(setting.name)), 0),
                                        int(setting.choices.size()), direction);
    draft->Change(setting.name, setting.choices[size_t(index)].value);
  };
  // The data was read as the game started.
  row.note = [draft, &setting] {
    // A launch option wins at every start; a value out of range counts as the default.
    if (FromCommandLine(setting.name)) return std::string();
    const int now = std::max(ChoiceIndex(setting, draft->Value(setting.name)), 0);
    const int at_start = std::max(ChoiceIndex(setting, HardcoreSettingAtStart(setting.name)), 0);
    return std::string(now != at_start ? "From the next start." : "");
  };
  row.badge = [&setting] { return LaunchOptionBadge(setting.name); };
  row.unapplied = [draft, &setting] { return draft->pending.contains(setting.name); };
  return row;
}

MenuRow AutoRestartRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "AUTO RESTART ON DIRTY LAP";
  row.description =
      "A lap the game marks dirty (off the track, into a wall or contact: the warning by the lap "
      "time) restarts at once, as QUICK RESTART does. HOT LAP: alone on track. RACES: in races "
      "against AI cars, in Free Play and the career; the whole race restarts. Needs QUICK RESTART on.";
  row.value = [draft] {
    return std::string(kAutoRestarts[size_t(AutoRestartIndex(draft->Value(kAutoRestart)))].second);
  };
  row.adjust = [draft](int direction) {
    const int index =
        hostui::StepIndex(AutoRestartIndex(draft->Value(kAutoRestart)), int(kAutoRestarts.size()), direction);
    draft->Change(kAutoRestart, std::string(kAutoRestarts[size_t(index)].first));
  };
  row.enabled = [draft] { return draft->RestartOn(); };
  row.badge = [] { return LaunchOptionBadge(kAutoRestart); };
  row.unapplied = [draft] { return draft->pending.contains(kAutoRestart); };
  return row;
}

MenuRow SignageRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "TRACK SIGNAGE";
  row.description =
      "Your own pictures on the track's boards: choose a track, then a picture for each of its boards.";
  row.value = [] { return std::string("OPEN"); };
  row.adjust = [draft](int direction) {
    if (direction > 0) draft->screen->OpenPage([] { return CreateSignagePage(); });
  };
  return row;
}

MenuRow ModMenuRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "SHOW MODS TAB";
  row.description =
      "Shows the MODS tab next to this one, with everything added to the game: quick restart, "
      "career difficulty, fast post-race, custom decals, track signage and more. Turning it off only hides "
      "the tab: what you set there stays as it is.";
  row.value = [draft] { return std::string(draft->Value(kModMenu) == "true" ? "ON" : "OFF"); };
  row.adjust = [draft](int) {
    draft->Change(kModMenu, draft->Value(kModMenu) == "true" ? "false" : "true");
  };
  row.badge = [] { return LaunchOptionBadge(kModMenu); };
  row.unapplied = [draft] { return draft->pending.contains(kModMenu); };
  return row;
}

MenuRow Heading(const char* label) {
  MenuRow row;
  row.label = label;
  row.heading = true;
  row.divider_above = true;
  return row;
}

// One setting's row, wherever the layout puts it.
void AddRow(std::vector<MenuRow>& rows, SettingRow setting, const std::shared_ptr<Draft>& draft,
            const std::shared_ptr<PickedFolder>& picked) {
  const auto hardcore = [&](size_t i) { rows.push_back(HardcoreRow(draft, HardcoreSettings()[i])); };
  switch (setting) {
    case SettingRow::kQuickRaceLaps: rows.push_back(LapsRow(draft)); break;
    case SettingRow::kQuickRestart: rows.push_back(QuickRestartRow(draft)); break;
    case SettingRow::kQuickRestartButton: rows.push_back(QuickRestartButtonRow(draft)); break;
    case SettingRow::kQuickRestartTrigger: rows.push_back(RestartTriggerRow(draft)); break;
    case SettingRow::kNarrator: rows.push_back(NarratorRow(draft)); break;
    case SettingRow::kOnlineItems: rows.push_back(OnlineItemsRow(draft)); break;
    case SettingRow::kFastPostRace: rows.push_back(FastPostRaceRow(draft)); break;
    case SettingRow::kRacePayouts: hardcore(0); break;
    case SettingRow::kRepairCosts: hardcore(1); break;
    case SettingRow::kResaleValue: hardcore(2); break;
    case SettingRow::kAutoRestart: rows.push_back(AutoRestartRow(draft)); break;
    case SettingRow::kDecalsFolder: rows.push_back(DecalsFolderRow(draft, picked)); break;
    case SettingRow::kAllSponsors: rows.push_back(AllSponsorsRow(draft)); break;
    case SettingRow::kTrackSignage: rows.push_back(SignageRow(draft)); break;
    case SettingRow::kModMenu: rows.push_back(ModMenuRow(draft)); break;
  }
}

std::unique_ptr<MenuScreen> CreatePage(const SettingsTab& tab, config::HostConfig& config,
                                       const std::filesystem::path& game_data_root) {
  auto draft = std::make_shared<Draft>(config, game_data_root);
  auto picked = std::make_shared<PickedFolder>();
  std::vector<MenuRow> rows;
  for (const SettingsSection& section : tab.sections) {
    if (section.heading) rows.push_back(Heading(section.heading));
    for (const SettingRow setting : section.rows) AddRow(rows, setting, draft, picked);
  }
  // The first heading sits at the top: no line above it.
  if (!rows.empty() && rows.front().heading) rows.front().divider_above = false;
  auto screen = std::make_unique<MenuScreen>(tab.title, std::move(rows));
  draft->screen = screen.get();
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

// A saved true or false at startup, below a launch option.
void ApplySavedBool(const config::HostConfig& config, const char* name) {
  const auto value = config.Get(name);
  if (!value) return;
  if (FromCommandLine(name)) {
    REXLOG_INFO("Gameplay settings: {} comes from the command line, not {}", name, config.path().string());
  } else if ((*value != "true" && *value != "false") || !rex::cvar::SetFlagByName(name, *value)) {
    REXLOG_WARN("Gameplay settings: ignored {} = {} in {}", name, *value, config.path().string());
  } else {
    REXLOG_INFO("Gameplay settings: {} = {}", name, *value);
  }
}

}  // namespace

void SetFolderPicker(FolderPicker picker) {
  std::lock_guard lock(g_picker_mutex);
  g_picker = std::move(picker);
}

void SetSettingsTabsChanged(std::function<void()> changed) {
  std::lock_guard lock(g_tabs_changed_mutex);
  g_tabs_changed = std::move(changed);
}

std::vector<SettingsPageInfo> SettingsPages() {
  const SettingsLayout& layout = ActiveSettingsLayout();
  std::vector<SettingsPageInfo> pages{{layout.game.title, false}};
  if (!layout.mod.sections.empty() && REXCVAR_GET(fm4_mod_menu)) pages.push_back({layout.mod.title, true});
  return pages;
}

std::unique_ptr<MenuScreen> CreateSettingsPage(bool mod, config::HostConfig& config,
                                               const std::filesystem::path& game_data_root) {
  const SettingsLayout& layout = ActiveSettingsLayout();
  return CreatePage(mod ? layout.mod : layout.game, config, game_data_root);
}

std::unique_ptr<MenuScreen> CreateGameplayPage(config::HostConfig& config,
                                               const std::filesystem::path& game_data_root) {
  static const SettingsTab all = EverySettingOnOnePage();
  return CreatePage(all, config, game_data_root);
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
  }
  if (const auto value = config.Get(kAutoRestart); value && !FromCommandLine(kAutoRestart)) {
    if (std::any_of(kAutoRestarts.begin(), kAutoRestarts.end(), [&](const auto& e) { return e.first == *value; })) {
      rex::cvar::SetFlagByName(kAutoRestart, *value);
      REXLOG_INFO("Gameplay settings: {} = {}", kAutoRestart, *value);
    } else {
      REXLOG_WARN("Gameplay settings: ignored {} = {}", kAutoRestart, *value);
    }
  }
  for (const char* name : {kMuteNarrator, kHideOnlineItems, kAllSponsors, kModMenu}) ApplySavedBool(config, name);
  if (const auto value = config.Get(kDecalsFolder); value && !FromCommandLine(kDecalsFolder)) {
    rex::cvar::SetFlagByName(kDecalsFolder, *value);
    REXLOG_INFO("Gameplay settings: {} = {}", kDecalsFolder, *value);
  }
  for (const HardcoreSetting& setting : HardcoreSettings()) {
    const auto value = config.Get(setting.name);
    if (!value || FromCommandLine(setting.name)) continue;
    if (ChoiceIndex(setting, *value) >= 0 && rex::cvar::SetFlagByName(setting.name, *value)) {
      REXLOG_INFO("Gameplay settings: {} = {}", setting.name, *value);
    } else {
      REXLOG_WARN("Gameplay settings: ignored {} = {}", setting.name, *value);
    }
  }
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
