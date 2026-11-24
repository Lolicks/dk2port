#import "DK2Bridge.h"

#include "DK2.h"

#include <memory>
#include <string>
#include <vector>

@implementation DK2Bridge {
    std::unique_ptr<dk2::Game> _game;
}

+ (instancetype)shared {
    static DK2Bridge *instance;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ instance = [[DK2Bridge alloc] init]; });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _game = std::make_unique<dk2::Game>();
    }
    return self;
}

static std::string stdString(NSString *s) { return s.UTF8String ?: ""; }

- (BOOL)loadMapAtPath:(NSString *)path error:(NSError **)error {
    NSData *data = [NSData dataWithContentsOfFile:path];
    if (!data) {
        if (error) {
            *error = [NSError errorWithDomain:@"DK2"
                                        code:1
                                    userInfo:@{NSLocalizedDescriptionKey: @"Map file missing"}];
        }
        return NO;
    }
    std::string err;
    BOOL ok = _game->load_map(static_cast<const uint8_t *>(data.bytes), data.length, &err);
    if (ok) {
        NSString *stem = [[path lastPathComponent] stringByDeletingPathExtension];
        _game->set_mission(stdString(stem));
    }
    if (!ok && error) {
        *error = [NSError errorWithDomain:@"DK2"
                                    code:2
                                userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithUTF8String:err.c_str()]}];
    }
    return ok;
}

- (void)loadCreatures:(NSArray<NSString *> *)creatures
                rooms:(NSArray<NSString *> *)rooms
               spells:(NSArray<NSString *> *)spells {
    auto conv = ^(NSArray<NSString *> *input) {
        std::vector<std::string> out;
        for (NSString *s in input) out.push_back(stdString(s));
        return out;
    };
    _game->load_catalog(conv(creatures), conv(rooms), conv(spells));
}

- (void)spawnInitialKeeper:(int)count {
    _game->spawn_initial_keeper(count);
}

- (int)spawnCreature:(int)species x:(int)x y:(int)y {
    return _game->spawn_creature(species, x, y);
}

- (BOOL)buildRoom:(int)type x:(int)x y:(int)y owner:(int)owner {
    return _game->build_room(type, x, y, owner);
}

- (BOOL)castSpell:(int)spell x:(int)x y:(int)y {
    return _game->cast_spell(spell, x, y);
}

- (BOOL)placeTrap:(int)index x:(int)x y:(int)y owner:(int)owner {
    return _game->place_trap(index, x, y, owner);
}

- (BOOL)placeDoor:(int)index x:(int)x y:(int)y owner:(int)owner {
    return _game->place_door(index, x, y, owner);
}

- (BOOL)findPathFromX:(int)sx y:(int)sy toX:(int)tx y:(int)ty result:(NSArray<NSNumber *> **)result {
    std::vector<int> path;
    if (!_game->find_path(sx, sy, tx, ty, path)) return NO;
    NSMutableArray *out = [NSMutableArray arrayWithCapacity:path.size()];
    for (int node : path) [out addObject:@(node)];
    if (result) *result = out;
    return YES;
}

- (void)step { _game->step(); }
- (void)panDX:(float)dx dy:(float)dy { _game->on_pan(dx, dy); }
- (void)zoom:(float)factor { _game->on_zoom(factor); }
- (void)rotate:(float)delta { _game->on_rotate(delta); }
- (void)selectWorldX:(float)x y:(float)y { _game->select_at(x, y); }
- (BOOL)saveToPath:(NSString *)path { return _game->save(stdString(path)); }
- (BOOL)loadFromPath:(NSString *)path { return _game->load(stdString(path)); }

- (NSDictionary *)snapshot {
    const dk2::GameState &s = _game->state();
    return @{
        @"width": @(s.world.width),
        @"height": @(s.world.height),
        @"gold": @(s.economy.gold),
        @"mana": @(s.mana),
        @"manaMax": @(s.mana_max),
        @"goldCapacity": @(s.economy.gold_capacity),
        @"treasures": @(s.economy.treasures),
        @"creatures": @(s.creatures.size()),
        @"rooms": @(s.rooms.size()),
        @"centerX": @(s.camera.center_x),
        @"centerY": @(s.camera.center_y),
        @"zoom": @(s.camera.zoom),
        @"yaw": @(s.camera.yaw),
        @"ticks": @(s.tick_count),
        @"elapsed": @(s.elapsed),
        @"mission": [NSString stringWithUTF8String:s.mission_name.c_str()],
        @"victory": @(s.victory),
        @"defeat": @(s.defeat),
        @"selected": @(_game->selected_creature),
        @"selectedX": @(s.selected_tile_x),
        @"selectedY": @(s.selected_tile_y),
    };
}

// Atlas index assignment is shared with MetalRenderer.swift.
static uint32_t atlasIndexForTerrain(uint8_t terrain) {
    if (terrain >= 25) return 63;
    return terrain;
}

- (NSArray<NSDictionary *> *)tileInstances {
    const dk2::World &w = _game->state().world;
    const dk2::CameraState &cam = _game->state().camera;
    NSMutableArray *out = [NSMutableArray arrayWithCapacity:(NSUInteger)w.tiles.size()];
    int x0 = std::max(0, int(cam.center_x - 26));
    int x1 = std::min(w.width - 1, int(cam.center_x + 26));
    int y0 = std::max(0, int(cam.center_y - 26));
    int y1 = std::min(w.height - 1, int(cam.center_y + 26));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const dk2::Tile *t = w.at(x, y);
            if (!t) continue;
            [out addObject:@{
                @"x": @(float(x)),
                @"y": @(float(y)),
                @"index": @(atlasIndexForTerrain(t->terrain)),
                @"terrain": @(t->terrain),
                @"flags": @(t->flags),
                @"owner": @(t->owner),
            }];
        }
    }
    return out;
}

- (NSArray<NSDictionary *> *)creatureInstances {
    const dk2::GameState &s = _game->state();
    NSMutableArray *out = [NSMutableArray arrayWithCapacity:s.creatures.size()];
    for (const dk2::Creature &c : s.creatures) {
        [out addObject:@{
            @"x": @(c.x - 0.5f),
            @"y": @(c.y - 0.5f),
            @"index": @(32u + uint32_t(c.species % 16u)),
            @"species": @(c.species),
            @"hp": @(c.hp / std::max(1.f, c.max_hp)),
        }];
    }
    return out;
}

@end
