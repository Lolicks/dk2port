#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface DK2Bridge : NSObject

- (instancetype)init;
- (BOOL)loadMapAtPath:(NSString *)path error:(NSError *_Nullable *_Nullable)error;
- (void)loadCreatures:(NSArray<NSString *> *)creatures
                rooms:(NSArray<NSString *> *)rooms
               spells:(NSArray<NSString *> *)spells;
- (int)spawnCreature:(int)species x:(int)x y:(int)y;
- (BOOL)buildRoom:(int)type x:(int)x y:(int)y owner:(int)owner;
- (BOOL)castSpell:(int)spell x:(int)x y:(int)y;
- (BOOL)findPathFromX:(int)sx y:(int)sy toX:(int)tx y:(int)ty result:(NSArray<NSNumber *> *_Nullable *_Nullable)result;
- (void)step;
- (void)panDX:(float)dx dy:(float)dy;
- (void)zoom:(float)factor;
- (void)rotate:(float)delta;
- (void)selectWorldX:(float)x y:(float)y;
- (BOOL)saveToPath:(NSString *)path;
- (BOOL)loadFromPath:(NSString *)path;

@property (nonatomic, readonly) NSDictionary *snapshot;
@property (nonatomic, readonly) NSArray<NSDictionary *> *tileInstances;
@property (nonatomic, readonly) NSArray<NSDictionary *> *creatureInstances;

@end

NS_ASSUME_NONNULL_END
