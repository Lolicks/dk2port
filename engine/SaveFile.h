#pragma once

#include <cstdint>
#include <string>

namespace dk2 {
namespace save {

constexpr uint32_t kContainerMagic = 0x50324b44;  // "DK2P"
constexpr uint32_t kContainerVersion = 1;
constexpr uint32_t kMaxMissionName = 128;

struct SaveInfo {
    uint32_t container_version = kContainerVersion;
    uint32_t payload_version = 0;      // version stored by Game::save (4)
    uint64_t timestamp = 0;            // Unix seconds
    uint32_t tick = 0;
    float elapsed = 0.0f;
    uint32_t creature_count = 0;
    uint32_t payload_size = 0;
    uint32_t crc32 = 0;
    char mission[kMaxMissionName] = {};
    bool valid = false;
};

uint32_t crc32(const uint8_t* data, size_t size);

/* Wrap an existing Game::save payload into the container at `path`.
   Writes to path.tmp first and renames atomically on success. */
bool write_container(const std::string& path,
                     const uint8_t* payload, size_t payload_size,
                     const SaveInfo& info, std::string* error = nullptr);

/* Read container metadata without deserializing the game state. */
bool read_info(const std::string& path, SaveInfo* info,
               std::string* error = nullptr);

/* Extract payload into `payload_path` after verifying magic, version and CRC.
   Legacy v2/v3/v4 raw saves (starting with "DK2S") are passed through. */
bool extract_payload(const std::string& path, const std::string& payload_path,
                     SaveInfo* info, std::string* error = nullptr);

}  // namespace save
}  // namespace dk2
