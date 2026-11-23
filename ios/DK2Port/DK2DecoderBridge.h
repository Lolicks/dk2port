#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Thin Objective-C++ wrapper around the independent C decoder layer.
/// Original game files are never bundled; callers supply them explicitly.
@interface DK2DecoderBridge : NSObject

+ (nullable NSDictionary *)wadInfo:(NSData *)wad error:(NSError **)error;
+ (nullable NSData *)wadEntry:(NSString *)name
                          from:(NSData *)wad
                         error:(NSError **)error;
+ (nullable NSData *)decompressKmsh:(NSData *)stored error:(NSError **)error;
+ (nullable NSDictionary *)kmfInfo:(NSData *)kmf error:(NSError **)error;
+ (nullable NSData *)kmfObj:(NSData *)kmf error:(NSError **)error;
+ (nullable NSArray<NSData *> *)animFrames:(NSData *)kmf
                                     limit:(NSUInteger)limit
                                     error:(NSError **)error;

/// Same source as `animFrames:limit:error:` but returns each frame as a
/// compact in-memory buffer instead of a Wavefront OBJ text file.
///
/// Binary layout (host byte order, little-endian on every supported device):
///   u32 magic   = 0x46414B44 ('DKAF')
///   u32 version = 1
///   u32 vertexCount
///   u32 indexCount
///   vertexCount * 3 * float32   XYZ triplets
///   indexCount  * 2 * u16       triangle indices (0 based)
+ (nullable NSArray<NSData *> *)animFrameBuffers:(NSData *)kmf
                                           limit:(NSUInteger)limit
                                           error:(NSError **)error;
+ (nullable NSDictionary *)tgqInfo:(NSData *)tgq error:(NSError **)error;
+ (BOOL)extractTgqAudio:(NSData *)tgq toWAV:(NSString *)path error:(NSError **)error;

@end

NS_ASSUME_NONNULL_END
