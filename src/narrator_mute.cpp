// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "narrator_mute.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem/content_override.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(fm4_mute_narrator, false, "Forza Motorsport 4",
                    "Silences the narrator who introduces World Tour events and tracks in the menus; "
                    "Jeremy Clarkson's Autovista commentary stays (restart required)");

namespace fm4_static_recomp {
namespace {

// FSB4: "FSB4", sample count, sample header bytes, data bytes, version, mode, then the
// 16-byte hash and GUID, so the sample headers start at 0x30. Each sample header begins with
// its own size; the fields used here are at fixed offsets from it.
constexpr size_t kHeaderSize = 0x30;
constexpr size_t kLengthSamples = 32;
constexpr size_t kLoopStart = 40;
constexpr size_t kLoopEnd = 44;
constexpr size_t kDefaultVolume = 56;
constexpr size_t kMinSampleHeader = kDefaultVolume + 2;
constexpr uint16_t kFullVolume = 255;
constexpr uint32_t kXmaFrameSamples = 512;

uint32_t ReadU32(std::span<const uint8_t> bytes, size_t at) {
  uint32_t value;
  std::memcpy(&value, bytes.data() + at, sizeof(value));
  return value;
}

uint16_t ReadU16(std::span<const uint8_t> bytes, size_t at) {
  uint16_t value;
  std::memcpy(&value, bytes.data() + at, sizeof(value));
  return value;
}

template <typename T>
void Write(std::span<uint8_t> bytes, size_t at, T value) {
  std::memcpy(bytes.data() + at, &value, sizeof(value));
}

}  // namespace

bool MuteNarratorBank(std::span<uint8_t> bank) {
  if (bank.size() < kHeaderSize || std::memcmp(bank.data(), "FSB4", 4) != 0) return false;
  const uint32_t count = ReadU32(bank, 4);
  const uint32_t headers_end = kHeaderSize + ReadU32(bank, 8);
  if (count == 0 || headers_end > bank.size()) return false;
  std::vector<size_t> samples;
  samples.reserve(count);
  size_t at = kHeaderSize;
  for (uint32_t i = 0; i < count; ++i) {
    if (at + 2 > headers_end) return false;
    const uint16_t size = ReadU16(bank, at);
    if (size < kMinSampleHeader || at + size > headers_end) return false;
    if (ReadU16(bank, at + kDefaultVolume) != kFullVolume) return false;
    samples.push_back(at);
    at += size;
  }
  for (const size_t sample : samples) {
    Write<uint16_t>(bank, sample + kDefaultVolume, 0);
    Write<uint32_t>(bank, sample + kLengthSamples, kXmaFrameSamples);
    Write<uint32_t>(bank, sample + kLoopStart, 0);
    Write<uint32_t>(bank, sample + kLoopEnd, kXmaFrameSamples - 1);
  }
  return true;
}

void InstallNarratorMute(const std::filesystem::path& game_data_root) {
  if (!REXCVAR_GET(fm4_mute_narrator)) return;
  // The game opens one language's bank; muting each one keeps fm4_language and the console
  // language out of it.
  std::error_code ec;
  int muted = 0;
  for (const auto& entry :
       std::filesystem::directory_iterator(game_data_root / "Media" / "audio" / "VO", ec)) {
    const auto path = entry.path() / "vo.fsb";
    if (!std::filesystem::is_regular_file(path, ec)) continue;
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bank((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::string language = entry.path().filename().string();
    if (!MuteNarratorBank(bank)) {
      REXLOG_WARN("MUTE NARRATOR not applied to {}: the voice bank is not the disc's", language);
      continue;
    }
    rex::filesystem::SetContentOverride("game:\\media\\audio\\vo\\" + language + "\\vo.fsb",
                                        std::move(bank));
    ++muted;
  }
  if (muted == 0) {
    REXLOG_WARN("MUTE NARRATOR: no voice bank found under {}", game_data_root.string());
  } else {
    REXLOG_INFO("MUTE NARRATOR: {} voice banks muted", muted);
  }
}

}  // namespace fm4_static_recomp
