#import "DK2DecoderBridge.h"

#include "../../decoder/core/reader.h"
#include "../../decoder/wad/wad.h"
#include "../../decoder/kmsh/kmsh.h"
#include "../../decoder/kmf/kmf.h"
#include "../../decoder/kmf/anim.h"
#include "../../decoder/tgq/tgq.h"
#include "../../decoder/audio/tgq_audio.h"
#include "../../engine/SaveFile.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

static NSError *DK2Error(NSString *domain, NSInteger code, NSString *text) {
    return [NSError errorWithDomain:domain code:code
                            userInfo:@{NSLocalizedDescriptionKey: text}];
}

/// Export ANIM frames into a private temporary directory and return the OBJ
/// text for each frame. Shared by the text and binary entry points so the
/// C decoder is only ever called from one place.
static NSArray<NSData *> *DK2AnimOBJFrames(NSData *kmf, NSUInteger limit,
                                           NSError **error) {
    NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:
        [NSString stringWithFormat:@"dk2anim_%u", arc4random()]];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir
                              withIntermediateDirectories:YES
                                               attributes:nil error:nil];
    unsigned written = 0;
    Dk2Result rc = dk2_anim_export(
        static_cast<const uint8_t *>(kmf.bytes), kmf.length,
        dir.UTF8String, static_cast<unsigned>(limit), &written);
    if (rc != DK2_OK) {
        [[NSFileManager defaultManager] removeItemAtPath:dir error:nil];
        if (error) *error = DK2Error(@"DK2Decoder", rc, @"ANIM export failed");
        return nil;
    }
    NSMutableArray *frames = [NSMutableArray arrayWithCapacity:written];
    for (unsigned i = 0; i < written; ++i) {
        NSString *name = [NSString stringWithFormat:@"frame_%04d.obj", i];
        NSData *d = [NSData dataWithContentsOfFile:
            [dir stringByAppendingPathComponent:name]];
        if (d) [frames addObject:d];
    }
    [[NSFileManager defaultManager] removeItemAtPath:dir error:nil];
    return frames;
}

/// Convert one exported frame into the compact binary layout documented in
/// DK2DecoderBridge.h. Faces in the OBJ are already fully expanded triangles
/// with one based indices; only position and face lines are needed here.
static NSData *DK2BinaryFrameFromOBJ(NSData *obj) {
    std::vector<float> pos;
    std::vector<uint16_t> idx;
    std::string text(static_cast<const char *>(obj.bytes), obj.length);
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 2 && line[0] == 'v' &&
            (line[1] == ' ' || line[1] == '\t')) {
            float x = 0, y = 0, z = 0;
            if (std::sscanf(line.c_str() + 2, "%f %f %f", &x, &y, &z) == 3) {
                pos.push_back(x); pos.push_back(y); pos.push_back(z);
            }
        } else if (line.size() >= 2 && line[0] == 'f' &&
                   (line[1] == ' ' || line[1] == '\t')) {
            std::istringstream iss(line.substr(2));
            std::string token;
            while (iss >> token) {
                size_t slash = token.find('/');
                std::string number = (slash == std::string::npos)
                    ? token : token.substr(0, slash);
                int v = std::atoi(number.c_str());
                if (v > 0) {
                    uint32_t zeroBased = static_cast<uint32_t>(v - 1);
                    idx.push_back(static_cast<uint16_t>(
                        zeroBased > 65535u ? 65535u : zeroBased));
                }
            }
        }
    }
    uint32_t vertexCount = static_cast<uint32_t>(pos.size() / 3);
    uint32_t indexCount = static_cast<uint32_t>(idx.size());
    const uint32_t magic = 0x46414B44u;
    const uint32_t version = 1u;
    NSMutableData *out = [NSMutableData dataWithCapacity:
        16 + pos.size() * sizeof(float) + idx.size() * sizeof(uint16_t)];
    [out appendBytes:&magic length:4];
    [out appendBytes:&version length:4];
    [out appendBytes:&vertexCount length:4];
    [out appendBytes:&indexCount length:4];
    if (!pos.empty()) [out appendBytes:pos.data() length:pos.size() * sizeof(float)];
    if (!idx.empty()) [out appendBytes:idx.data() length:idx.size() * sizeof(uint16_t)];
    return out;
}

@implementation DK2DecoderBridge

+ (NSDictionary *)saveInfoAtPath:(NSString *)path error:(NSError **)error {
    dk2::save::SaveInfo info;
    std::string err;
    if (!dk2::save::read_info(stdString(path), &info, &err)) {
        if (error) *error = [NSError errorWithDomain:@"DK2Save" code:1
            userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithUTF8String:err.c_str()]}];
        return nil;
    }
    return @{
        @"valid": @(info.valid),
        @"version": @(info.container_version),
        @"payloadVersion": @(info.payload_version),
        @"tick": @(info.tick),
        @"elapsed": @(info.elapsed),
        @"creatures": @(info.creature_count),
        @"mission": @(info.mission),
        @"timestamp": @(info.timestamp),
    };
}


+ (NSDictionary *)wadInfo:(NSData *)wad error:(NSError **)error {
    Dk2Wad parsed;
    Dk2Result rc = dk2_wad_parse(
        static_cast<const uint8_t *>(wad.bytes), wad.length, &parsed);
    if (rc != DK2_OK) {
        if (error) *error = DK2Error(@"DK2Decoder", rc, @"WAD parse failed");
        return nil;
    }
    NSMutableArray *names = [NSMutableArray array];
    for (uint32_t i = 0; i < parsed.count; ++i) {
        if ([names count] >= 32) break;
        [names addObject:@(parsed.entries[i].name)];
    }
    NSDictionary *result = @{@"entries": @(parsed.count), @"sample": names};
    dk2_wad_free(&parsed);
    return result;
}

