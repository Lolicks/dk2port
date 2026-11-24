#include "SaveFile.h"

#include <cstdio>
#include <cstring>

namespace dk2 {
namespace save {

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

namespace {
void put32(FILE* f, uint32_t v) {
    uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    fwrite(b, 1, 4, f);
}
void put64(FILE* f, uint64_t v) {
    put32(f, uint32_t(v));
    put32(f, uint32_t(v >> 32));
}
bool get32(FILE* f, uint32_t& v) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    v = uint32_t(b[0]) | (uint32_t(b[1]) << 8) |
        (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
    return true;
}
bool get64(FILE* f, uint64_t& v) {
    uint32_t lo, hi;
    if (!get32(f, lo) || !get32(f, hi)) return false;
    v = uint64_t(lo) | (uint64_t(hi) << 32);
    return true;
}
}  // namespace

bool write_container(const std::string& path, const uint8_t* payload,
                     size_t size, const SaveInfo& info, std::string* error) {
    if (!payload && size) {
        if (error) *error = "null save payload";
        return false;
    }
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) {
        if (error) *error = "cannot create save temp file";
        return false;
    }
    put32(f, kContainerMagic);
    put32(f, kContainerVersion);
    put32(f, uint32_t(size));
    put32(f, crc32(payload, size));
    put64(f, info.timestamp);
    put32(f, info.tick);
    uint32_t elapsed_bits;
    static_assert(sizeof(elapsed_bits) == sizeof(info.elapsed), "float size");
    memcpy(&elapsed_bits, &info.elapsed, 4);
    put32(f, elapsed_bits);
    put32(f, info.creature_count);
    uint32_t name_len = uint32_t(strnlen(info.mission, kMaxMissionName));
    put32(f, name_len);
    fwrite(info.mission, 1, name_len, f);
    if (size) fwrite(payload, 1, size, f);
    if (fclose(f) != 0) {
        remove(tmp.c_str());
        if (error) *error = "failed to close save file";
        return false;
    }
    remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        remove(tmp.c_str());
        if (error) *error = "failed to commit save file";
        return false;
    }
    return true;
}

bool read_info(const std::string& path, SaveInfo* info, std::string* error) {
    if (!info) return false;
    *info = SaveInfo();
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        if (error) *error = "save file not found";
        return false;
    }
    uint32_t magic = 0, ver = 0;
    if (!get32(f, magic) || !get32(f, ver)) {
        fclose(f);
        if (error) *error = "truncated save header";
        return false;
    }
    if (magic != kContainerMagic) {
        fclose(f);
        if (error) *error = "not a DK2Port save container";
        return false;
    }
    info->container_version = ver;
    if (!get32(f, info->payload_size) || !get32(f, info->crc32) ||
        !get64(f, info->timestamp) || !get32(f, info->tick)) {
        fclose(f);
        if (error) *error = "truncated save metadata";
        return false;
    }
    uint32_t bits;
    if (!get32(f, bits)) { fclose(f); return false; }
    memcpy(&info->elapsed, &bits, 4);
    if (!get32(f, info->creature_count)) { fclose(f); return false; }
    uint32_t name_len;
    if (!get32(f, name_len) || name_len >= kMaxMissionName) {
        fclose(f);
        if (error) *error = "invalid mission name length";
        return false;
    }
    if (name_len && fread(info->mission, 1, name_len, f) != name_len) {
        fclose(f);
        if (error) *error = "truncated mission name";
        return false;
    }
    info->mission[name_len] = '\0';
    info->valid = true;
    fclose(f);
    return true;
}

bool extract_payload(const std::string& path, const std::string& payload_path,
                     SaveInfo* info, std::string* error) {
    SaveInfo meta;
    if (!read_info(path, &meta, nullptr)) {
        /* Legacy raw payload (Game::save v2/v3/v4) starting with "DK2S". */
        FILE* raw = fopen(path.c_str(), "rb");
        if (!raw) {
            if (error) *error = "cannot open save file";
            return false;
        }
        uint8_t head[8];
        size_t n = fread(head, 1, 8, raw);
        fclose(raw);
        if (n == 8 && memcmp(head, "DK2S", 4) == 0) {
            if (info) *info = meta;
            return true;  // caller reads the original path directly
        }
        if (error) *error = "unrecognized save format";
        return false;
    }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long total = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t fixed = 4 + 4 + 4 + 4 + 8 + 4 + 4 + 4 + 4;  // up to name length
    uint32_t name_len = uint32_t(strnlen(meta.mission, kMaxMissionName));
    long header_size = long(fixed + name_len);
    if (total < header_size || meta.payload_size != uint32_t(total - header_size)) {
        fclose(f);
        if (error) *error = "save payload size mismatch";
        return false;
    }
    fseek(f, header_size, SEEK_SET);
    uint8_t* payload = new uint8_t[meta.payload_size ? meta.payload_size : 1];
    size_t got = fread(payload, 1, meta.payload_size, f);
    fclose(f);
    if (got != meta.payload_size) {
        delete[] payload;
        if (error) *error = "truncated save payload";
        return false;
    }
    if (crc32(payload, meta.payload_size) != meta.crc32) {
        delete[] payload;
        if (error) *error = "save CRC mismatch";
        return false;
    }
    bool ok = false;
    FILE* out = fopen(payload_path.c_str(), "wb");
    if (out) {
        ok = fwrite(payload, 1, meta.payload_size, out) == meta.payload_size;
        ok = fclose(out) == 0 && ok;
    }
    delete[] payload;
    if (info) *info = meta;
    if (!ok && error) *error = "cannot materialize save payload";
    return ok;
}

}  // namespace save
}  // namespace dk2
