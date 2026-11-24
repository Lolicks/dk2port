#include "DK2.h"
#include "Balance.generated.hpp"
#include "CreatureStats.generated.hpp"
#include "MissionRules.generated.hpp"
#include "MissionTriggers.generated.hpp"
#include "SpellData.generated.hpp"
#include "Structures.h"
#include "GridSimulation.h"
#include "SaveFile.h"
#include <ctime>
#include <iterator>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <queue>
#include <sstream>

namespace dk2 {

namespace {
const char* kTerrainFallback = "Unknown";
const char* kTerrainNames[] = {
    "Impenetrable Rock", "Rock", "Dirt Path", "Water", "Lava", "Gold",
    "Gems", "Claimed Path", "Reinforced Wall", "Treasury", "Lair", "Portal",
    "Hatchery", "Dungeon Heart", "Library", "Training Room", "Wooden Bridge",
    "Guard Room", "Work Shop", "Prison", "Torture Chamber", "Graveyard",
    "Temple", "Scavenger Room", "Behemoth Pit",
};

struct SaveHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t world_w, world_h;
    uint32_t creature_count, room_count;
};

// Appended to v3 saves as a length-tagged trailer so old v3 files remain
// readable: absent trailer => all fields keep their defaults.
struct SaveCreatureExtras {
    float xp; int level;
    float exp_for_next_level, exp_per_second, exp_per_second_training;
    float research_per_second, manufacture_per_second, mana_gen_prayer;
    int job_class, fight_style, room_capacity_needed;
    float hunger_rate, hunger_fill, time_awake, time_sleep;
    float time_awake_elapsed, time_slept, sleep_need;
    float last_ate_time, last_slept_time, last_attempt_hunger, last_attempt_sleep;
    float pay, pay_timer, payment_needed;
    int payment_attempts; int get_paid, skip_next_payment;
    float anger_values[6];
    float anger_no_lair, anger_no_food, anger_no_pay, anger_no_work, anger_slap, anger_in_hand;
    float unhappy_threshold; int anger_state;
    float current_fear, temp_fear, threat, threat_threshold;
    int overthreat_counter, threat_thing, attacking_id, attack_type, spell_no;
    float attack_range, backoff_timer, fearless_until;
    int avoid_creature; float avoid_counter; int retreating_creature;
    int melee_attackers[8], range_attackers[8];
    int state_id; float state_age, state_started_at, state_tick_last_updated;
    int current_job_id, return_job_id, dropped_job_id, idle_counter;
    int last_hunger_chosen_tick, last_happy_chosen_tick, last_angry_chosen_tick;
    int failed_move_state, lair_id, room_id;
    int lair_calculated, lair_availability;
    int reporting_to, my_leaders_id, position_in_party;
    float last_slap_tick, last_reaction_time; int last_reaction_target; float last_hello_time;
    int call_to_arms_on, rebel_active, rebel_leader;
    float creature_spell_cooldown, melee_recharge;
    int active_creature_spell, creature_spell_level;
    int spell_id[3], spell_level[3];
    int happy_job[3], happy_chance[3];
    int unhappy_job[3], unhappy_chance[3];
    int angry_job[3], angry_chance[3];
    int attract_room[3], attract_size[3];
    int resist_type[4]; float resist_value[4];
    int ai_flags;
    float death_timer, decompose_value, lair_check_timer;
};

static void pack_extras(const Creature& c, SaveCreatureExtras& e) {
    e.xp = c.xp; e.level = c.level;
    e.exp_for_next_level = c.exp_for_next_level; e.exp_per_second = c.exp_per_second;
    e.exp_per_second_training = c.exp_per_second_training;
    e.research_per_second = c.research_per_second;
    e.manufacture_per_second = c.manufacture_per_second;
    e.mana_gen_prayer = c.mana_gen_prayer;
    e.job_class = c.job_class; e.fight_style = c.fight_style;
    e.room_capacity_needed = c.room_capacity_needed;
    e.hunger_rate = c.hunger_rate; e.hunger_fill = c.hunger_fill;
    e.time_awake = c.time_awake; e.time_sleep = c.time_sleep;
    e.time_awake_elapsed = c.time_awake_elapsed; e.time_slept = c.time_slept;
    e.sleep_need = c.sleep_need;
    e.last_ate_time = c.last_ate_time; e.last_slept_time = c.last_slept_time;
    e.last_attempt_hunger = c.last_attempt_hunger;
    e.last_attempt_sleep = c.last_attempt_sleep;
    e.pay = c.pay; e.pay_timer = c.pay_timer; e.payment_needed = c.payment_needed;
    e.payment_attempts = c.payment_attempts;
    e.get_paid = c.get_paid ? 1 : 0; e.skip_next_payment = c.skip_next_payment ? 1 : 0;
    for (int i = 0; i < 6; ++i) e.anger_values[i] = c.anger_values[i];
    e.anger_no_lair = c.anger_no_lair; e.anger_no_food = c.anger_no_food;
    e.anger_no_pay = c.anger_no_pay; e.anger_no_work = c.anger_no_work;
    e.anger_slap = c.anger_slap; e.anger_in_hand = c.anger_in_hand;
    e.unhappy_threshold = c.unhappy_threshold; e.anger_state = c.anger_state;
    e.current_fear = c.current_fear; e.temp_fear = c.temp_fear;
    e.threat = c.threat; e.threat_threshold = c.threat_threshold;
    e.overthreat_counter = c.overthreat_counter; e.threat_thing = c.threat_thing;
    e.attacking_id = c.attacking_id; e.attack_type = c.attack_type; e.spell_no = c.spell_no;
    e.attack_range = c.attack_range; e.backoff_timer = c.backoff_timer;
    e.fearless_until = c.fearless_until;
    e.avoid_creature = c.avoid_creature; e.avoid_counter = c.avoid_counter;
    e.retreating_creature = c.retreating_creature;
    for (int i = 0; i < 8; ++i) {
        e.melee_attackers[i] = c.melee_attackers[i];
        e.range_attackers[i] = c.range_attackers[i];
    }
    e.state_id = c.state_id; e.state_age = c.state_age;
    e.state_started_at = c.state_started_at;
    e.state_tick_last_updated = c.state_tick_last_updated;
    e.current_job_id = c.current_job_id; e.return_job_id = c.return_job_id;
    e.dropped_job_id = c.dropped_job_id; e.idle_counter = c.idle_counter;
    e.last_hunger_chosen_tick = c.last_hunger_chosen_tick;
    e.last_happy_chosen_tick = c.last_happy_chosen_tick;
    e.last_angry_chosen_tick = c.last_angry_chosen_tick;
    e.failed_move_state = c.failed_move_state;
    e.lair_id = c.lair_id; e.room_id = c.room_id;
    e.lair_calculated = c.lair_calculated ? 1 : 0;
    e.lair_availability = c.lair_availability ? 1 : 0;
    e.reporting_to = c.reporting_to; e.my_leaders_id = c.my_leaders_id;
    e.position_in_party = c.position_in_party;
    e.last_slap_tick = c.last_slap_tick; e.last_reaction_time = c.last_reaction_time;
    e.last_reaction_target = c.last_reaction_target; e.last_hello_time = c.last_hello_time;
    e.call_to_arms_on = c.call_to_arms_on ? 1 : 0;
    e.rebel_active = c.rebel_active ? 1 : 0; e.rebel_leader = c.rebel_leader ? 1 : 0;
    e.creature_spell_cooldown = c.creature_spell_cooldown;
    e.melee_recharge = c.melee_recharge;
    e.active_creature_spell = c.active_creature_spell;
    e.creature_spell_level = c.creature_spell_level;
    for (int i = 0; i < 3; ++i) {
        e.spell_id[i] = c.spell_id[i]; e.spell_level[i] = c.spell_level[i];
        e.happy_job[i] = c.happy_job[i]; e.happy_chance[i] = c.happy_chance[i];
        e.unhappy_job[i] = c.unhappy_job[i]; e.unhappy_chance[i] = c.unhappy_chance[i];
        e.angry_job[i] = c.angry_job[i]; e.angry_chance[i] = c.angry_chance[i];
        e.attract_room[i] = c.attract_room[i]; e.attract_size[i] = c.attract_size[i];
    }
    for (int i = 0; i < 4; ++i) {
        e.resist_type[i] = c.resist_type[i]; e.resist_value[i] = c.resist_value[i];
    }
    e.ai_flags = c.ai_flags;
    e.death_timer = c.death_timer; e.decompose_value = c.decompose_value;
    e.lair_check_timer = c.lair_check_timer;
}

static void unpack_extras(Creature& c, const SaveCreatureExtras& e) {
    c.xp = e.xp; c.level = e.level;
    c.exp_for_next_level = e.exp_for_next_level; c.exp_per_second = e.exp_per_second;
    c.exp_per_second_training = e.exp_per_second_training;
    c.research_per_second = e.research_per_second;
    c.manufacture_per_second = e.manufacture_per_second;
    c.mana_gen_prayer = e.mana_gen_prayer;
    c.job_class = e.job_class; c.fight_style = e.fight_style;
    c.room_capacity_needed = e.room_capacity_needed;
    c.hunger_rate = e.hunger_rate; c.hunger_fill = e.hunger_fill;
    c.time_awake = e.time_awake; c.time_sleep = e.time_sleep;
    c.time_awake_elapsed = e.time_awake_elapsed; c.time_slept = e.time_slept;
    c.sleep_need = e.sleep_need;
    c.last_ate_time = e.last_ate_time; c.last_slept_time = e.last_slept_time;
    c.last_attempt_hunger = e.last_attempt_hunger;
    c.last_attempt_sleep = e.last_attempt_sleep;
    c.pay = e.pay; c.pay_timer = e.pay_timer; c.payment_needed = e.payment_needed;
    c.payment_attempts = e.payment_attempts;
    c.get_paid = e.get_paid != 0; c.skip_next_payment = e.skip_next_payment != 0;
    for (int i = 0; i < 6; ++i) c.anger_values[i] = e.anger_values[i];
    c.anger_no_lair = e.anger_no_lair; c.anger_no_food = e.anger_no_food;
    c.anger_no_pay = e.anger_no_pay; c.anger_no_work = e.anger_no_work;
    c.anger_slap = e.anger_slap; c.anger_in_hand = e.anger_in_hand;
    c.unhappy_threshold = e.unhappy_threshold; c.anger_state = e.anger_state;
    c.current_fear = e.current_fear; c.temp_fear = e.temp_fear;
    c.threat = e.threat; c.threat_threshold = e.threat_threshold;
    c.overthreat_counter = e.overthreat_counter; c.threat_thing = e.threat_thing;
    c.attacking_id = e.attacking_id; c.attack_type = e.attack_type; c.spell_no = e.spell_no;
    c.attack_range = e.attack_range; c.backoff_timer = e.backoff_timer;
    c.fearless_until = e.fearless_until;
    c.avoid_creature = e.avoid_creature; c.avoid_counter = e.avoid_counter;
    c.retreating_creature = e.retreating_creature;
    for (int i = 0; i < 8; ++i) {
        c.melee_attackers[i] = e.melee_attackers[i];
        c.range_attackers[i] = e.range_attackers[i];
    }
    c.state_id = e.state_id; c.state_age = e.state_age;
    c.state_started_at = e.state_started_at;
    c.state_tick_last_updated = e.state_tick_last_updated;
    c.current_job_id = e.current_job_id; c.return_job_id = e.return_job_id;
    c.dropped_job_id = e.dropped_job_id; c.idle_counter = e.idle_counter;
    c.last_hunger_chosen_tick = e.last_hunger_chosen_tick;
    c.last_happy_chosen_tick = e.last_happy_chosen_tick;
    c.last_angry_chosen_tick = e.last_angry_chosen_tick;
    c.failed_move_state = e.failed_move_state;
    c.lair_id = e.lair_id; c.room_id = e.room_id;
    c.lair_calculated = e.lair_calculated != 0;
    c.lair_availability = e.lair_availability != 0;
    c.reporting_to = e.reporting_to; c.my_leaders_id = e.my_leaders_id;
    c.position_in_party = e.position_in_party;
    c.last_slap_tick = e.last_slap_tick; c.last_reaction_time = e.last_reaction_time;
    c.last_reaction_target = e.last_reaction_target; c.last_hello_time = e.last_hello_time;
    c.call_to_arms_on = e.call_to_arms_on != 0;
    c.rebel_active = e.rebel_active != 0; c.rebel_leader = e.rebel_leader != 0;
    c.creature_spell_cooldown = e.creature_spell_cooldown;
    c.melee_recharge = e.melee_recharge;
    c.active_creature_spell = e.active_creature_spell;
    c.creature_spell_level = e.creature_spell_level;
    for (int i = 0; i < 3; ++i) {
        c.spell_id[i] = e.spell_id[i]; c.spell_level[i] = e.spell_level[i];
        c.happy_job[i] = e.happy_job[i]; c.happy_chance[i] = e.happy_chance[i];
        c.unhappy_job[i] = e.unhappy_job[i]; c.unhappy_chance[i] = e.unhappy_chance[i];
        c.angry_job[i] = e.angry_job[i]; c.angry_chance[i] = e.angry_chance[i];
        c.attract_room[i] = e.attract_room[i]; c.attract_size[i] = e.attract_size[i];
    }
    for (int i = 0; i < 4; ++i) {
        c.resist_type[i] = e.resist_type[i]; c.resist_value[i] = e.resist_value[i];
    }
    c.ai_flags = e.ai_flags;
    c.death_timer = e.death_timer; c.decompose_value = e.decompose_value;
    c.lair_check_timer = e.lair_check_timer;
}
constexpr uint32_t kSaveMagic = 0x324b3244;  // "DK2S"
constexpr uint32_t kSaveVersion = 4;
constexpr uint32_t kSaveExtMagic = 0x454b3244;  // "DK2E" trailer

// AI flags recovered from the Creatures.kwd boolean fields.
[[maybe_unused]] constexpr int kAIFlagWorker       = 1 << 0;
constexpr int kAIFlagAlwaysFlee   = 1 << 1;
constexpr int kAIFlagFearless     = 1 << 2;
[[maybe_unused]] constexpr int kAIFlagCanBeSlapped = 1 << 3;
[[maybe_unused]] constexpr int kAIFlagEvil         = 1 << 4;
[[maybe_unused]] constexpr int kAIFlagDeadBody     = 1 << 5;
[[maybe_unused]] constexpr int kAIFlagUnique       = 1 << 6;
[[maybe_unused]] constexpr int kAIFlagCanFly       = 1 << 7;

// Generated from docs/stats.json (Creatures.kwd); see AI_FUNCTIONS.md.
struct DK2CreatureAIStat {
    const char* name;
    float pay, anger_no_lair, anger_no_food, anger_no_pay, anger_no_work, anger_slap, anger_in_hand;
    float unhappy_threshold, hunger_rate, hunger_fill, time_awake, time_sleep;
    float exp_for_next_level, exp_per_second, exp_per_second_training, research_per_second, manufacture_per_second;
    int job_class, fight_style, room_capacity_needed;
    float fear, threat, decompose_value, mana_gen_prayer;
    int flags;
    int spell_id[3], spell_level[3];
    int happy_job[3], happy_chance[3];
    int unhappy_job[3], unhappy_chance[3];
    int angry_job[3], angry_chance[3];
    int attract_room[3], attract_size[3];
    int resist_type[4]; float resist_value[4];
};
static const DK2CreatureAIStat kDK2CreatureAIStats[] = {
    {"Imp", 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 50.0f, 32767.0f, 0.0f, 32767.0f, 0.0f, 3000.0f, 1200.0f, 30.0f, 0.0f, 0.0f, 3, 0, 1, 5.0f, 2.0f, 0.0f, 0.0f, 27, {37,36,0}, {4,8,0}, {24,0,0}, {100,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 0
    {"Prince Tristran", 750.0f, 130.0f, 90.0f, 60.0f, 60.0f, 3000.0f, 1000.0f, 50.0f, 220.0f, 3.0f, 330.0f, 9.0f, 3000.0f, 75.0f, 15.0f, 0.0f, 0.0f, 2, 2, 1, 800.0f, 320.0f, 200.0f, 150.0f, 40, {0,0,0}, {0,0,0}, {4,9,0}, {95,5,0}, {9,11,0}, {90,10,0}, {16,0,0}, {100,0,0}, {0,0,0}, {0,0,0}, {11,4,6,7}, {100.0f,50.0f,50.0f,50.0f}}, // 1
    {"Bile Demon", 600.0f, 150.0f, 150.0f, 60.0f, 50.0f, 3000.0f, 65236.0f, 50.0f, 220.0f, 5.0f, 270.0f, 9.0f, 3000.0f, 100.0f, 20.0f, 0.0f, 50.0f, 2, 2, 1, 300.0f, 120.0f, 200.0f, 50.0f, 56, {5,6,0}, {4,8,0}, {4,5,0}, {40,60,0}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {10,4,0}, {0,25,0}, {7,0,0,0}, {100.0f,0.0f,0.0f,0.0f}}, // 2
    {"Mistress", 600.0f, 120.0f, 110.0f, 60.0f, 50.0f, 60536.0f, 500.0f, 50.0f, 200.0f, 2.0f, 300.0f, 9.0f, 3000.0f, 100.0f, 20.0f, 0.0f, 0.0f, 2, 1, 1, 267.0f, 107.0f, 200.0f, 110.0f, 56, {4,3,11}, {4,8,10}, {4,7,0}, {50,50,0}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {12,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 3
    {"Warlock", 600.0f, 100.0f, 100.0f, 60.0f, 70.0f, 7000.0f, 65036.0f, 50.0f, 200.0f, 2.0f, 300.0f, 9.0f, 3000.0f, 150.0f, 30.0f, 5.0f, 0.0f, 1, 4, 1, 50.0f, 20.0f, 200.0f, 85.0f, 56, {1,14,2}, {1,2,8}, {3,4,8}, {80,10,10}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {6,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 4
    {"Dark Elf", 600.0f, 110.0f, 100.0f, 60.0f, 50.0f, 3000.0f, 65286.0f, 50.0f, 200.0f, 1.0f, 300.0f, 9.0f, 3000.0f, 150.0f, 30.0f, 0.0f, 0.0f, 2, 4, 1, 50.0f, 20.0f, 200.0f, 60.0f, 56, {12,18,7}, {1,4,8}, {6,4,0}, {80,20,0}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {9,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 5
    {"Goblin", 500.0f, 100.0f, 100.0f, 60.0f, 50.0f, 3000.0f, 65286.0f, 50.0f, 180.0f, 1.0f, 240.0f, 9.0f, 3000.0f, 200.0f, 40.0f, 0.0f, 0.0f, 2, 3, 1, 50.0f, 37.0f, 200.0f, 40.0f, 56, {0,0,0}, {0,0,0}, {4,0,0}, {100,0,0}, {9,0,0}, {100,0,0}, {10,16,0}, {50,50,0}, {2,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 6
    {"Vampire", 750.0f, 100.0f, 1.0f, 60.0f, 60.0f, 7000.0f, 65286.0f, 50.0f, 32767.0f, 0.0f, 330.0f, 9.0f, 3000.0f, 150.0f, 30.0f, 5.0f, 0.0f, 1, 1, 1, 267.0f, 107.0f, 0.0f, 95.0f, 28, {8,10,20}, {4,8,10}, {3,8,0}, {80,20,0}, {15,9,0}, {10,90,0}, {16,0,0}, {100,0,0}, {14,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 7
    {"Skeleton", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 50.0f, 32767.0f, 0.0f, 32767.0f, 0.0f, 3000.0f, 200.0f, 40.0f, 1.0f, 0.0f, 2, 3, 1, 30000.0f, 60.0f, 0.0f, 30.0f, 28, {0,0,0}, {0,0,0}, {4,0,0}, {100,0,0}, {6,0,0}, {100,0,0}, {16,0,0}, {100,0,0}, {11,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 8
    {"Troll", 500.0f, 130.0f, 110.0f, 60.0f, 40.0f, 2000.0f, 65336.0f, 50.0f, 200.0f, 3.0f, 330.0f, 10.0f, 3000.0f, 150.0f, 30.0f, 0.0f, 60.0f, 0, 2, 1, 50.0f, 20.0f, 200.0f, 55.0f, 56, {0,0,0}, {0,0,0}, {5,0,0}, {100,0,0}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {10,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 9
    {"Salamander", 500.0f, 90.0f, 110.0f, 60.0f, 50.0f, 3000.0f, 65236.0f, 50.0f, 200.0f, 1.0f, 270.0f, 10.0f, 3000.0f, 200.0f, 40.0f, 0.0f, 0.0f, 2, 3, 1, 113.0f, 45.0f, 200.0f, 60.0f, 56, {39,26,0}, {4,8,0}, {4,0,0}, {100,0,0}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {7,0,0}, {0,0,0}, {1,16,14,0}, {75.0f,50.0f,50.0f,0.0f}}, // 10
    {"Firefly", 300.0f, 120.0f, 1.0f, 60.0f, 50.0f, 3000.0f, 65386.0f, 50.0f, 200.0f, 2.0f, 270.0f, 9.0f, 3000.0f, 200.0f, 40.0f, 0.0f, 0.0f, 3, 3, 1, 19.0f, 8.0f, 200.0f, 30.0f, 184, {21,0,0}, {8,0,0}, {24,0,0}, {100,0,0}, {15,0,0}, {100,0,0}, {10,16,0}, {50,50,0}, {2,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 11
    {"Knight", 750.0f, 100.0f, 150.0f, 60.0f, 60.0f, 3000.0f, 65286.0f, 50.0f, 200.0f, 2.0f, 300.0f, 9.0f, 3000.0f, 100.0f, 20.0f, 0.0f, 0.0f, 2, 2, 1, 533.0f, 213.0f, 200.0f, 90.0f, 40, {0,0,0}, {0,0,0}, {4,6,0}, {95,5,0}, {9,0,0}, {100,0,0}, {16,0,0}, {100,0,0}, {16,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 12
    {"Dwarf", 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 65436.0f, 50.0f, 200.0f, 1.0f, 32767.0f, 0.0f, 3000.0f, 200.0f, 40.0f, 0.0f, 0.0f, 3, 3, 1, 50.0f, 20.0f, 200.0f, 15.0f, 41, {0,0,0}, {0,0,0}, {24,0,0}, {100,0,0}, {15,9,0}, {10,90,0}, {16,0,0}, {100,0,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 13
    {"Giant", 500.0f, 130.0f, 100.0f, 60.0f, 60.0f, 3000.0f, 64786.0f, 50.0f, 220.0f, 5.0f, 300.0f, 9.0f, 3000.0f, 150.0f, 30.0f, 0.0f, 150.0f, 2, 2, 1, 400.0f, 160.0f, 200.0f, 60.0f, 40, {0,0,0}, {0,0,0}, {5,4,0}, {50,50,0}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {10,4,0}, {0,25,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 14
    {"Wizard", 750.0f, 130.0f, 110.0f, 60.0f, 80.0f, 7000.0f, 65036.0f, 50.0f, 200.0f, 2.0f, 300.0f, 9.0f, 3000.0f, 150.0f, 30.0f, 10.0f, 0.0f, 1, 4, 1, 75.0f, 30.0f, 200.0f, 120.0f, 40, {25,27,28}, {1,4,8}, {3,8,0}, {90,10,0}, {15,9,0}, {10,90,0}, {16,0,0}, {100,0,0}, {6,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 15
    {"Elven Archer", 600.0f, 110.0f, 110.0f, 60.0f, 60.0f, 3000.0f, 65286.0f, 50.0f, 200.0f, 1.0f, 300.0f, 9.0f, 3000.0f, 150.0f, 30.0f, 0.0f, 0.0f, 2, 4, 1, 50.0f, 20.0f, 200.0f, 60.0f, 40, {24,30,9}, {1,4,8}, {6,0,0}, {100,0,0}, {15,9,0}, {10,90,0}, {16,0,0}, {100,0,0}, {9,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 16
    {"Thief", 750.0f, 55.0f, 120.0f, 100.0f, 60.0f, 3000.0f, 65036.0f, 50.0f, 200.0f, 2.0f, 300.0f, 9.0f, 3000.0f, 150.0f, 30.0f, 0.0f, 0.0f, 3, 3, 1, 113.0f, 45.0f, 200.0f, 50.0f, 40, {17,0,0}, {8,0,0}, {4,0,0}, {100,0,0}, {13,9,0}, {20,80,0}, {16,0,0}, {100,0,0}, {15,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 17
    {"Monk", 300.0f, 110.0f, 90.0f, 60.0f, 60.0f, 3000.0f, 65186.0f, 50.0f, 200.0f, 2.0f, 300.0f, 9.0f, 3000.0f, 150.0f, 30.0f, 3.0f, 0.0f, 1, 3, 1, 75.0f, 30.0f, 200.0f, 105.0f, 40, {32,13,15}, {1,4,8}, {8,0,0}, {100,0,0}, {15,9,0}, {10,90,0}, {16,0,0}, {100,0,0}, {14,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 18
    {"Fairy", 600.0f, 120.0f, 150.0f, 60.0f, 60.0f, 3000.0f, 65036.0f, 50.0f, 200.0f, 1.0f, 270.0f, 8.0f, 3000.0f, 150.0f, 30.0f, 5.0f, 20.0f, 2, 3, 1, 113.0f, 45.0f, 200.0f, 90.0f, 168, {34,15,21}, {4,8,10}, {3,24,0}, {95,5,0}, {15,9,0}, {10,90,0}, {16,0,0}, {100,0,0}, {13,0,0}, {0,0,0}, {30,32,0,0}, {75.0f,75.0f,0.0f,0.0f}}, // 19
    {"King Reginald", 1000.0f, 120.0f, 110.0f, 60.0f, 60.0f, 3000.0f, 32000.0f, 50.0f, 240.0f, 3.0f, 360.0f, 8.0f, 3000.0f, 75.0f, 15.0f, 0.0f, 0.0f, 2, 1, 1, 1400.0f, 560.0f, 200.0f, 250.0f, 44, {0,0,0}, {0,0,0}, {9,0,0}, {100,0,0}, {15,9,0}, {10,90,0}, {16,0,0}, {100,0,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 20
    {"Black Knight", 750.0f, 100.0f, 100.0f, 60.0f, 50.0f, 3000.0f, 65286.0f, 50.0f, 200.0f, 2.0f, 300.0f, 9.0f, 3000.0f, 100.0f, 20.0f, 0.0f, 0.0f, 2, 2, 1, 533.0f, 213.0f, 200.0f, 90.0f, 56, {0,0,0}, {0,0,0}, {4,6,0}, {95,5,0}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {16,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 21
    {"Dark Angel", 750.0f, 130.0f, 100.0f, 100.0f, 50.0f, 6000.0f, 65036.0f, 50.0f, 200.0f, 2.0f, 360.0f, 9.0f, 3000.0f, 100.0f, 20.0f, 3.0f, 0.0f, 2, 1, 1, 700.0f, 280.0f, 5000.0f, 200.0f, 56, {19,31,22}, {4,8,10}, {3,4,0}, {50,50,0}, {9,15,0}, {80,20,0}, {10,16,0}, {50,50,0}, {13,0,0}, {0,0,0}, {1,7,11,6}, {90.0f,90.0f,100.0f,90.0f}}, // 22
    {"Rogue", 650.0f, 140.0f, 100.0f, 100.0f, 50.0f, 3000.0f, 65036.0f, 50.0f, 200.0f, 2.0f, 300.0f, 9.0f, 3000.0f, 150.0f, 30.0f, 0.0f, 0.0f, 3, 3, 1, 190.0f, 80.0f, 200.0f, 42.0f, 56, {33,0,0}, {8,0,0}, {24,4,0}, {80,20,0}, {9,0,0}, {100,0,0}, {10,16,0}, {50,50,0}, {15,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 23
    {"Guard", 600.0f, 110.0f, 110.0f, 60.0f, 60.0f, 3000.0f, 65036.0f, 50.0f, 220.0f, 2.0f, 330.0f, 9.0f, 3000.0f, 150.0f, 30.0f, 0.0f, 30.0f, 2, 2, 1, 300.0f, 120.0f, 200.0f, 70.0f, 40, {0,0,0}, {0,0,0}, {6,4,0}, {90,10,0}, {15,9,0}, {10,90,0}, {16,0,0}, {100,0,0}, {7,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 24
    {"Prince Felix", 750.0f, 130.0f, 110.0f, 60.0f, 60.0f, 3000.0f, 1000.0f, 50.0f, 220.0f, 3.0f, 330.0f, 9.0f, 3000.0f, 75.0f, 15.0f, 0.0f, 0.0f, 2, 2, 1, 800.0f, 320.0f, 200.0f, 150.0f, 40, {0,0,0}, {0,0,0}, {5,9,0}, {95,5,0}, {9,0,0}, {100,0,0}, {16,0,0}, {100,0,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 25
    {"Horny", 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 8000.0f, 1000.0f, 50.0f, 32767.0f, 0.0f, 32767.0f, 0.0f, 3000.0f, 75.0f, 15.0f, 0.0f, 0.0f, 2, 1, 1, 1400.0f, 560.0f, 200.0f, 250.0f, 28, {38,0,0}, {1,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 26
    {"Stone Knight", 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 65286.0f, 50.0f, 32767.0f, 0.0f, 32767.0f, 0.0f, 3000.0f, 75.0f, 15.0f, 0.0f, 70.0f, 2, 2, 1, 400.0f, 160.0f, 0.0f, 150.0f, 12, {0,0,0}, {0,0,0}, {5,4,0}, {50,50,0}, {15,9,0}, {10,90,0}, {11,0,0}, {100,0,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 27
    {"Lord Of The Land", 750.0f, 70.0f, 130.0f, 60.0f, 60.0f, 3000.0f, 32000.0f, 50.0f, 220.0f, 3.0f, 330.0f, 9.0f, 3000.0f, 75.0f, 15.0f, 0.0f, 0.0f, 2, 2, 1, 800.0f, 320.0f, 200.0f, 150.0f, 40, {0,0,0}, {0,0,0}, {9,0,0}, {100,0,0}, {15,9,0}, {10,90,0}, {16,0,0}, {100,0,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 28
    {"Royal Guard", 750.0f, 120.0f, 140.0f, 60.0f, 60.0f, 3000.0f, 65036.0f, 50.0f, 220.0f, 2.0f, 300.0f, 9.0f, 3000.0f, 100.0f, 20.0f, 0.0f, 0.0f, 2, 2, 1, 400.0f, 160.0f, 200.0f, 85.0f, 40, {0,0,0}, {0,0,0}, {6,4,0}, {95,5,0}, {15,9,0}, {10,90,0}, {16,0,0}, {100,0,0}, {12,7,0}, {0,25,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 29
    {"Prince Balder", 750.0f, 90.0f, 130.0f, 60.0f, 60.0f, 3000.0f, 1000.0f, 50.0f, 220.0f, 3.0f, 330.0f, 9.0f, 3000.0f, 75.0f, 15.0f, 0.0f, 0.0f, 2, 2, 1, 800.0f, 320.0f, 200.0f, 150.0f, 40, {0,0,0}, {0,0,0}, {3,9,0}, {95,5,0}, {9,0,0}, {100,0,0}, {16,0,0}, {100,0,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 30
    {"Unique Firefly", 450.0f, 50.0f, 1.0f, 30.0f, 60.0f, 5000.0f, 65386.0f, 50.0f, 32767.0f, 0.0f, 270.0f, 9.0f, 2800.0f, 200.0f, 40.0f, 0.0f, 0.0f, 3, 3, 1, 19.0f, 15.0f, 200.0f, 30.0f, 248, {21,0,0}, {8,0,0}, {24,0,0}, {100,0,0}, {15,0,0}, {100,0,0}, {10,16,0}, {50,50,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 31
    {"Unique Goblin", 750.0f, 50.0f, 50.0f, 30.0f, 60.0f, 3000.0f, 65286.0f, 50.0f, 180.0f, 1.0f, 240.0f, 9.0f, 2800.0f, 200.0f, 40.0f, 0.0f, 0.0f, 2, 3, 1, 93.0f, 59.0f, 200.0f, 40.0f, 120, {0,0,0}, {0,0,0}, {4,0,0}, {100,0,0}, {9,0,0}, {100,0,0}, {10,16,0}, {50,50,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 32
    {"Unique Warlock", 900.0f, 50.0f, 50.0f, 30.0f, 70.0f, 10000.0f, 65036.0f, 50.0f, 200.0f, 2.0f, 300.0f, 9.0f, 2800.0f, 150.0f, 30.0f, 18.0f, 0.0f, 1, 4, 1, 50.0f, 20.0f, 200.0f, 85.0f, 120, {1,14,2}, {1,2,8}, {3,4,8}, {80,10,10}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 33
    {"Unique Troll", 750.0f, 50.0f, 50.0f, 30.0f, 50.0f, 3000.0f, 65336.0f, 50.0f, 200.0f, 3.0f, 330.0f, 10.0f, 2800.0f, 200.0f, 30.0f, 0.0f, 150.0f, 0, 2, 1, 50.0f, 20.0f, 200.0f, 55.0f, 120, {0,0,0}, {0,0,0}, {5,0,0}, {100,0,0}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 34
    {"Unique Dark Elf", 900.0f, 50.0f, 50.0f, 30.0f, 60.0f, 5000.0f, 65286.0f, 50.0f, 200.0f, 1.0f, 300.0f, 9.0f, 2800.0f, 150.0f, 30.0f, 0.0f, 0.0f, 2, 4, 1, 50.0f, 20.0f, 200.0f, 60.0f, 120, {12,18,7}, {1,4,8}, {6,4,0}, {80,20,0}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 35
    {"Unique Skeleton", 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 50.0f, 32767.0f, 0.0f, 32767.0f, 0.0f, 2800.0f, 200.0f, 40.0f, 0.0f, 0.0f, 2, 1, 1, 30000.0f, 60.0f, 0.0f, 30.0f, 92, {0,0,0}, {0,0,0}, {4,0,0}, {100,0,0}, {6,0,0}, {100,0,0}, {16,0,0}, {100,0,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 36
    {"Unique Mistress", 900.0f, 50.0f, 50.0f, 30.0f, 60.0f, 60536.0f, 500.0f, 50.0f, 200.0f, 2.0f, 300.0f, 9.0f, 2800.0f, 150.0f, 20.0f, 0.0f, 0.0f, 2, 1, 1, 267.0f, 107.0f, 200.0f, 110.0f, 120, {4,3,11}, {4,8,10}, {4,7,0}, {50,50,0}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 37
    {"Unique Salamander", 750.0f, 50.0f, 50.0f, 30.0f, 60.0f, 5000.0f, 65236.0f, 50.0f, 200.0f, 1.0f, 270.0f, 10.0f, 2800.0f, 200.0f, 40.0f, 0.0f, 0.0f, 2, 3, 1, 113.0f, 45.0f, 200.0f, 60.0f, 120, {39,26,0}, {4,8,0}, {4,0,0}, {100,0,0}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 38
    {"Unique Rogue", 975.0f, 50.0f, 50.0f, 30.0f, 60.0f, 5000.0f, 65036.0f, 50.0f, 200.0f, 2.0f, 300.0f, 9.0f, 2800.0f, 150.0f, 30.0f, 0.0f, 0.0f, 3, 3, 1, 190.0f, 80.0f, 200.0f, 42.0f, 120, {33,0,0}, {8,0,0}, {24,4,0}, {80,20,0}, {9,0,0}, {100,0,0}, {10,16,0}, {50,50,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 39
    {"Unique Bile Demon", 900.0f, 50.0f, 50.0f, 30.0f, 60.0f, 5000.0f, 65236.0f, 50.0f, 220.0f, 5.0f, 270.0f, 9.0f, 2800.0f, 150.0f, 20.0f, 0.0f, 120.0f, 2, 2, 1, 400.0f, 200.0f, 200.0f, 50.0f, 120, {5,6,0}, {4,8,0}, {4,5,0}, {40,60,0}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 40
    {"Unique Vampire", 1075.0f, 50.0f, 1.0f, 30.0f, 60.0f, 10000.0f, 65286.0f, 50.0f, 32767.0f, 0.0f, 330.0f, 9.0f, 2800.0f, 150.0f, 30.0f, 15.0f, 0.0f, 1, 1, 1, 267.0f, 107.0f, 0.0f, 95.0f, 88, {8,10,20}, {4,8,10}, {3,8,0}, {80,20,0}, {15,9,0}, {10,90,0}, {16,0,0}, {100,0,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 41
    {"Unique Black Knight", 1075.0f, 50.0f, 50.0f, 30.0f, 60.0f, 5000.0f, 65286.0f, 50.0f, 200.0f, 2.0f, 300.0f, 9.0f, 2800.0f, 100.0f, 20.0f, 0.0f, 0.0f, 2, 1, 1, 533.0f, 213.0f, 200.0f, 90.0f, 120, {0,0,0}, {0,0,0}, {4,6,0}, {95,5,0}, {15,9,0}, {10,90,0}, {10,16,0}, {50,50,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 42
    {"Unique Dark Angel", 1075.0f, 50.0f, 50.0f, 30.0f, 60.0f, 10000.0f, 65036.0f, 50.0f, 200.0f, 2.0f, 360.0f, 9.0f, 2800.0f, 100.0f, 20.0f, 15.0f, 0.0f, 2, 1, 1, 700.0f, 280.0f, 500.0f, 200.0f, 120, {19,31,22}, {4,8,10}, {3,4,0}, {50,50,0}, {9,15,0}, {20,80,0}, {10,16,0}, {50,50,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 43
    {"Maiden Of The Nest", 750.0f, 150.0f, 120.0f, 60.0f, 50.0f, 3000.0f, 65236.0f, 50.0f, 200.0f, 3.0f, 240.0f, 8.0f, 3000.0f, 100.0f, 25.0f, 4.0f, 0.0f, 1, 2, 1, 30000.0f, 200.0f, 200.0f, 80.0f, 60, {40,41,0}, {4,8,0}, {3,0,0}, {100,0,0}, {9,0,0}, {100,0,0}, {10,0,0}, {100,0,0}, {6,13,0}, {0,9,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 44
    {"Jack", 600.0f, 120.0f, 110.0f, 60.0f, 50.0f, 60536.0f, 500.0f, 50.0f, 200.0f, 2.0f, 300.0f, 9.0f, 3000.0f, 100.0f, 20.0f, 0.0f, 0.0f, 0, 0, 1, 267.0f, 107.0f, 200.0f, 110.0f, 20, {0,0,0}, {0,0,0}, {24,0,0}, {100,0,0}, {6,0,0}, {100,0,0}, {16,0,0}, {100,0,0}, {0,0,0}, {0,0,0}, {0,0,0,0}, {0.0f,0.0f,0.0f,0.0f}}, // 45
};
static const unsigned kDK2CreatureAIStatCount =
    sizeof(kDK2CreatureAIStats) / sizeof(kDK2CreatureAIStats[0]);

// CreatureSpells.kwd range / recharge indexed by creature_spell_id.
static const float kCreatureSpellRange[42] = {
    0.00f, 4.00f, 4.00f, 3.00f, 3.00f, 1.00f, 4.00f, 5.00f, 3.00f, 4.00f, 3.00f, 4.00f, 5.00f, 3.00f, 3.00f, 3.00f, 3.00f, 1.00f, 2.00f, 3.00f, 3.00f,
    1.00f, 1.00f, 3.00f, 5.00f, 5.00f, 4.00f, 4.00f, 4.00f, 3.00f, 5.00f, 3.00f, 3.00f, 1.00f, 3.00f, 4.00f, 0.00f, 0.00f, 5.00f, 3.00f, 4.00f, 3.00f
};
static const float kCreatureSpellRecharge[42] = {
    5.00f, 5.00f, 7.50f, 15.00f, 13.00f, 5.00f, 5.00f, 3.50f, 13.00f, 7.50f, 15.00f, 18.00f, 5.00f, 7.50f, 2.50f, 5.00f, 7.50f, 5.00f, 7.50f, 16.00f, 17.00f,
    10.00f, 15.00f, 3.50f, 2.00f, 5.00f, 5.00f, 7.50f, 10.00f, 5.00f, 3.50f, 15.00f, 2.50f, 7.50f, 7.50f, 5.00f, 5.00f, 5.00f, 7.00f, 5.00f, 25.00f, 15.00f
};
}  // namespace

static const DK2CreatureAIStat* ai_stat_for(int species) {
    if (species < 0 || species >= static_cast<int>(kDK2CreatureAIStatCount))
        return nullptr;
    return &kDK2CreatureAIStats[species];
}

static uint32_t mix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Map the original KWD job ids (Creatures.kwd happy/unhappy/angry_jobs) onto
// the engine job enum.  Ids with no direct original analogue map to the
// closest utility state (9=Sulk, 15=Anger, 16=Rebel, 24=worker haul).
static CreatureJob job_from_original_id(int id) {
    switch (id) {
        case 3:  return CreatureJob::Research;
        case 4:  return CreatureJob::Guard;
        case 5:  return CreatureJob::Manufacture;
        case 6:  return CreatureJob::Train;
        case 7:  return CreatureJob::Torture;
        case 8:  return CreatureJob::Pray;
        case 9:  return CreatureJob::Sulk;
        case 10: return CreatureJob::Rebel;
        case 11: return CreatureJob::Fight;
        case 13: return CreatureJob::Explore;
        case 15: return CreatureJob::Anger;
        case 16: return CreatureJob::Rebel;
        case 24: return CreatureJob::HaulGold;
        default: return CreatureJob::Idle;
    }
}

static int original_job_id(CreatureJob job) {
    switch (job) {
        case CreatureJob::Sleep: return 2;
        case CreatureJob::Eat: return 1;
        case CreatureJob::Mine: return 24;
        case CreatureJob::HaulGold: return 24;
        case CreatureJob::Train: return 6;
        case CreatureJob::Research: return 3;
        case CreatureJob::Guard: return 4;
        case CreatureJob::Fight: return 11;
        case CreatureJob::Flee: return 12;
        case CreatureJob::Pay: return 17;
        case CreatureJob::Sulk: return 9;
        case CreatureJob::Rebel: return 16;
        case CreatureJob::Pray: return 8;
        case CreatureJob::Manufacture: return 5;
        case CreatureJob::Explore: return 13;
        case CreatureJob::Tunnels: return 14;
        default: return 0;
    }
}

static float anger_total(const Creature& c) {
    float total = 0.0f;
    for (float v : c.anger_values) total += v;
    return total;
}

// Resistances.kwd-style damage reduction from the creature's recovered
// resist_type/resist_value pairs (attack type -> percent).
static float resistance_factor(const Creature& target, int attack_type) {
    for (int i = 0; i < 4; ++i) {
        if (target.resist_type[i] == attack_type && target.resist_value[i] > 0.0f)
            return std::max(0.0f, 1.0f - std::min(100.0f, target.resist_value[i]) / 100.0f);
    }
    return 1.0f;
}

static CreatureState physical_state(CreatureJob job) {
    switch (job) {
        case CreatureJob::Sleep: return CreatureState::Sleeping;
        case CreatureJob::Eat: return CreatureState::Eating;
        case CreatureJob::Mine: case CreatureJob::Tunnels: return CreatureState::Mining;
        case CreatureJob::Fight: return CreatureState::Fighting;
        case CreatureJob::Flee: return CreatureState::Fleeing;
        case CreatureJob::Pay: return CreatureState::Paying;
        case CreatureJob::Sulk: case CreatureJob::Anger: return CreatureState::Sulking;
        case CreatureJob::Rebel: return CreatureState::Rebelling;
        case CreatureJob::Pray: return CreatureState::Praying;
        case CreatureJob::Manufacture: return CreatureState::Manufacturing;
        case CreatureJob::Guard: return CreatureState::Guarding;
        case CreatureJob::Explore: return CreatureState::Exploring;
        default: return CreatureState::Idle;
    }
}

static void set_job(Creature& c, CreatureJob job, float now) {
    if (!c.path.empty()) {
        c.return_job_id = c.current_job_id;
    }
    if (c.job != job) {
        c.state_started_at = now;
        c.state_age = 0.0f;
        c.state_id = original_job_id(job);
    }
    c.job = job;
    c.current_job_id = original_job_id(job);
    if (c.state != CreatureState::Walking && c.state != CreatureState::Dead)
        c.state = physical_state(job);
}

static int room_tiles_of_type(const GameState& s, int type) {
    int n = 0;
    for (const Room& r : s.rooms)
        if (r.type == type) ++n;
    for (const Tile& t : s.world.tiles)
        if (static_cast<int>(t.terrain) == type) ++n;
    return n;
}

static bool room_tile_for_job(CreatureJob job, int& type) {
    switch (job) {
        case CreatureJob::Eat:        type = int(TerrainId::Hatchery); return true;
        case CreatureJob::Sleep:
        case CreatureJob::Lair:       type = int(TerrainId::Lair); return true;
        case CreatureJob::Train:      type = int(TerrainId::TrainingRoom); return true;
        case CreatureJob::Research:   type = int(TerrainId::Library); return true;
        case CreatureJob::HaulGold:
        case CreatureJob::Pay:        type = int(TerrainId::Treasury); return true;
        case CreatureJob::Manufacture:type = int(TerrainId::Workshop); return true;
        case CreatureJob::Pray:       type = int(TerrainId::Temple); return true;
        case CreatureJob::Guard:      type = int(TerrainId::GuardRoom); return true;
        case CreatureJob::Torture:    type = int(TerrainId::TortureChamber); return true;
        case CreatureJob::Prison:     type = int(TerrainId::Prison); return true;
        default: return false;
    }
}

const char* terrain_name(unsigned id) {
    return id < sizeof(kTerrainNames) / sizeof(kTerrainNames[0])
               ? kTerrainNames[id] : kTerrainFallback;
}

bool terrain_walkable(TerrainId id) {
    switch (id) {
        case TerrainId::DirtPath:
        case TerrainId::Gold:
        case TerrainId::Gems:
        case TerrainId::ClaimedPath:
        case TerrainId::Treasury:
        case TerrainId::Lair:
        case TerrainId::Portal:
        case TerrainId::Hatchery:
        case TerrainId::DungeonHeart:
        case TerrainId::Library:
        case TerrainId::TrainingRoom:
        case TerrainId::WoodenBridge:
        case TerrainId::GuardRoom:
        case TerrainId::Workshop:
        case TerrainId::Prison:
        case TerrainId::TortureChamber:
        case TerrainId::Graveyard:
        case TerrainId::Temple:
        case TerrainId::ScavengerRoom:
        case TerrainId::BehemothPit:
            return true;
        default:
            return false;
    }
}

bool terrain_is_room(TerrainId id) {
    unsigned v = static_cast<unsigned>(id);
    return v >= static_cast<unsigned>(TerrainId::Treasury) &&
           v <= static_cast<unsigned>(TerrainId::BehemothPit);
}

bool World::load_binary(const uint8_t* data, size_t size, std::string* error) {
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    if (!data || size < 16) return fail("map too short");
    uint32_t magic = uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
                     (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
    if (magic != kMapMagic) return fail("bad map magic");
    uint32_t version, w, h;
    std::memcpy(&version, data + 4, 4);
    std::memcpy(&w, data + 8, 4);
    std::memcpy(&h, data + 12, 4);
    if (version != 1 || w == 0 || h == 0 || w > 512 || h > 512)
        return fail("unsupported map header");
    const size_t need = 16 + size_t(w) * h * 4;
    if (size < need) return fail("truncated tile array");
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    tiles.resize(size_t(w) * h);
    for (size_t i = 0; i < tiles.size(); ++i) {
        uint32_t word;
        std::memcpy(&word, data + 16 + i * 4, 4);
        tiles[i] = Tile::from_word(word);
    }
    return true;
}

void Economy::recalculate(const World& world) {
    float cap = 0;
    int count = 0;
    for (const Tile& t : world.tiles) {
        if (t.terrain == static_cast<uint8_t>(TerrainId::Treasury)) {
            ++count;
            cap += 4000.0f;
        }
    }
    treasures = count;
    gold_capacity = cap;
    if (gold > gold_capacity) gold = gold_capacity;
}

float Economy::deposit(float amount) {
    if (amount <= 0) return 0;
    float accepted = std::min(amount, std::max(0.0f, gold_capacity - gold));
    gold += accepted;
    return accepted;
}

bool Economy::withdraw(float amount) {
    if (amount < 0 || amount > gold) return false;
    gold -= amount;
    return true;
}

void CameraState::clamp(const World& world) {
    zoom = std::max(0.35f, std::min(3.0f, zoom));
    pitch = std::max(0.15f, std::min(1.45f, pitch));
    if (world.width > 0 && world.height > 0) {
        center_x = std::max(0.0f, std::min(float(world.width - 1), center_x));
        center_y = std::max(0.0f, std::min(float(world.height - 1), center_y));
    }
}

Game::Game() {
    state_.camera.center_x = 24;
    state_.camera.center_y = 24;
}

bool Game::load_map(const uint8_t* data, size_t size, std::string* error) {
    if (!state_.world.load_binary(data, size, error)) return false;
    state_.creatures.clear();
    state_.rooms.clear();
    state_.elapsed = 0;
    state_.tick_count = 0;
    state_.victory = state_.defeat = false;
    next_creature_id_ = 1;
    fired_actions_.clear();
    initial_enemies_spawned_ = false;
    pay_period_timer_ = 0.0f;
    ai_rng_ = 0x51ed270bU;
    grid_.reset(new GridSimulation(state_.world.width, state_.world.height));
    grid_accumulator_ = 0;
    state_.economy.gold = 0;
    state_.economy.recalculate(state_.world);
    state_.mana = 1000.0f;
    state_.research.assign(kDK2SpellCount, 0.0f);
    state_.cooldowns.assign(kDK2SpellCount, 0.0f);
    state_.spells_known.assign(kDK2SpellCount, 0);
    // Sight Of Evil and Possession start known, as in the original campaign.
    if (kDK2SpellCount > 0) state_.spells_known[0] = 1;
    if (kDK2SpellCount > 1) state_.spells_known[1] = 1;
    state_.camera.center_x = state_.world.width * 0.5f;
    state_.camera.center_y = state_.world.height * 0.5f;
    state_.camera.clamp(state_.world);
    return true;
}

void Game::load_catalog(const std::vector<std::string>& creatures,
                        const std::vector<std::string>& rooms,
                        const std::vector<std::string>& spells) {
    state_.known_creatures = creatures;
    state_.known_rooms = rooms;
    state_.known_spells = spells;
}

void Game::set_mission(const std::string& name) {
    state_.mission_name = name;
    uint32_t seed = 0x9e3779b9U;
    for (char ch : name) seed = mix32(seed ^ uint32_t(static_cast<uint8_t>(ch)));
    ai_rng_ = seed;
}

int Game::spawn_creature(int species, int x, int y, int team) {
    if (state_.creatures.size() >= kMaxCreatures) return -1;
    if (!state_.world.walkable(x, y)) return -1;
    Creature c;
    c.id = next_creature_id_++;
    c.species = species;
    c.team = team;
    c.x = float(x) + 0.5f;
    c.y = float(y) + 0.5f;
    c.max_hp = 100.0f;
    c.damage = state_.config.attack_per_second;
    c.speed = state_.config.creature_speed;
    if (species >= 0 && species < static_cast<int>(kDK2CreatureStatCount)) {
        const DK2CreatureStat& s = kDK2CreatureStats[species];
        c.max_hp = float(s.hp);
        c.damage = s.melee_damage;
        c.attack_cooldown = s.melee_recharge > 0.0f ? s.melee_recharge : 1.2f;
        c.melee_recharge = c.attack_cooldown;
        c.speed = s.speed;
        c.sight = s.sight_range;
        c.melee_range = std::max(0.5f, s.melee_range * 4.0f);
        c.fear = s.fear;
        c.max_gold = float(s.max_gold_held);
    }
    const DK2CreatureAIStat* ai = ai_stat_for(species);
    if (ai) {
        c.pay = ai->pay;
        c.anger_no_lair = ai->anger_no_lair;
        c.anger_no_food = ai->anger_no_food;
        c.anger_no_pay = ai->anger_no_pay;
        c.anger_no_work = ai->anger_no_work;
        c.anger_slap = ai->anger_slap;
        c.anger_in_hand = ai->anger_in_hand;
        c.unhappy_threshold = ai->unhappy_threshold;
        c.hunger_rate = ai->hunger_rate;
        c.hunger_fill = ai->hunger_fill;
        c.time_awake = ai->time_awake;
        c.time_sleep = ai->time_sleep;
        c.exp_for_next_level = ai->exp_for_next_level > 0.0f
                                   ? ai->exp_for_next_level : 3000.0f;
        c.exp_per_second = ai->exp_per_second;
        c.exp_per_second_training = ai->exp_per_second_training;
        c.research_per_second = ai->research_per_second;
        c.manufacture_per_second = ai->manufacture_per_second;
        c.mana_gen_prayer = ai->mana_gen_prayer;
        c.job_class = ai->job_class;
        c.fight_style = ai->fight_style;
        c.room_capacity_needed = ai->room_capacity_needed > 0 ? ai->room_capacity_needed : 1;
        c.threat = ai->threat;
        c.decompose_value = ai->decompose_value;
        c.ai_flags = ai->flags;
        c.threat_threshold = c.fear;
        for (int i = 0; i < 3; ++i) {
            c.spell_id[i] = ai->spell_id[i] > 0 ? ai->spell_id[i] : -1;
            c.spell_level[i] = ai->spell_level[i];
            c.happy_job[i] = ai->happy_job[i];
            c.happy_chance[i] = ai->happy_chance[i];
            c.unhappy_job[i] = ai->unhappy_job[i];
            c.unhappy_chance[i] = ai->unhappy_chance[i];
            c.angry_job[i] = ai->angry_job[i];
            c.angry_chance[i] = ai->angry_chance[i];
            c.attract_room[i] = ai->attract_room[i];
            c.attract_size[i] = ai->attract_size[i];
        }
        for (int i = 0; i < 4; ++i) {
            c.resist_type[i] = ai->resist_type[i];
            c.resist_value[i] = ai->resist_value[i];
        }
    }
    c.hp = c.max_hp;
    c.state_started_at = state_.elapsed;
    state_.creatures.push_back(c);
    return c.id;
}

bool Game::build_room(int type, int x, int y, int owner) {
    Tile* tile = state_.world.at(x, y);
    if (!tile) return false;
    if (!terrain_walkable(static_cast<TerrainId>(tile->terrain))) return false;
    for (const Room& r : state_.rooms)
        if (r.x == x && r.y == y) return false;
    Room room;
    room.x = x;
    room.y = y;
    room.owner = owner;
    TerrainId terrain = TerrainId::Treasury;
    int cost = -1;
    if (type >= 0 && type < static_cast<int>(state_.known_rooms.size())) {
        const std::string& wanted = state_.known_rooms[static_cast<size_t>(type)];
        for (unsigned i = 0; i < sizeof(kTerrainNames) / sizeof(kTerrainNames[0]); ++i) {
            std::string a = wanted, b = kTerrainNames[i];
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            if (a == b) { terrain = static_cast<TerrainId>(i); break; }
        }
        for (size_t i = 0; i < kDK2RoomCostCount; ++i) {
            std::string a = wanted, b = kDK2RoomCosts[i].name;
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            if (a == b) { cost = kDK2RoomCosts[i].gold; break; }
        }
    }
    // Costs are quoted from the shipped manual; -1 means "not documented".
    if (cost > 0 && !state_.economy.withdraw(float(cost))) return false;
    room.type = static_cast<int>(terrain);  // consumers compare TerrainId
    state_.rooms.push_back(room);
    tile->terrain = static_cast<uint8_t>(terrain);
    tile->owner = static_cast<uint8_t>(owner);
    state_.economy.recalculate(state_.world);
    return true;
}

bool Game::place_trap(int index, int x, int y, int owner) {
    const DK2TrapDef* def = structures::trap_by_index(static_cast<size_t>(index));
    if (!def) return false;
    Tile* tile = state_.world.at(x, y);
    if (!tile || !terrain_walkable(static_cast<TerrainId>(tile->terrain))) return false;
    structures::BuildCost cost = structures::trap_build_cost(def);
    if (cost.gold > 0 && !state_.economy.withdraw(float(cost.gold))) return false;
    if (cost.mana > 0 && state_.mana < float(cost.mana)) return false;
    if (cost.mana > 0) state_.mana -= float(cost.mana);
    Room trap;
    trap.type = 1000 + index;   // distinct from room TerrainId
    trap.x = x; trap.y = y; trap.owner = owner;
    trap.integrity = float(structures::trap_health(def));
    state_.rooms.push_back(trap);
    return true;
}

bool Game::place_door(int index, int x, int y, int owner) {
    const DK2DoorDef* def = structures::door_by_index(static_cast<size_t>(index));
    if (!def) return false;
    Tile* tile = state_.world.at(x, y);
    if (!tile || !terrain_walkable(static_cast<TerrainId>(tile->terrain))) return false;
    structures::BuildCost cost = structures::door_build_cost(def);
    if (cost.gold > 0 && !state_.economy.withdraw(float(cost.gold))) return false;
    if (cost.mana > 0 && state_.mana < float(cost.mana)) return false;
    if (cost.mana > 0) state_.mana -= float(cost.mana);
    Room door;
    door.type = 2000 + index;
    door.x = x; door.y = y; door.owner = owner;
    door.integrity = float(structures::door_health(def));
    state_.rooms.push_back(door);
    return true;
}

bool Game::cast_spell(int spell_index, int x, int y) {
    if (spell_index < 0 || spell_index >= static_cast<int>(kDK2SpellCount))
        return false;
    if (spell_index >= static_cast<int>(state_.spells_known.size()) ||
        !state_.spells_known[spell_index]) return false;
    if (spell_index < static_cast<int>(state_.cooldowns.size()) &&
        state_.cooldowns[spell_index] > 0.0f) return false;
    const DK2SpellData& spell = kDK2Spells[spell_index];
    if (state_.mana < float(spell.mana_cost)) return false;

    Tile* tile = state_.world.at(x, y);
    if (!tile) return false;
    state_.mana -= float(spell.mana_cost);
    if (spell_index < static_cast<int>(state_.cooldowns.size()))
        state_.cooldowns[spell_index] = spell.recharge_time;

    /* Effects use the real spell id from KeeperSpells.kwd. */
    switch (spell.id) {
        case 1:  /* Sight Of Evil - reveal a 5x5 area */
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    if (Tile* t = state_.world.at(x + dx, y + dy))
                        t->flags |= 0x80;
            break;
        case 4:  /* Create Imp */
            spawn_creature(0, x, y, 0);
            break;
        case 10: /* Heal - restore a friendly creature nearby */
            for (Creature& c : state_.creatures) {
                if (c.team != 0 || c.state == CreatureState::Dead) continue;
                if (std::fabs(c.x - (x + 0.5f)) < 2.0f &&
                    std::fabs(c.y - (y + 0.5f)) < 2.0f) {
                    c.hp = std::min(c.max_hp, c.hp + c.max_hp * 0.5f);
                }
            }
            break;
        case 6:  /* Thunderbolt - damage nearby enemies */
        case 9:  /* Inferno */
        case 8:  /* Tremor */
            for (Creature& c : state_.creatures) {
                if (c.team == 0 || c.state == CreatureState::Dead) continue;
                if (std::fabs(c.x - (x + 0.5f)) < 3.0f &&
                    std::fabs(c.y - (y + 0.5f)) < 3.0f)
                    c.hp -= c.max_hp * (spell.id == 9 ? 0.8f : 0.4f);
                if (c.hp <= 0) { c.hp = 0; c.state = CreatureState::Dead; }
            }
            break;
        case 11: /* Chicken - cripple an enemy */
            for (Creature& c : state_.creatures) {
                if (c.team != 0 && std::fabs(c.x - x) < 2.0f &&
                    std::fabs(c.y - y) < 2.0f) {
                    c.hp = std::min(c.hp, c.max_hp * 0.25f);
                    c.speed *= 0.5f;
                }
            }
            break;
        case 13: /* Create Gold */
            state_.economy.deposit(1000.0f);
            break;
        case 3:  /* Call To Arms - raise friendly mood, cancel rebellion */
            for (Creature& c : state_.creatures) {
                if (c.team != 0) continue;
                c.mood = std::min(100.0f, c.mood + 20.0f);
                for (float& a : c.anger_values) a = std::max(0.0f, a - 20.0f);
                c.anger_state = 0;
                c.call_to_arms_on = true;
                c.fearless_until = state_.elapsed + 30.0f;
                if (anger_total(c) < c.unhappy_threshold) c.rebel_active = false;
            }
            break;
        default: break;
    }
    return true;
}

bool Game::research_spell(int spell_index) {
    if (spell_index < 0 || spell_index >= static_cast<int>(kDK2SpellCount))
        return false;
    if (spell_index >= static_cast<int>(state_.spells_known.size()) ||
        state_.spells_known[spell_index]) return false;
    int libraries = 0;
    for (const Room& r : state_.rooms)
        if (r.type == static_cast<int>(TerrainId::Library)) ++libraries;
    if (libraries == 0 || static_cast<int>(state_.research.size()) <= spell_index)
        return false;
    state_.research[spell_index] += kTickSeconds * float(libraries);
    if (state_.research[spell_index] >= kDK2Spells[spell_index].research_time) {
        state_.spells_known[spell_index] = 1;
        return true;
    }
    return false;
}

void Game::update_magic() {
    int hearts = 0;
    for (const Tile& t : state_.world.tiles)
        if (t.terrain == static_cast<uint8_t>(TerrainId::DungeonHeart)) ++hearts;
    state_.mana = std::min(state_.mana_max,
                           state_.mana + 20.0f * kTickSeconds * float(1 + hearts));
    for (float& cd : state_.cooldowns)
        if (cd > 0.0f) cd = std::max(0.0f, cd - kTickSeconds);
    for (unsigned i = 0; i < kDK2SpellCount; ++i)
        if (i < state_.spells_known.size() && !state_.spells_known[i])
            research_spell(static_cast<int>(i));
}

bool Game::find_path(int sx, int sy, int tx, int ty, std::vector<int>& out) const {
    out.clear();
    const World& world = state_.world;
    if (!world.walkable(sx, sy) || !world.walkable(tx, ty)) return false;
    const int n = world.width * world.height;
    const int start = world.index(sx, sy), goal = world.index(tx, ty);
    if (start == goal) return true;
    std::vector<float> g(n, std::numeric_limits<float>::max());
    std::vector<int> parent(n, -1);
    std::vector<uint8_t> closed(n, 0);
    auto heuristic = [&](int a) {
        int ax = a % world.width, ay = a / world.width;
        int dx = std::abs(ax - tx), dy = std::abs(ay - ty);
        return float(dx + dy);
    };
    using Node = std::pair<float, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    g[start] = 0;
    open.push({heuristic(start), start});
    int expanded = 0;
    while (!open.empty() && expanded++ < kMaxPathNodes) {
        int cur = open.top().second;
        open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        if (cur == goal) break;
        int cx = cur % world.width, cy = cur / world.width;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                if (!world.walkable(cx + dx, cy + dy)) continue;
                if (dx && dy && (!world.walkable(cx + dx, cy) ||
                                 !world.walkable(cx, cy + dy)))
                    continue;
                int nb = world.index(cx + dx, cy + dy);
                float step = (dx && dy) ? 1.41421356f : 1.0f;
                float ng = g[cur] + step;
                if (ng < g[nb]) {
                    g[nb] = ng;
                    parent[nb] = cur;
                    open.push({ng + heuristic(nb), nb});
                }
            }
        }
    }
    if (parent[goal] < 0) return false;
    std::vector<int> rev;
    for (int cur = goal; cur != start && cur >= 0; cur = parent[cur])
        rev.push_back(cur);
    std::reverse(rev.begin(), rev.end());
    out = std::move(rev);
    return true;
}

static bool find_room_tile(const std::vector<Room>& rooms,
                           int type, int from_x, int from_y,
                           int& out_x, int& out_y) {
    long best = -1;
    int bx = -1, by = -1;
    for (const Room& room : rooms) {
        if (room.type != type) continue;
        long d = (long)(room.x - from_x) * (room.x - from_x) +
                 (long)(room.y - from_y) * (room.y - from_y);
        if (best < 0 || d < best) { best = d; bx = room.x; by = room.y; }
    }
    if (bx < 0) return false;
    out_x = bx; out_y = by;
    return true;
}

void Game::update_creature(Creature& c) {
    const float dt = kTickSeconds;
    if (c.state == CreatureState::Dead || c.state == CreatureState::Decomposing) {
        c.death_timer += dt;
        const float decay = c.decompose_value > 0.0f
                                ? std::max(4.0f, c.decompose_value / 50.0f) : 12.0f;
        if (c.state != CreatureState::Decomposing && c.death_timer >= decay)
            c.state = CreatureState::Decomposing;
        return;
    }
    if (c.state == CreatureState::InHand) {
        apply_anger(c, c.anger_in_hand * 0.1f, 5);
        return;
    }
    const World& world = state_.world;
    if (world.width <= 0 || world.height <= 0) return;
    int cx = std::max(0, std::min(world.width - 1, int(c.x)));
    int cy = std::max(0, std::min(world.height - 1, int(c.y)));

    c.state_age += dt;
    c.state_tick_last_updated = state_.elapsed;
    c.time_awake_elapsed += dt * 30.0f;
    if (c.attack_cooldown > 0.0f) c.attack_cooldown -= dt;
    if (c.creature_spell_cooldown > 0.0f) c.creature_spell_cooldown -= dt;
    if (c.backoff_timer > 0.0f) c.backoff_timer -= dt;
    c.current_fear = std::max(0.0f, c.current_fear - 0.5f * dt);

    auto gain_xp = [&](float amount) {
        if (amount <= 0.0f) return;
        c.xp += amount;
        while (c.xp >= c.exp_for_next_level) {
            c.xp -= c.exp_for_next_level;
            ++c.level;
            c.max_hp *= 1.05f;
            c.hp = std::min(c.max_hp, c.hp + c.max_hp * 0.05f);
            c.mood = std::min(100.0f, c.mood + 5.0f);
        }
    };

    /* --- payday (PayState.cpp / FUN_00472390) --------------------------- */
    c.pay_timer += dt;
    if (c.pay_timer >= 10.0f) {
        c.pay_timer = 0.0f;
        if (c.team == 0 && c.pay > 0.0f) {
            if (c.skip_next_payment) {
                c.skip_next_payment = false;
                c.get_paid = true;
            } else if (state_.economy.withdraw(c.pay)) {
                c.get_paid = true;
                c.payment_needed = 0.0f;
                c.mood = std::min(100.0f, c.mood + 2.0f);
            } else {
                c.get_paid = false;
                c.payment_needed += c.pay;
                ++c.payment_attempts;
                apply_anger(c, c.anger_no_pay, 2);
                if (c.path.empty()) set_job(c, CreatureJob::Pay, state_.elapsed);
                if (c.payment_attempts >= 3) {
                    c.rebel_active = true;
                    apply_anger(c, c.anger_no_pay * 2.0f, 4);
                }
            }
        } else {
            c.get_paid = true;  // heroes are paid by their own portal
        }
    }

    /* --- needs ---------------------------------------------------------- */
    // The KWD field is a period in ticks: 32767 means "never gets hungry".
    if (c.hunger_rate > 0.0f)
        c.hunger = std::min(100.0f,
                            c.hunger + std::min(10.0f, 1000.0f / c.hunger_rate) * dt);
    else
        c.hunger = std::min(100.0f, c.hunger + state_.config.hunger_rate * dt);
    c.fatigue = std::min(100.0f, c.fatigue + state_.config.fatigue_rate * dt);
    if (c.time_awake > 0.0f && c.time_awake_elapsed > c.time_awake)
        c.sleep_need = std::min(100.0f, c.sleep_need + 2.0f * dt);
    if (c.hunger > 70.0f || c.fatigue > 70.0f || c.sleep_need > 70.0f)
        apply_anger(c, 0.01f, 1);
    else
        c.mood = std::min(100.0f, c.mood + 0.2f * dt);

    /* --- lair / room capacity happiness (LairId + LairAvailability) ----- */
    c.lair_check_timer += dt;
    if (c.lair_check_timer >= 2.0f) {
        c.lair_check_timer = 0.0f;
        int team_size = 0, lairs = 0, hatcheries = 0, workplaces = 0;
        for (const Creature& other : state_.creatures)
            if (other.team == c.team && other.state != CreatureState::Dead) ++team_size;
        lairs = room_tiles_of_type(state_, int(TerrainId::Lair));
        hatcheries = room_tiles_of_type(state_, int(TerrainId::Hatchery));
        workplaces = room_tiles_of_type(state_, int(TerrainId::TrainingRoom)) +
                     room_tiles_of_type(state_, int(TerrainId::Library)) +
                     room_tiles_of_type(state_, int(TerrainId::Workshop));
        c.lair_availability = lairs * std::max(1, c.room_capacity_needed) >= team_size;
        c.lair_calculated = true;
        if (!c.lair_availability) apply_anger(c, c.anger_no_lair * 0.2f, 0);
        for (int i = 0; i < 3; ++i) {
            if (c.attract_room[i] <= 0) continue;
            const DK2RoomDef* def = structures::room_by_id(c.attract_room[i]);
            if (!def) continue;
            int engine = structures::stats_terrain_to_engine_id(
                structures::room_terrain_id(def));
            if (engine < 0) continue;
            int tiles = room_tiles_of_type(state_, engine);
            if (tiles < std::max(1, c.attract_size[i]))
                apply_anger(c, c.anger_no_lair * 0.05f, 0);
        }
        if (hatcheries == 0) apply_anger(c, c.anger_no_food * 0.2f, 1);
        if (workplaces == 0) apply_anger(c, c.anger_no_work * 0.2f, 3);
    }

    /* --- combat: target selection and fear (FUN_0045cb00/FUN_004915d0) --- */
    Creature* enemy = nullptr;
    float best_d2 = c.sight * c.sight;
    if (c.attacking_id >= 0) {
        for (Creature& other : state_.creatures) {
            if (other.id != c.attacking_id || other.state == CreatureState::Dead) continue;
            float dx = other.x - c.x, dy = other.y - c.y;
            if (dx * dx + dy * dy <= c.sight * c.sight) {
                enemy = &other;
                best_d2 = dx * dx + dy * dy;
            } else {
                c.attacking_id = -1;
            }
            break;
        }
    }
    if (!enemy) {
        for (Creature& other : state_.creatures) {
            if (other.team == c.team || other.state == CreatureState::Dead) continue;
            // Guard checks from SelectAVictim: a fighter already attacking,
            // a creature held in hand, or a possessed creature is skipped.
            if (c.state == CreatureState::InHand ||
                (c.job == CreatureJob::Fight && c.attacking_id >= 0)) continue;
            float dx = other.x - c.x, dy = other.y - c.y;
            float d2 = dx * dx + dy * dy;
            if (d2 <= c.melee_range * c.melee_range) { enemy = &other; best_d2 = d2; break; }
            if (d2 < best_d2) { best_d2 = d2; enemy = &other; }
        }
        if (enemy) c.attacking_id = enemy->id;
    }

    for (int i = 0; i < 8; ++i) {
        c.melee_attackers[i] = -1;
        c.range_attackers[i] = -1;
    }
    int melee_n = 0, range_n = 0;
    for (const Creature& other : state_.creatures) {
        if (&other == &c || other.state == CreatureState::Dead ||
            other.team == c.team || other.attacking_id != c.id) continue;
        float dx = other.x - c.x, dy = other.y - c.y;
        float d2 = dx * dx + dy * dy;
        if (d2 <= other.melee_range * other.melee_range) {
            if (melee_n < 8) c.melee_attackers[melee_n++] = other.id;
        } else if (d2 <= other.sight * other.sight) {
            if (range_n < 8) c.range_attackers[range_n++] = other.id;
        }
    }
    float incoming_threat = 0.0f;
    for (int i = 0; i < 8 && c.melee_attackers[i] >= 0; ++i)
        for (const Creature& other : state_.creatures)
            if (other.id == c.melee_attackers[i]) incoming_threat += other.threat;
    for (int i = 0; i < 8 && c.range_attackers[i] >= 0; ++i)
        for (const Creature& other : state_.creatures)
            if (other.id == c.range_attackers[i]) incoming_threat += other.threat * 0.5f;
    if (enemy) incoming_threat = std::max(incoming_threat, enemy->threat);
    c.temp_fear = std::max(0.0f, c.temp_fear + incoming_threat * 0.02f * dt - 0.5f * dt);
    c.current_fear = std::max(c.current_fear, c.temp_fear);
    if (enemy) c.threat_thing = enemy->id;

    bool outnumbered = (melee_n + range_n) > 1;
    bool already_fleeing = c.job == CreatureJob::Flee && c.backoff_timer > 0.0f;
    bool overwhelmed = enemy && (already_fleeing || c.backoff_timer <= 0.0f) &&
                       state_.elapsed >= c.fearless_until &&
                       !(c.ai_flags & kAIFlagFearless) &&
                       (c.current_fear > std::max(1.0f, c.fear) || outnumbered) &&
                       (c.hp < c.max_hp * 0.55f || outnumbered);
    if (overwhelmed) {
        if (c.job != CreatureJob::Flee) c.return_job_id = c.current_job_id;
        set_job(c, CreatureJob::Flee, state_.elapsed);
        float ax = c.x - enemy->x, ay = c.y - enemy->y;
        float len = std::max(0.0001f, std::sqrt(ax * ax + ay * ay));
        c.target_x = std::max(0, std::min(world.width - 1,
                                         int(c.x - ax / len * 4.0f)));
        c.target_y = std::max(0, std::min(world.height - 1,
                                         int(c.y - ay / len * 4.0f)));
        c.path.clear();
        c.path_index = 0;
        c.state = CreatureState::Walking;
        c.backoff_timer = 2.0f;
        c.retreating_creature = enemy->id;
        ++c.overthreat_counter;
        apply_anger(c, 1.0f, 1);
    } else if (enemy && best_d2 <= c.melee_range * c.melee_range) {
        set_job(c, CreatureJob::Fight, state_.elapsed);
        c.state = CreatureState::Fighting;
        c.attack_type = 1;
        if (c.attack_cooldown <= 0.0f) {
            enemy->hp -= c.damage * resistance_factor(*enemy, c.attack_type);
            c.attack_cooldown = std::max(0.1f, c.melee_recharge);
            enemy->current_fear = std::max(enemy->current_fear, c.threat * 0.05f);
            gain_xp(c.damage * 0.05f);
        }
        apply_anger(*enemy, 1.0f, 1);
    }

    if (enemy && enemy->state != CreatureState::Dead && c.creature_spell_cooldown <= 0.0f) {
        float dist = std::sqrt(best_d2);
        for (int i = 0; i < 3; ++i) {
            int sid = c.spell_id[i];
            if (sid <= 0 || sid >= 42 || c.level < c.spell_level[i]) continue;
            float range = kCreatureSpellRange[sid] > 0.0f
                              ? kCreatureSpellRange[sid] : c.melee_range;
            if (dist > range) continue;
            bool healing = (sid == 14 || sid == 20 || sid == 32);
            bool self_buff = (sid == 13 || sid == 15 || sid == 16 || sid == 17 ||
                              sid == 21 || sid == 22 || sid == 33 || sid == 36 || sid == 37);
            if (healing) {
                c.hp = std::min(c.max_hp, c.hp + c.max_hp * 0.2f);
            } else if (self_buff) {
                if (sid == 13 || sid == 21 || sid == 37)
                    c.speed = std::min(4.0f, c.speed * 1.15f);
                else if (sid == 36) {
                    ai_rng_ = mix32(ai_rng_ ^ uint32_t(c.id));
                    int nx = std::max(0, std::min(world.width - 1,
                        int(c.x) + int(ai_rng_ % 9U) - 4));
                    int ny = std::max(0, std::min(world.height - 1,
                        int(c.y) + int((ai_rng_ >> 7) % 9U) - 4));
                    if (world.walkable(nx, ny)) {
                        c.x = float(nx) + 0.5f;
                        c.y = float(ny) + 0.5f;
                        c.path.clear();
                    }
                } else
                    c.fearless_until = state_.elapsed + 10.0f;
            } else {
                enemy->hp -= c.damage * (0.5f + 0.1f * float(c.level)) *
                             (1.0f + 0.1f * float(c.fight_style)) *
                             resistance_factor(*enemy, c.spell_no);
            }
            c.creature_spell_cooldown = std::max(2.0f, kCreatureSpellRecharge[sid]);
            c.active_creature_spell = sid;
            c.spell_no = sid;
            gain_xp(5.0f);
            break;
        }
    }
    if (enemy && enemy->hp <= 0.0f) {
        enemy->hp = 0.0f;
        enemy->state = CreatureState::Dead;
        enemy->death_timer = 0.0f;
        enemy->attacking_id = -1;
        c.attacking_id = -1;
        c.mood = std::min(100.0f, c.mood + 6.0f);
        gain_xp(std::max(20.0f, enemy->max_hp * 0.05f));
    }

    /* --- job selection: three mood tiers (00479200.c:96-104) ------------- */
    if (!enemy && c.job == CreatureJob::Fight && c.path.empty()) {
        set_job(c, CreatureJob::Idle, state_.elapsed);
    }
    if (c.job != CreatureJob::Fight && c.job != CreatureJob::Flee) {
        if (c.rebel_active && c.anger_state >= 2) {
            if (c.path.empty()) set_job(c, CreatureJob::Rebel, state_.elapsed);
        } else if (!c.get_paid && c.payment_needed > 0.0f && c.path.empty()) {
            set_job(c, CreatureJob::Pay, state_.elapsed);
        } else if (c.gold_carried > 0.0f) {
            set_job(c, CreatureJob::HaulGold, state_.elapsed);
        } else if (c.hunger > 60.0f) {
            set_job(c, CreatureJob::Eat, state_.elapsed);
        } else if (c.fatigue > 75.0f || c.sleep_need > 60.0f) {
            set_job(c, CreatureJob::Sleep, state_.elapsed);
        } else if (c.job == CreatureJob::Idle || c.job == CreatureJob::Guard ||
                   c.job == CreatureJob::Explore || c.job == CreatureJob::Train ||
                   c.job == CreatureJob::Research || c.job == CreatureJob::Sulk) {
            if (c.path.empty()) {
                const int* jobs = c.happy_job;
                const int* chances = c.happy_chance;
                if (c.anger_state == 1) {
                    jobs = c.unhappy_job; chances = c.unhappy_chance;
                } else if (c.anger_state >= 2) {
                    jobs = c.angry_job; chances = c.angry_chance;
                }
                ai_rng_ = mix32(ai_rng_ ^ (0x9e3779b9U * uint32_t(c.id + 1)) ^
                                uint32_t(state_.tick_count));
                unsigned roll = ai_rng_ % 100U;
                unsigned cumulative = 0;
                CreatureJob picked = CreatureJob::Idle;
                for (int i = 0; i < 3; ++i) {
                    unsigned chance = static_cast<unsigned>(
                        std::min(100, std::max(0, chances[i])));
                    if (jobs[i] > 0 && roll >= cumulative && roll < cumulative + chance) {
                        picked = job_from_original_id(jobs[i]);
                        break;
                    }
                    cumulative += chance;
                }
                if (picked != CreatureJob::Idle) {
                    set_job(c, picked, state_.elapsed);
                    if (c.anger_state == 0) c.last_happy_chosen_tick = int(state_.tick_count);
                    else if (c.anger_state == 1) c.last_hunger_chosen_tick = int(state_.tick_count);
                    else c.last_angry_chosen_tick = int(state_.tick_count);
                } else {
                    /* Artisan species prefer the workshop, thinkers the library. */
                    int wanted = (c.job_class == 3 || c.species % 3 == 0)
                        ? static_cast<int>(TerrainId::Workshop)
                        : (c.job_class == 1 ? static_cast<int>(TerrainId::Library)
                                            : static_cast<int>(TerrainId::GuardRoom));
                    int tx, ty;
                    if (find_room_tile(state_.rooms, wanted, cx, cy, tx, ty))
                        set_job(c, wanted == static_cast<int>(TerrainId::Library)
                                       ? CreatureJob::Research : CreatureJob::Train,
                                state_.elapsed);
                    else
                        set_job(c, CreatureJob::Guard, state_.elapsed);
                }
            }
        }
    }

    /* --- target tile for the selected job ------------------------------- */
    if (c.target_x < 0 && c.path.empty()) {
        int tx = cx, ty = cy;
        bool have_target = false;
        int room_type = 0;
        if (room_tile_for_job(c.job, room_type))
            have_target = find_room_tile(state_.rooms, room_type, cx, cy, tx, ty);
        if (!have_target && c.job == CreatureJob::Explore) {
            ai_rng_ = mix32(ai_rng_ ^ uint32_t(c.id));
            tx = std::max(0, std::min(world.width - 1,
                                      cx + int(ai_rng_ % 17U) - 8));
            ty = std::max(0, std::min(world.height - 1,
                                      cy + int((ai_rng_ >> 8) % 17U) - 8));
            have_target = world.walkable(tx, ty);
        }
        if (have_target) {
            std::vector<int> path;
            if (find_path(cx, cy, tx, ty, path)) {
                c.path = std::move(path);
                c.path_index = 0;
                c.state = CreatureState::Walking;
            }
        }
    }

    /* --- movement ------------------------------------------------------- */
    if (c.path_index < static_cast<int>(c.path.size())) {
        int target = c.path[c.path_index];
        float tx = float(target % world.width) + 0.5f;
        float ty = float(target / world.width) + 0.5f;
        float dx = tx - c.x, dy = ty - c.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        float move = c.speed * dt;
        if (dist <= move || dist < 0.0001f) {
            c.x = tx; c.y = ty;
            ++c.path_index;
        } else {
            c.x += dx / dist * move;
            c.y += dy / dist * move;
        }
    } else if (c.state == CreatureState::Walking) {
        c.path.clear();
        c.target_x = c.target_y = -1;
        c.state = physical_state(c.job);
    }

    /* --- job effects at destination ------------------------------------- */
    const Tile* tile = world.at(std::max(0, std::min(world.width - 1, int(c.x))),
                                std::max(0, std::min(world.height - 1, int(c.y))));
    if (c.job == CreatureJob::Eat &&
        tile && tile->terrain == static_cast<uint8_t>(TerrainId::Hatchery)) {
        c.hunger = std::max(0.0f, c.hunger - std::max(10.0f, c.hunger_fill * 8.0f) * dt);
        c.last_ate_time = state_.elapsed;
        c.mood = std::min(100.0f, c.mood + 1.0f * dt);
        gain_xp(c.exp_per_second * 0.1f * dt);
    }
    if (c.job == CreatureJob::Sleep &&
        tile && tile->terrain == static_cast<uint8_t>(TerrainId::Lair)) {
        c.fatigue = std::max(0.0f, c.fatigue - 35.0f * dt);
        c.sleep_need = std::max(0.0f, c.sleep_need - 20.0f * dt);
        c.time_slept += dt * 30.0f;
        c.last_slept_time = state_.elapsed;
        gain_xp(c.exp_per_second * 0.05f * dt);
        bool slept_enough = c.time_sleep <= 0.0f || c.time_slept >= c.time_sleep;
        if (c.fatigue <= 0.0f && c.sleep_need <= 0.0f && slept_enough) {
            c.time_awake_elapsed = 0.0f;
            if (c.path.empty()) set_job(c, CreatureJob::Idle, state_.elapsed);
        }
    }
    if (c.job == CreatureJob::Train &&
        tile && tile->terrain == static_cast<uint8_t>(TerrainId::TrainingRoom)) {
        c.damage += 0.02f * dt;
        gain_xp(c.exp_per_second_training * dt);
    }
    if (c.job == CreatureJob::Research &&
        tile && tile->terrain == static_cast<uint8_t>(TerrainId::Library)) {
        float progress = c.research_per_second * 0.1f * dt;
        for (size_t i = 0; i < state_.research.size(); ++i)
            state_.research[i] += progress;
        gain_xp(c.exp_per_second * 0.15f * dt);
    }
    if (c.job == CreatureJob::Manufacture &&
        tile && tile->terrain == static_cast<uint8_t>(TerrainId::Workshop))
        gain_xp(c.manufacture_per_second * dt);
    if (c.job == CreatureJob::Pray &&
        tile && tile->terrain == static_cast<uint8_t>(TerrainId::Temple))
        state_.mana = std::min(state_.mana_max, state_.mana + c.mana_gen_prayer * dt);
    if (c.job == CreatureJob::Guard) gain_xp(c.exp_per_second * 0.05f * dt);
    if (c.job == CreatureJob::Explore) gain_xp(c.exp_per_second * 0.02f * dt);
    if (c.job == CreatureJob::Sulk || c.job == CreatureJob::Rebel)
        apply_anger(c, 0.05f, 4);
    if (tile &&
        structures::terrain_is_minable_id(static_cast<int>(tile->terrain)) &&
        c.gold_carried < c.max_gold && c.hunger < 80.0f) {
        const DK2TerrainDef* td = structures::terrain_for_engine_id(
            static_cast<int>(tile->terrain));
        float value = td ? float(structures::terrain_gold_value(td)) : 100.0f;
        c.gold_carried += (value > 0.0f ? value : 1.0f) * 0.05f * dt;
        c.job = CreatureJob::Mine;
        gain_xp(c.exp_per_second * 0.2f * dt);
        if (c.gold_carried >= c.max_gold) {
            c.target_x = c.target_y = -1;
            c.path.clear();
        }
    }
    if (c.job == CreatureJob::Tunnels) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const Tile* neighbour = world.at(int(c.x) + dx, int(c.y) + dy);
                if (!neighbour || !structures::terrain_is_minable_id(int(neighbour->terrain)))
                    continue;
                c.gold_carried += 0.05f * dt * state_.config.mining_rate;
                gain_xp(c.exp_per_second * 0.15f * dt);
                break;
            }
        }
    }
    if (c.gold_carried > 0.0f) {
        for (const Room& room : state_.rooms) {
            if (room.type != static_cast<int>(TerrainId::Treasury)) continue;
            if (std::abs(float(room.x) - c.x) < 1.0f &&
                std::abs(float(room.y) - c.y) < 1.0f) {
                c.gold_carried -= state_.economy.deposit(c.gold_carried);
                gain_xp(c.exp_per_second * 0.1f * dt);
                break;
            }
        }
    }
    c.mood = std::max(0.0f, std::min(c.mood, 100.0f - anger_total(c)));
    c.idle_counter = std::abs(c.x - cx) < 0.01f && std::abs(c.y - cy) < 0.01f
                         ? c.idle_counter + 1 : 0;
}

void Game::apply_anger(Creature& c, float amount, int source) {
    if (c.state == CreatureState::Dead || amount == 0.0f) return;
    int slot = std::max(0, std::min(5, source));
    c.anger_values[slot] = std::max(0.0f,
                                    std::min(1000.0f, c.anger_values[slot] + amount));
    float total = anger_total(c);
    c.mood = std::max(0.0f, std::min(c.mood, 100.0f - total));
    int previous = c.anger_state;
    float limit = std::max(1.0f, c.unhappy_threshold);
    c.anger_state = total >= limit * 2.0f ? 2 : (total >= limit ? 1 : 0);
    if (c.anger_state == 2 && previous != 2) {
        c.rebel_active = true;
        if (c.my_leaders_id >= 0) c.rebel_leader = true;
    }
}

void Game::spawn_initial_enemies() {
    if (initial_enemies_spawned_ || state_.world.width <= 0 ||
        state_.world.height <= 0) return;
    initial_enemies_spawned_ = true;
    for (const Creature& c : state_.creatures)
        if (c.team == 1 && c.state != CreatureState::Dead) return;
    int keepers = 0;
    for (const Creature& c : state_.creatures)
        if (c.team == 0 && c.state != CreatureState::Dead) ++keepers;
    if (keepers == 0) return;  // nothing to skirmish with yet

    uint32_t seed = 0x9e3779b9U;
    const std::string& label = state_.mission_name.empty() ? state_.world.name
                                                           : state_.mission_name;
    for (char ch : label) seed = mix32(seed ^ uint32_t(static_cast<uint8_t>(ch)));
    seed = mix32(seed ^ uint32_t(state_.world.width * 131 + state_.world.height));
    ai_rng_ = seed;

    static const int kEnemySpecies[] = {6, 12, 15, 5, 7, 17, 24};  // Goblin, Knight, ...
    int count = 2 + int(seed % 2U);
    for (int n = 0; n < count; ++n) {
        int best_x = -1, best_y = -1;
        long best_score = -1;
        for (int y = 0; y < state_.world.height; ++y) {
            for (int x = 0; x < state_.world.width; ++x) {
                if (!state_.world.walkable(x, y)) continue;
                long nearest = 1 << 28;
                for (const Creature& c : state_.creatures) {
                    if (c.state == CreatureState::Dead) continue;
                    long dx = long(x) - long(c.x), dy = long(y) - long(c.y);
                    nearest = std::min(nearest, dx * dx + dy * dy);
                }
                long score = nearest + long(mix32(seed ^ uint32_t(x * 733 + y * 517)) % 7U);
                if (score > best_score) { best_score = score; best_x = x; best_y = y; }
            }
        }
        if (best_x < 0) break;
        int species = kEnemySpecies[mix32(seed ^ uint32_t(n * 2654435761U)) %
                                    (sizeof(kEnemySpecies) / sizeof(kEnemySpecies[0]))];
        int id = spawn_creature(species, best_x, best_y, 1);
        if (id >= 0) {
            Creature& e = state_.creatures.back();
            set_job(e, CreatureJob::Guard, state_.elapsed);
            e.ai_flags |= kAIFlagAlwaysFlee;  // heroes retreat when overwhelmed
        }
    }
}

static const DK2TriggerNode* find_node(const DK2MissionTriggers& set, unsigned id) {
    for (unsigned i = 0; i < set.count; ++i)
        if (set.nodes[i].id == id) return &set.nodes[i];
    return nullptr;
}

static bool condition_true(const DK2TriggerNode& n, const GameState& s) {
    int keeper = 0;
    for (const Creature& c : s.creatures)
        if (c.team == 0 && c.state != CreatureState::Dead) ++keeper;
    switch (n.type) {
        case 1: {
            unsigned id = n.id_param & 255u;
            return id < s.flags.size() ? s.flags[id] != 0 : false;
        }
        case 2: {
            unsigned id = n.id_param & 255u;
            float value = id < s.timers.size() ? s.timers[id] : 0.0f;
            return n.value <= 0.0f || value >= n.value;
        }
        case 19: return float(keeper) >= n.value;                 /* PLAYER_CREATURES */
        case 25: return float(s.rooms.size()) >= n.value;         /* PLAYER_ROOMS */
        case 30: return s.economy.gold >= n.value;                /* PLAYER_GOLD */
        case 34: return s.elapsed >= n.value;                     /* LEVEL_TIME */
        case 47: {
            for (const Creature& c : s.creatures)
                if (c.team == 1 && c.state == CreatureState::Dead) return true;
            return false;
        }
        case 72: return s.elapsed > 2.0f;                         /* GUI_TRANSITION_ENDS */
        default: return false;
    }
}

void Game::evaluate_triggers() {
    const DK2MissionTriggers* set = nullptr;
    for (unsigned i = 0; i < kDK2MissionTriggerCount; ++i) {
        if (state_.mission_name == kDK2MissionTriggers[i].map) {
            set = &kDK2MissionTriggers[i];
            break;
        }
    }
    if (!set) return;

    for (unsigned i = 0; i < set->count; ++i) {
        const DK2TriggerNode& cond = set->nodes[i];
        if (cond.is_action) continue;

        /* idNext is a priority/first-match OR chain; idChild is the action. */
        bool satisfied = condition_true(cond, state_);
        unsigned next = cond.next;
        for (int depth = 0; !satisfied && next != 0 && next != 65535 && depth < 32; ++depth) {
            const DK2TriggerNode* nx = find_node(*set, next);
            if (!nx || nx->is_action) break;
            satisfied = condition_true(*nx, state_);
            next = nx->next;
        }
        if (!satisfied || cond.child == 0 || cond.child == 65535) continue;
        const DK2TriggerNode* action = find_node(*set, cond.child);
        if (!action || !action->is_action) continue;
        if (fired_actions_.count(action->id)) continue;
        fired_actions_.insert(action->id);

        switch (action->type) {
            case 7: {  /* FLAG */
                unsigned id = action->id_param & 255u;
                if (id < state_.flags.size())
                    state_.flags[id] = uint8_t(action->value != 0.0f ? 1 : 0);
                break;
            }
            case 8: {  /* INITIALIZE_TIMER */
                unsigned id = action->id_param & 255u;
                if (id < state_.timers.size()) state_.timers[id] = 0.0f;
                break;
            }
            case 23: state_.time_limit = action->value; break;  /* SET_TIME_LIMIT */
            case 25: state_.objective_id = int(action->id_param); break;  /* DISPLAY_TEXT_STRING */
            case 15: state_.objective_id = int(action->id_param); break;  /* SET_OBJECTIVE */
            case 14: {  /* CREATE_HERO_PARTY */
                int x = state_.world.width - 4, y = state_.world.height / 2;
                for (int i = 0; i < 3; ++i)
                    spawn_creature(7 + i, x, y + i, 1);
                break;
            }
            case 10: state_.victory = true; break;
            case 11: state_.defeat = true; break;
            case 12: break;
            case 13: break;
            case 1: {
                int x = state_.world.width / 2, y = state_.world.height / 2;
                spawn_creature(0, x, y, 1);
                break;
            }
            default: break;
        }
    }
}

void Game::step() {
    for (float& t : state_.timers) t += kTickSeconds;
    if (state_.time_limit > 0.0f && state_.elapsed >= state_.time_limit &&
        !state_.victory) state_.defeat = true;
    grid_accumulator_ += kTickSeconds;
    if (grid_ && grid_accumulator_ >= 0.049f) {
        grid_->step();
        grid_accumulator_ = 0.0f;
    }
    if (spell_cooldown_left_ > 0)
        spell_cooldown_left_ = std::max(0.0f, spell_cooldown_left_ - kTickSeconds);
    spawn_initial_enemies();
    evaluate_triggers();
    update_magic();
    for (Creature& c : state_.creatures) update_creature(c);
    state_.elapsed += kTickSeconds;
    ++state_.tick_count;
    int keeper_alive = 0, enemies_alive = 0;
    for (const Creature& c : state_.creatures) {
        if (c.state == CreatureState::Dead) continue;
        if (c.team == 0) ++keeper_alive; else ++enemies_alive;
    }
    if (keeper_alive == 0 && !state_.creatures.empty()) state_.defeat = true;

    const DK2MissionRule* rule = nullptr;
    for (unsigned i = 0; i < kDK2MissionRuleCount; ++i) {
        if (state_.mission_name == kDK2MissionRules[i].map) {
            rule = &kDK2MissionRules[i];
            break;
        }
    }
    bool rule_has_requirement = rule && (rule->required_gold > 0 ||
        rule->required_creatures > 0 || rule->required_rooms > 0 ||
        rule->enemy_must_die != 0);
    if (rule && rule_has_requirement) {
        bool gold_ok = state_.economy.gold >= rule->required_gold;
        bool creatures_ok = float(keeper_alive) >= rule->required_creatures;
        bool rooms_ok = float(state_.rooms.size()) >= rule->required_rooms;
        bool time_ok = state_.elapsed >= rule->min_time;
        bool enemy_ok = rule->enemy_must_die ? (enemies_alive == 0) : true;
        if (gold_ok && creatures_ok && rooms_ok && time_ok && enemy_ok)
            state_.victory = true;
        if (rule->min_time > 0 && state_.elapsed > rule->min_time * 6.0f &&
            !state_.victory)
            state_.defeat = true;
    } else {
        if (enemies_alive == 0 && keeper_alive > 0 && state_.elapsed > 30.0f)
            state_.victory = true;
    }
}

bool Game::save(const std::string& path, std::string* error) const {
    const std::string raw_path = path + ".raw";
    std::ofstream out(raw_path, std::ios::binary);
    if (!out) {
        if (error) *error = "cannot create save temp file";
        return false;
    }
    SaveHeader h{};
    h.magic = kSaveMagic;
    h.version = kSaveVersion;
    h.world_w = uint32_t(state_.world.width);
    h.world_h = uint32_t(state_.world.height);
    h.creature_count = uint32_t(state_.creatures.size());
    h.room_count = uint32_t(state_.rooms.size());
    out.write(reinterpret_cast<const char*>(&h), sizeof(h));

    auto w32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto wf = [&](float v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto wi = [&](int v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto ws = [&](const std::string& v) {
        w32(uint32_t(v.size()));
        if (!v.empty()) out.write(v.data(), std::streamsize(v.size()));
    };

    for (const Tile& t : state_.world.tiles) w32(t.to_word());

    for (const Creature& c : state_.creatures) {
        wi(c.id); wi(c.species); wi(c.team); wi(int(c.state));
        wi(int(c.job));
        wf(c.x); wf(c.y); wf(c.damage); wf(c.attack_cooldown); wf(c.speed); wf(c.sight);
        wf(c.melee_range); wf(c.fear); wf(c.mood);
        wf(c.hp); wf(c.max_hp); wf(c.hunger); wf(c.fatigue);
        wf(c.gold_carried); wf(c.max_gold);
        wi(c.target_x); wi(c.target_y); wi(int(c.path_index));
        w32(uint32_t(c.path.size()));
        for (int node : c.path) wi(node);
    }
    for (const Room& r : state_.rooms) {
        wi(r.type); wi(r.x); wi(r.y); wi(r.owner); wf(r.integrity);
    }
    wf(state_.economy.gold);
    wf(state_.mana); wf(state_.mana_max);
    w32(uint32_t(state_.research.size()));
    for (float v : state_.research) wf(v);
    w32(uint32_t(state_.cooldowns.size()));
    for (float v : state_.cooldowns) wf(v);
    w32(uint32_t(state_.spells_known.size()));
    for (uint8_t v : state_.spells_known) w32(v);
    ws(state_.mission_name);
    for (uint8_t f : state_.flags) w32(f);
    for (float t : state_.timers) wf(t);
    wf(state_.time_limit); wi(state_.objective_id);
    wi(int(state_.victory)); wi(int(state_.defeat));
    wf(state_.camera.center_x); wf(state_.camera.center_y);
    wf(state_.camera.zoom); wf(state_.camera.yaw); wf(state_.camera.pitch);
    wf(state_.elapsed);
    uint64_t ticks = state_.tick_count;
    out.write(reinterpret_cast<const char*>(&ticks), 8);

    // v3-compatible extension: length-tagged trailer of AI state.
    w32(kSaveExtMagic);
    w32(uint32_t(sizeof(SaveCreatureExtras)));
    for (const Creature& c : state_.creatures) {
        SaveCreatureExtras e{};
        pack_extras(c, e);
        out.write(reinterpret_cast<const char*>(&e), sizeof(e));
    }
    w32(ai_rng_);
    w32(initial_enemies_spawned_ ? 1U : 0U);
    wf(pay_period_timer_);
    bool written = out.good();
    out.close();
    if (!written) {
        remove(raw_path.c_str());
        if (error) *error = "failed writing save payload";
        return false;
    }
    std::ifstream pf(raw_path, std::ios::binary);
    std::vector<uint8_t> payload((std::istreambuf_iterator<char>(pf)),
                                 std::istreambuf_iterator<char>());
    pf.close();
    remove(raw_path.c_str());
    save::SaveInfo meta;
    meta.timestamp = uint64_t(std::time(nullptr));
    meta.tick = state_.tick_count;
    meta.elapsed = state_.elapsed;
    meta.creature_count = uint32_t(state_.creatures.size());
    std::strncpy(meta.mission, state_.mission_name.c_str(), save::kMaxMissionName - 1);
    return save::write_container(path, payload.data(), payload.size(), meta, error);
}

bool Game::load(const std::string& path, std::string* error) {
    save::SaveInfo meta;
    std::string source = path;
    const std::string extracted = path + ".extracted";
    if (!save::extract_payload(path, extracted, &meta, error)) return false;
    if (meta.valid) source = extracted;
    std::ifstream in(source, std::ios::binary);
    if (!in) {
        if (meta.valid) remove(extracted.c_str());
        if (error) *error = "cannot open save payload";
        return false;
    }
    SaveHeader h{};
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!in || h.magic != kSaveMagic || h.version != kSaveVersion) {
        if (meta.valid) remove(extracted.c_str());
        if (error) *error = "unsupported save payload version";
        return false;
    }

    auto r32 = [&](uint32_t& v) { in.read(reinterpret_cast<char*>(&v), 4); };
    auto rf = [&](float& v) { in.read(reinterpret_cast<char*>(&v), 4); };
    auto ri = [&](int& v) { in.read(reinterpret_cast<char*>(&v), 4); };
    auto rs = [&](std::string& v) {
        uint32_t n; r32(n);
        if (n > 4096 || !in) { v.clear(); return; }
        v.resize(n);
        if (n) in.read(&v[0], std::streamsize(n));
    };

    state_.world.width = int(h.world_w);
    state_.world.height = int(h.world_h);
    state_.world.tiles.resize(size_t(h.world_w) * h.world_h);
    for (Tile& t : state_.world.tiles) {
        uint32_t word; r32(word); t = Tile::from_word(word);
    }
    state_.creatures.resize(h.creature_count);
    for (Creature& c : state_.creatures) {
        ri(c.id); ri(c.species); ri(c.team);
        int v; ri(v); c.state = static_cast<CreatureState>(v);
        ri(v); c.job = static_cast<CreatureJob>(v);
        rf(c.x); rf(c.y); rf(c.damage); rf(c.attack_cooldown); rf(c.speed); rf(c.sight);
        rf(c.melee_range); rf(c.fear); rf(c.mood);
        rf(c.hp); rf(c.max_hp); rf(c.hunger); rf(c.fatigue);
        rf(c.gold_carried); rf(c.max_gold);
        ri(c.target_x); ri(c.target_y); ri(v); c.path_index = v;
        uint32_t n; r32(n);
        c.path.clear();
        if (n <= 100000) for (uint32_t i = 0; i < n; ++i) { ri(v); c.path.push_back(v); }
    }
    state_.rooms.resize(h.room_count);
    for (Room& r : state_.rooms) {
        ri(r.type); ri(r.x); ri(r.y); ri(r.owner); rf(r.integrity);
    }
    rf(state_.economy.gold);
    rf(state_.mana); rf(state_.mana_max);
    uint32_t n;
    r32(n); state_.research.resize(n <= 1024 ? n : 0);
    for (float& v : state_.research) rf(v);
    r32(n); state_.cooldowns.resize(n <= 1024 ? n : 0);
    for (float& v : state_.cooldowns) rf(v);
    r32(n); state_.spells_known.resize(n <= 1024 ? n : 0);
    for (uint8_t& v : state_.spells_known) { uint32_t x; r32(x); v = uint8_t(x); }
    rs(state_.mission_name);
    for (uint8_t& f : state_.flags) { uint32_t x; r32(x); f = uint8_t(x); }
    for (float& t : state_.timers) rf(t);
    rf(state_.time_limit); ri(state_.objective_id);
    int flag;
    ri(flag); state_.victory = flag != 0;
    ri(flag); state_.defeat = flag != 0;
    rf(state_.camera.center_x); rf(state_.camera.center_y);
    rf(state_.camera.zoom); rf(state_.camera.yaw); rf(state_.camera.pitch);
    rf(state_.elapsed);
    in.read(reinterpret_cast<char*>(&state_.tick_count), 8);

    // Backward-safe: a plain v3 save ends after tick_count.  Only consume the
    // optional trailer when bytes remain and its tag/size validate.
    std::streampos here = in.tellg();
    in.seekg(0, std::ios::end);
    std::streampos file_end = in.tellg();
    if (here >= 0 && file_end > here) {
        in.seekg(here);
        uint32_t magic = 0, size = 0;
        r32(magic); r32(size);
        if (magic == kSaveExtMagic && size == sizeof(SaveCreatureExtras)) {
            for (Creature& c : state_.creatures) {
                SaveCreatureExtras e{};
                if (!in.read(reinterpret_cast<char*>(&e), sizeof(e))) break;
                unpack_extras(c, e);
            }
            if (in.good()) {
                r32(ai_rng_);
                uint32_t flag = 0;
                r32(flag); initial_enemies_spawned_ = flag != 0;
                rf(pay_period_timer_);
            }
        }
        in.clear();
    } else if (here >= 0) {
        in.clear();
        in.seekg(here);
    }

    state_.economy.recalculate(state_.world);
    next_creature_id_ = 1;
    for (const Creature& c : state_.creatures)
        next_creature_id_ = std::max(next_creature_id_, c.id + 1);
    bool ok = in.good();
    if (meta.valid) remove(extracted.c_str());
    return ok;
}

void Game::on_pan(float dx, float dy) {
    state_.camera.center_x -= dx / std::max(0.05f, state_.camera.zoom);
    state_.camera.center_y -= dy / std::max(0.05f, state_.camera.zoom);
    state_.camera.clamp(state_.world);
}

void Game::on_zoom(float factor) {
    state_.camera.zoom *= factor;
    state_.camera.clamp(state_.world);
}

void Game::on_rotate(float delta) {
    state_.camera.yaw += delta;
    state_.camera.clamp(state_.world);
}

void Game::select_at(float wx, float wy) {
    selected_creature = -1;
    float best = 1.5f * 1.5f;
    for (const Creature& c : state_.creatures) {
        float dx = c.x - wx, dy = c.y - wy;
        float d2 = dx * dx + dy * dy;
        if (d2 < best) {
            best = d2;
            selected_creature = c.id;
        }
    }
}

}  // namespace dk2
