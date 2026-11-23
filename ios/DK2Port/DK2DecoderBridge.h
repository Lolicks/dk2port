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
+ (nullable NSDictionary *)tgqInfo:(NSData *)tgq error:(NSError **)error;
+ (BOOL)extractTgqAudio:(NSData *)tgq toWAV:(NSString *)path error:(NSError **)error;

@end

NS_ASSUME_NONNULL_END