+ (NSData *)wadEntry:(NSString *)name from:(NSData *)wad error:(NSError **)error {
    Dk2Wad parsed;
    Dk2Result rc = dk2_wad_parse(
        static_cast<const uint8_t *>(wad.bytes), wad.length, &parsed);
    if (rc != DK2_OK) {
        if (error) *error = DK2Error(@"DK2Decoder", rc, @"WAD parse failed");
        return nil;
    }
    NSData *result = nil;
    for (uint32_t i = 0; i < parsed.count; ++i) {
        if ([name UTF8String] &&
            strcmp(parsed.entries[i].name, [name UTF8String]) == 0) {
            uint8_t *bytes = nullptr;
            size_t size = 0;
            rc = dk2_wad_get(static_cast<const uint8_t *>(wad.bytes), wad.length,
                             &parsed.entries[i], &bytes, &size);
            if (rc == DK2_OK) {
                result = [NSData dataWithBytes:bytes length:size];
                free(bytes);
            }
            break;
        }
    }
    dk2_wad_free(&parsed);
    if (!result && error)
        *error = DK2Error(@"DK2Decoder", 4, @"WAD entry not found");
    return result;
}

+ (NSData *)decompressKmsh:(NSData *)stored error:(NSError **)error {
    uint8_t *out = nullptr;
    size_t out_size = 0;
    Dk2Result rc = dk2_kmsh_decompress(
        static_cast<const uint8_t *>(stored.bytes), stored.length, &out, &out_size);
    if (rc != DK2_OK) {
        if (error) *error = DK2Error(@"DK2Decoder", rc, @"KMSH decompression failed");
        return nil;
    }
    NSData *result = [NSData dataWithBytes:out length:out_size];
    free(out);
    return result;
}

+ (NSDictionary *)kmfInfo:(NSData *)kmf error:(NSError **)error {
    Dk2KmfInfo info;
    Dk2Result rc = dk2_kmf_info(
        static_cast<const uint8_t *>(kmf.bytes), kmf.length, &info);
    if (rc != DK2_OK) {
        if (error) *error = DK2Error(@"DK2Decoder", rc, @"KMF parse failed");
        return nil;
    }
    NSMutableArray *textures = [NSMutableArray array];
    for (uint32_t i = 0; i < info.texture_count; ++i)
        [textures addObject:@(info.textures[i])];
    return @{
        @"version": @(info.version),
        @"type": @(info.type),
        @"chunks": @(info.chunk_count),
        @"materials": @(info.material_count),
        @"textures": textures,
        @"vertices": @(info.vertex_count),
        @"triangles": @(info.triangle_count),
        @"lod0Triangles": @(info.lod0_triangles),
        @"groups": @(info.sprite_groups),
    };
}

+ (NSData *)kmfObj:(NSData *)kmf error:(NSError **)error {
    NSString *tmp = [NSTemporaryDirectory() stringByAppendingPathComponent:
        [NSString stringWithFormat:@"dk2_%u.obj", arc4random()]];
    FILE *f = fopen(tmp.UTF8String, "wb");
    if (!f) {
        if (error) *error = DK2Error(@"DK2Decoder", 1, @"cannot create temp OBJ");
        return nil;
    }
    Dk2Result rc = dk2_kmf_export_obj(
        static_cast<const uint8_t *>(kmf.bytes), kmf.length, f);
    fclose(f);
    if (rc != DK2_OK) {
        [[NSFileManager defaultManager] removeItemAtPath:tmp error:nil];
        if (error) *error = DK2Error(@"DK2Decoder", rc, @"OBJ export failed");
        return nil;
    }
    NSData *data = [NSData dataWithContentsOfFile:tmp];
    [[NSFileManager defaultManager] removeItemAtPath:tmp error:nil];
    return data;
}

+ (NSArray<NSData *> *)animFrames:(NSData *)kmf
                             limit:(NSUInteger)limit
                             error:(NSError **)error {
    return DK2AnimOBJFrames(kmf, limit, error);
}

+ (NSArray<NSData *> *)animFrameBuffers:(NSData *)kmf
                                  limit:(NSUInteger)limit
                                  error:(NSError **)error {
    NSArray<NSData *> *objFrames = DK2AnimOBJFrames(kmf, limit, error);
    if (!objFrames) return nil;
    NSMutableArray *buffers = [NSMutableArray arrayWithCapacity:objFrames.count];
    for (NSData *frame in objFrames) {
        NSData *binary = DK2BinaryFrameFromOBJ(frame);
        if (binary) [buffers addObject:binary];
    }
    return buffers;
}


+ (NSDictionary *)tgqInfo:(NSData *)tgq error:(NSError **)error {
    Dk2TgqInfo info;
    Dk2Result rc = dk2_tgq_parse(
        static_cast<const uint8_t *>(tgq.bytes), tgq.length, &info);
    if (rc != DK2_OK) {
        if (error) *error = DK2Error(@"DK2Decoder", rc, @"TGQ parse failed");
        return nil;
    }
    return @{
        @"width": @(info.width),
        @"height": @(info.height),
        @"frames": @(info.frame_count),
        @"audioBlocks": @(info.audio_count),
    };
}

+ (BOOL)extractTgqAudio:(NSData *)tgq toWAV:(NSString *)path error:(NSError **)error {
    Dk2Result rc = dk2_tgq_audio_extract(
        static_cast<const uint8_t *>(tgq.bytes), tgq.length, path.UTF8String);
    if (rc != DK2_OK) {
        if (error) *error = DK2Error(@"DK2Decoder", rc, @"XA audio decode failed");
        return NO;
    }
    return YES;
}

@end
