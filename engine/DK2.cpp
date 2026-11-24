#include "DK2.h"
#include "Balance.generated.hpp"
#include "CreatureStats.generated.hpp"
#include "MissionRules.generated.hpp"
#include "MissionTriggers.generated.hpp"
#include "SpellData.generated.hpp"
#include "Structures.h"
#include "GridSimulation.h"
#include "SaveFile.h"
#include "TriggerRuntime.h"
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

// Second, independently length-tagged trailer (v5 payload extension).  It is
// appended after the original DK2E block, so old readers stop at DK2E and new
// readers recognise DK2F when present.  Absence leaves every field at its
// default: backward-safe in both directions.
struct SaveCreatureExtras2 {
    int state_before_utility; float suspend_time; int pay_checked_time;
    int return_room_tag_id; int initiated_end_of_level_gems;
    float data_started_at, data_timer_tick; int last_game_tick_interrupted;
    // ImpJobData (11 fields, FUN_00479200 +0x8a..+0xa7)
    int imp_task_id, imp_task_pos_x, imp_task_pos_y, imp_task_surface, imp_task_slot;
    int imp_last_task_type, imp_just_dropped, imp_dropped_task;
    int imp_last_evaluate, imp_scanning_job, imp_scanning_data;
    int utility_new_state;
    // Utility-state parameters
    int move_to_x, move_to_y, room_id_state, room_type_id;
    int move_to_room_x, move_to_room_y;
    int move_range_x, move_range_y, thing_id; float range_squared; int recalc_nav_counter;
    int move_turns_x, move_turns_y, turn_counter, wait_for;
    // Battle override / restricted dispatch
    int battle_state_index, battle_state_index2, ai_battle_restricted;
    // Fight additions
    float attack_range_squared; int creature_retreating_counter; int avoid_retreaters_counter;
    // Per-creature triggers / victim guard diagnostics
    int trigger_root, trigger_root_repeat_count, possessed, victim_guard_warnings;
};

static void pack_extras2(const Creature& c, SaveCreatureExtras2& e) {
    e.state_before_utility = c.state_before_utility; e.suspend_time = c.suspend_time;
    e.pay_checked_time = c.pay_checked_time; e.return_room_tag_id = c.return_room_tag_id;
    e.initiated_end_of_level_gems = c.initiated_end_of_level_gems;
    e.data_started_at = c.data_started_at; e.data_timer_tick = c.data_timer_tick;
    e.last_game_tick_interrupted = c.last_game_tick_interrupted;
    e.imp_task_id = c.imp_job.task_id; e.imp_task_pos_x = c.imp_job.task_pos_x;
    e.imp_task_pos_y = c.imp_job.task_pos_y; e.imp_task_surface = c.imp_job.task_surface;
    e.imp_task_slot = c.imp_job.task_slot; e.imp_last_task_type = c.imp_job.last_task_type;
    e.imp_just_dropped = c.imp_job.just_dropped ? 1 : 0;
    e.imp_dropped_task = c.imp_job.dropped_task;
    e.imp_last_evaluate = c.imp_job.last_evaluate;
    e.imp_scanning_job = c.imp_job.scanning_job ? 1 : 0;
    e.imp_scanning_data = c.imp_job.scanning_data;
    e.utility_new_state = c.utility_new_state;
    e.move_to_x = c.move_to_x; e.move_to_y = c.move_to_y;
    e.room_id_state = c.room_id_state; e.room_type_id = c.room_type_id;
    e.move_to_room_x = c.move_to_room_x; e.move_to_room_y = c.move_to_room_y;
    e.move_range_x = c.move_range_x; e.move_range_y = c.move_range_y;
    e.thing_id = c.thing_id; e.range_squared = c.range_squared;
    e.recalc_nav_counter = c.recalc_nav_counter;
    e.move_turns_x = c.move_turns_x; e.move_turns_y = c.move_turns_y;
    e.turn_counter = c.turn_counter; e.wait_for = c.wait_for;
    e.battle_state_index = c.battle_state_index;
    e.battle_state_index2 = c.battle_state_index2;
    e.ai_battle_restricted = c.ai_battle_restricted ? 1 : 0;
    e.attack_range_squared = c.attack_range_squared;
    e.creature_retreating_counter = c.creature_retreating_counter;
    e.avoid_retreaters_counter = c.avoid_retreaters_counter;
    e.trigger_root = c.trigger_root; e.trigger_root_repeat_count = c.trigger_root_repeat_count;
    e.possessed = c.possessed ? 1 : 0; e.victim_guard_warnings = c.victim_guard_warnings;
}

static void unpack_extras2(Creature& c, const SaveCreatureExtras2& e) {
    c.state_before_utility = e.state_before_utility; c.suspend_time = e.suspend_time;
    c.pay_checked_time = e.pay_checked_time; c.return_room_tag_id = e.return_room_tag_id;
    c.initiated_end_of_level_gems = e.initiated_end_of_level_gems;
    c.data_started_at = e.data_started_at; c.data_timer_tick = e.data_timer_tick;
    c.last_game_tick_interrupted = e.last_game_tick_interrupted;
    c.imp_job.task_id = e.imp_task_id; c.imp_job.task_pos_x = e.imp_task_pos_x;
    c.imp_job.task_pos_y = e.imp_task_pos_y; c.imp_job.task_surface = e.imp_task_surface;
    c.imp_job.task_slot = e.imp_task_slot; c.imp_job.last_task_type = e.imp_last_task_type;
    c.imp_job.just_dropped = e.imp_just_dropped != 0;
    c.imp_job.dropped_task = e.imp_dropped_task;
    c.imp_job.last_evaluate = e.imp_last_evaluate;
    c.imp_job.scanning_job = e.imp_scanning_job != 0;
    c.imp_job.scanning_data = e.imp_scanning_data;
    c.utility_new_state = e.utility_new_state;
    c.move_to_x = e.move_to_x; c.move_to_y = e.move_to_y;
    c.room_id_state = e.room_id_state; c.room_type_id = e.room_type_id;
    c.move_to_room_x = e.move_to_room_x; c.move_to_room_y = e.move_to_room_y;
    c.move_range_x = e.move_range_x; c.move_range_y = e.move_range_y;
    c.thing_id = e.thing_id; c.range_squared = e.range_squared;
    c.recalc_nav_counter = e.recalc_nav_counter;
    c.move_turns_x = e.move_turns_x; c.move_turns_y = e.move_turns_y;
    c.turn_counter = e.turn_counter; c.wait_for = e.wait_for;
    c.battle_state_index = e.battle_state_index;
    c.battle_state_index2 = e.battle_state_index2;
    c.ai_battle_restricted = e.ai_battle_restricted != 0;
    c.attack_range_squared = e.attack_range_squared;
    c.creature_retreating_counter = e.creature_retreating_counter;
    c.avoid_retreaters_counter = e.avoid_retreaters_counter;
    c.trigger_root = e.trigger_root; c.trigger_root_repeat_count = e.trigger_root_repeat_count;
    c.possessed = e.possessed != 0; c.victim_guard_warnings = e.victim_guard_warnings;
}

constexpr uint32_t kSaveMagic = 0x324b3244;  // "DK2S"
constexpr uint32_t kSaveVersion = 4;
constexpr uint32_t kSaveExtMagic = 0x454b3244;  // "DK2E" trailer
constexpr uint32_t kSaveExtMagic2 = 0x464b3244;  // "DK2F" v5 extension trailer
constexpr uint32_t kSaveExtMagic3 = 0x474b3244;  // "DK2G" v6 extension trailer

// Creature flags from OpenKeeper Creature.CreatureFlag (KWD bytes).
constexpr int kAIFlagWorker               = 0x00000001;
constexpr int kAIFlagAlwaysFlee           = 0x00000008;
constexpr int kAIFlagFearless             = 0x00040000;
constexpr int kAIFlagHorny                = 0x00004000;
constexpr int kAIFlagOnlyAttackableHorny  = 0x00400000;
constexpr int kCreatureFlagCanBeSlapped   = 0x00000004;

constexpr int kFightStyleNonFighter = 0;
constexpr float kCriticalHealthPercent = 20.0f;
constexpr float kCallToArmsFightDistance = 5.0f;
constexpr float kDigRockHealth = 5000.0f;
constexpr float kMineGoldHealth = 300.0f;
constexpr float kDigOwnWallHealth = 130.0f;
constexpr float kDigEnemyWallHealth = 8.0f;
constexpr float kAttackTileHealth = 140.0f;
constexpr float kGoldMinedFromGems = 25.0f;
constexpr int kDwarfDiggingMultiplier = 4;
constexpr int kMaxMeleeAttackers = 8;
constexpr int kMaxRangedAttackers = 8;
constexpr int kMaxBattles = 8;
constexpr float kBattleJoinDistanceSquared = 5.0f * 5.0f;
constexpr float kMinBattleTime = 4.0f;

// --- Reconstructed thresholds ------------------------------------------------
// LOW confidence: these numbers were carried over from the previous build and
// could not be tied to a specific decompiled constant/field.  They are kept
// unchanged and reported as UNKNOWN in the reconstruction notes.
constexpr float kHungerEatThreshold     = 60.0f;   // RECONSTRUCTION (UNKNOWN)
constexpr float kNeedAngerThreshold     = 70.0f;   // RECONSTRUCTION (UNKNOWN)
constexpr float kFatigueSleepThreshold  = 75.0f;   // RECONSTRUCTION (UNKNOWN)
constexpr float kSleepNeedThreshold     = 60.0f;   // RECONSTRUCTION (UNKNOWN)
constexpr float kNeedAngerAmount        = 0.01f;   // RECONSTRUCTION (UNKNOWN)
constexpr float kNeedRetrySeconds       = 4.0f;    // RECONSTRUCTION (UNKNOWN)
constexpr float kMoodRecoveryPerSecond  = 0.2f;    // RECONSTRUCTION (UNKNOWN)
constexpr float kPayPeriodSeconds       = 10.0f;   // RECONSTRUCTION (UNKNOWN)
constexpr int   kMaxPaymentAttempts     = 3;       // RECONSTRUCTION (UNKNOWN)
constexpr float kIncomingThreatScale    = 0.02f;   // RECONSTRUCTION (UNKNOWN)
constexpr float kFearDecayPerSecond     = 0.5f;    // RECONSTRUCTION (UNKNOWN)
constexpr float kRetreatHpFraction      = 0.55f;   // RECONSTRUCTION (UNKNOWN)
constexpr float kBackoffSeconds         = 2.0f;    // RECONSTRUCTION (UNKNOWN)
constexpr int   kNavRecalcTicks         = 30;      // RECONSTRUCTION (UNKNOWN)
constexpr float kHelloRange             = 1.5f;    // RECONSTRUCTION (UNKNOWN)
constexpr float kHelloCooldownSeconds   = 5.0f;    // RECONSTRUCTION (UNKNOWN)

// DAT_006da8c7 is a table with a 0x19-byte stride indexed by creature +0x25e;
// a row value of 0x02 re-indexes the table with +0x32d, and a final value of
// 0x16 suppresses state dispatch (00478800.c:8-17, HIGH for the control flow).
// The original table bytes were not exported, so rows default to "no override"
// (RECONSTRUCTION, UNKNOWN table contents).
constexpr int kBattleTableRows = 32;
static const uint8_t kBattleOverrideTable[kBattleTableRows] = {};

static int battle_override_value(const Creature& c) {
    int v = 0;
    if (c.battle_state_index >= 0 && c.battle_state_index < kBattleTableRows)
        v = kBattleOverrideTable[c.battle_state_index];
    if (v == 0x02 && c.battle_state_index2 >= 0 && c.battle_state_index2 < kBattleTableRows)
        v = kBattleOverrideTable[c.battle_state_index2];
    return v;
}

static bool can_be_hurt_by(int attacker_flags, int target_flags) {
    if (target_flags & kAIFlagHorny) return false;
    if ((target_flags & kAIFlagOnlyAttackableHorny) &&
        !(attacker_flags & kAIFlagHorny)) return false;
    return true;
}

static float rear_attack_bonus(float damage, const Creature& attacker,
                               const Creature& target) {
    float dx = attacker.x - target.x;
    float dy = attacker.y - target.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0f) return damage;
    float facing_x = std::sin(target.facing);
    float facing_y = std::cos(target.facing);
    float cos_angle = (facing_x * dx + facing_y * dy) / len;
    cos_angle = std::max(-1.0f, std::min(1.0f, cos_angle));
    int steps = std::min(1023, int(std::acos(cos_angle) / 3.14159265f * 1024.0f));
    int percent = steps / 20;
    if (percent < 16) return damage;
    return damage * (100.0f + float(percent)) / 100.0f;
}

// FUN_00478800: dispatch one CState id to its state process function.  The
// individual process functions are attributed by source-path strings in
// AI_FUNCTIONS.md §1; where a function maps to a physical state we set it here.
static void dispatch_state(Creature& c, float now) {
    if (battle_override_value(c) == kAiStateBattleSkip)
        return;  // global battle override: dispatch skipped (return 0)
    const int sid = c.state_id;
    // Restricted battle branch (00478800.c:108-120): only ids 10 and 0x10 run.
    if (c.ai_battle_restricted && sid != kAiStateBattleAllowed1 &&
        sid != kAiStateBattleAllowed2)
        return;

    CreatureState physical = c.state;
    bool recognized = true;
    switch (sid) {
        case 1: case 2: case 3: case 4: case 5: break;
        case 6:  physical = CreatureState::Walking; break;  // MoveTo
        case 7:  physical = CreatureState::Walking; break;  // MoveToRoom
        case 8:  physical = CreatureState::Walking; break;  // MoveWithinRangeOfThing
        case 9:  physical = CreatureState::Walking; break;  // MoveToForNTurns
        case 10: break;                                     // battle-allowed
        case 11: physical = CreatureState::Fighting; break;
        case 12: case 13: case 14: case 15: break;
        case 16: break;                                     // battle-allowed
        case 17: case 18: case 19: break;
        case 20: break;                                     // 0x14: no handler
        case 22: case 23: break;
        case 24: physical = CreatureState::Exploring; break; // Explore (0045a290)
        case 25: case 26: case 27: case 28: break;
        case kAiStatePayNoPayRoom: physical = CreatureState::Paying; break;
        default: recognized = false; break;
    }
    if (recognized) {
        if (physical != c.state && c.state != CreatureState::Walking &&
            c.state != CreatureState::Dead && c.state != CreatureState::InHand)
            c.state = physical;
        c.state_tick_last_updated = now;
    }
}

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

static void grant_xp(Creature& c, float amount) {
    if (amount <= 0.0f) return;
    c.xp += amount;
    while (c.xp >= c.exp_for_next_level) {
        c.xp -= c.exp_for_next_level;
        ++c.level;
        c.max_hp *= 1.05f;
        c.hp = std::min(c.max_hp, c.hp + c.max_hp * 0.05f);
        c.mood = std::min(100.0f, c.mood + 5.0f);
    }
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
            // OWN IMPLEMENTATION: the shipped Treasury record has no explicit
            // capacity field; its max_health (10000) is used as the per-tile
            // capacity because it is the only plausible quantity in the KWD.
            const DK2TerrainDef* def = structures::terrain_for_engine_id(
                static_cast<int>(t.terrain));
            cap += def ? float(def->max_health) : 4000.0f;
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
    trigger_runtime_.reset();
    grid_.reset(new GridSimulation(state_.world.width, state_.world.height));
    for (int gy = 0; gy < state_.world.height; ++gy)
        for (int gx = 0; gx < state_.world.width; ++gx)
            sync_grid_tile(gx, gy);
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
    trigger_runtime_.reset();
    for (unsigned i = 0; i < kDK2MissionTriggerCount; ++i) {
        if (name == kDK2MissionTriggers[i].map) {
            const DK2MissionTriggers* set = &kDK2MissionTriggers[i];
            trigger_runtime_.reset(new dk2trigger::TriggerRuntime(
                *set, dk2trigger::TriggerRuntime::deriveRoots(*set)));
            break;
        }
    }
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
    const DK2RoomDef* def = (type >= 0 && type < static_cast<int>(state_.known_rooms.size()))
        ? structures::room_by_name(state_.known_rooms[static_cast<size_t>(type)].c_str())
        : nullptr;
    int w = def ? std::max(1, structures::room_recommended_width(def)) : 1;
    int h = def ? std::max(1, structures::room_recommended_height(def)) : 1;
    if (x < 0 || y < 0 || x + w > state_.world.width || y + h > state_.world.height)
        return false;
    TerrainId terrain = TerrainId::Treasury;
    if (def) {
        int tid = structures::stats_terrain_to_engine_id(structures::room_terrain_id(def));
        if (tid >= 0 && tid < 256) terrain = static_cast<TerrainId>(tid);
    }
    for (int dy = 0; dy < h; ++dy)
        for (int dx = 0; dx < w; ++dx)
            if (!state_.world.at(x + dx, y + dy) ||
                construction_blocked(x + dx, y + dy)) return false;
    for (const Room& r : state_.rooms)
        if (r.x >= x && r.x < x + w && r.y >= y && r.y < y + h) return false;
    int price = def ? structures::room_gold_cost(def) : -1;
    int cost = price > 0 ? price * w * h : -1;
    if (cost > 0 && !state_.economy.withdraw(float(cost))) return false;
    const DK2TerrainDef* rtdef = structures::terrain_for_engine_id(static_cast<int>(terrain));
    float hp = (rtdef && rtdef->starting_health > 0) ? float(rtdef->starting_health) : 10000.0f;
    for (int dy = 0; dy < h; ++dy) {
        for (int dx = 0; dx < w; ++dx) {
            Tile* tile = state_.world.at(x + dx, y + dy);
            tile->terrain = static_cast<uint8_t>(terrain);
            tile->owner = static_cast<uint8_t>(owner);
            Room room;
            room.type = static_cast<int>(terrain);
            room.x = x + dx; room.y = y + dy; room.owner = owner;
            room.integrity = room.max_integrity = hp;
            state_.rooms.push_back(room);
        }
    }
    for (int dy = 0; dy < h; ++dy)
        for (int dx = 0; dx < w; ++dx) sync_grid_tile(x + dx, y + dy);
    state_.economy.recalculate(state_.world);
    return true;
}

bool Game::damage_room(int x, int y, float amount) {
    for (auto it = state_.rooms.begin(); it != state_.rooms.end(); ++it) {
        if (it->x != x || it->y != y) continue;
        it->integrity -= amount;
        if (it->integrity > 0.0f) return true;
        Tile* tile = state_.world.at(x, y);
        if (tile) tile->terrain = static_cast<uint8_t>(TerrainId::DirtPath);
        state_.rooms.erase(it);
        sync_grid_tile(x, y);
        state_.economy.recalculate(state_.world);
        return true;
    }
    return false;
}

bool Game::repair_room(int x, int y, float amount) {
    for (Room& r : state_.rooms) {
        if (r.x != x || r.y != y) continue;
        if (r.integrity >= r.max_integrity) return false;
        r.integrity = std::min(r.max_integrity, r.integrity + amount);
        return true;
    }
    return false;
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
    trap.max_integrity = trap.integrity;
    trap.trap_cooldown = structures::trap_charge_time(def);
    trap.trap_charges = structures::trap_shots_when_armed(def);
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
    for (const Door& d : state_.doors) if (d.x == x && d.y == y) return false;
    Door door;
    door.type = def->door_id;
    door.x = x; door.y = y; door.owner = owner;
    door.integrity = door.max_integrity = float(def->health > 0 ? def->health : 1000);
    door.secret = structures::door_is_secret(def);
    door.hero = structures::door_is_hero(def);
    state_.doors.push_back(door);
    sync_grid_tile(x, y);
    return true;
}

static bool spell_cast_rule_valid(int rule, const Tile& tile, int team) {
    const bool solid = !terrain_walkable(static_cast<TerrainId>(tile.terrain));
    switch (rule) {
        case 0: return false;
        case 1: return !solid && tile.owner == uint8_t(team);
        case 2: return !solid && (tile.owner == uint8_t(team) || tile.owner == 255);
        case 3: return !solid && tile.owner != uint8_t(team);
        case 4: return !solid;
        case 5: return true;
        default: return false;
    }
}

static bool spell_target_rule_valid(int rule, const Creature* target) {
    switch (rule) {
        case 0: return false;
        case 1: return target && target->team == 0;
        case 2: return target && target->team != 0;
        case 3: return target && target->team != 255;
        case 4: case 5: return true;
        case 6: return target && target->team == 0 &&
                       !(target->ai_flags & kAIFlagHorny);
        default: return false;
    }
}

bool Game::cast_spell(int spell_index, int x, int y) {
    if (spell_index < 0 || spell_index >= static_cast<int>(kDK2SpellCount))
        return false;
    if (spell_index >= static_cast<int>(state_.spells_known.size()) ||
        !state_.spells_known[spell_index]) return false;
    if (spell_index < static_cast<int>(state_.cooldowns.size()) &&
        state_.cooldowns[spell_index] > 0.0f) return false;
    const DK2SpellData& spell = kDK2Spells[spell_index];
    Tile* tile = state_.world.at(x, y);
    if (!tile) return false;
    const bool creates_worker = (spell.flags & 0x0002) != 0;
    int imps = 0;
    for (const Creature& c : state_.creatures)
        if (c.species == 0 && c.team == 0 && c.state != CreatureState::Dead) ++imps;
    int mana_cost = spell.mana_cost;
    if (creates_worker) mana_cost *= (std::max(0, imps - 4) + 1);
    if (state_.mana < float(mana_cost)) return false;
    if (!spell_cast_rule_valid(spell.cast_rule, *tile, 0)) return false;
    const Creature* target = nullptr;
    float closest = 1.5f * 1.5f;
    for (const Creature& c : state_.creatures) {
        if (c.state == CreatureState::Dead) continue;
        float dx = c.x - (float(x) + 0.5f), dy = c.y - (float(y) + 0.5f);
        float d2 = dx * dx + dy * dy;
        if (d2 < closest) { closest = d2; target = &c; }
    }
    if (!spell_target_rule_valid(spell.target_rule, target)) return false;
    state_.mana -= float(mana_cost);
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
    // OWN IMPLEMENTATION: KWD terrain mana_gain is a per-tile rate; the
    // original tick scaling was not recovered. 1/10 of the table value per
    // game second is used, summed over tiles claimed by player 0.
    float gain = 0.0f;
    for (const Tile& t : state_.world.tiles) {
        if (t.owner != 0 && t.terrain != static_cast<uint8_t>(TerrainId::DungeonHeart))
            continue;
        const DK2TerrainDef* def = structures::terrain_for_engine_id(
            static_cast<int>(t.terrain));
        gain += def ? float(def->mana_gain) : 0.0f;
    }
    state_.mana = std::min(state_.mana_max,
                           state_.mana + gain * 0.1f * kTickSeconds);
    for (float& cd : state_.cooldowns)
        if (cd > 0.0f) cd = std::max(0.0f, cd - kTickSeconds);
    for (unsigned i = 0; i < kDK2SpellCount; ++i)
        if (i < state_.spells_known.size() && !state_.spells_known[i])
            research_spell(static_cast<int>(i));
}

bool Game::door_blocks(int x, int y, int team) const {
    for (const Door& d : state_.doors) {
        if (d.x != x || d.y != y || d.open || d.integrity <= 0.0f) continue;
        if (d.owner == team) continue;
        if (d.secret && team != 1) continue;
        return true;
    }
    return false;
}

bool Game::damage_door(int x, int y, float amount) {
    for (auto it = state_.doors.begin(); it != state_.doors.end(); ++it) {
        if (it->x != x || it->y != y) continue;
        it->integrity -= amount;
        if (it->integrity > 0.0f) return true;
        state_.doors.erase(it);
        sync_grid_tile(x, y);
        return true;
    }
    return false;
}

bool Game::find_path(int sx, int sy, int tx, int ty, std::vector<int>& out, int team) const {
    out.clear();
    const World& world = state_.world;
    auto passable = [&](int x, int y) {
        return world.walkable(x, y) && !door_blocks(x, y, team);
    };
    if (!passable(sx, sy) || !passable(tx, ty)) return false;
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
                if (!passable(cx + dx, cy + dy)) continue;
                if (dx && dy && (!passable(cx + dx, cy) ||
                                 !passable(cx, cy + dy)))
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

void Game::update_needs(Creature& c) {
    // Hunger: the KWD hunger_rate is a period in ticks; 32767 means "never
    // hungry" (AI_FUNCTIONS.md §5.2 / kDK2CreatureAIStats).
    const bool never_hungry = c.hunger_rate >= 32767.0f;
    if (!never_hungry && c.hunger_rate > 0.0f)
        c.hunger = std::min(100.0f, c.hunger +
            std::min(10.0f, 1000.0f / c.hunger_rate) * kTickSeconds);
    else if (!never_hungry)
        c.hunger = std::min(100.0f, c.hunger + state_.config.hunger_rate * kTickSeconds);

    // SleepNeeded is gated by time_awake (32767 = never needs sleep).
    if (c.time_awake > 0.0f && c.time_awake < 32767.0f &&
        c.time_awake_elapsed > c.time_awake)
        c.sleep_need = std::min(100.0f, c.sleep_need + 2.0f * kTickSeconds);
    c.fatigue = std::min(100.0f, c.fatigue + state_.config.fatigue_rate * kTickSeconds);

    if (c.hunger > kNeedAngerThreshold || c.fatigue > kNeedAngerThreshold ||
        c.sleep_need > kNeedAngerThreshold)
        apply_anger(c, kNeedAngerAmount, 1);
    else
        c.mood = std::min(100.0f, c.mood + kMoodRecoveryPerSecond * kTickSeconds);

    // HungerNeed/LastAttemptedHunger and SleepNeeded/LastAttemptedSleep
    // (CState +0x4c/+0x50 and +0x58/+0x5c): stamp a throttled attempt when the
    // need is high but no satisfying room exists.
    if (c.hunger > kHungerEatThreshold &&
        room_tiles_of_type(state_, int(TerrainId::Hatchery)) == 0 &&
        state_.elapsed - c.last_attempt_hunger > kNeedRetrySeconds)
        c.last_attempt_hunger = state_.elapsed;
    if (c.sleep_need > kSleepNeedThreshold &&
        room_tiles_of_type(state_, int(TerrainId::Lair)) == 0 &&
        state_.elapsed - c.last_attempt_sleep > kNeedRetrySeconds)
        c.last_attempt_sleep = state_.elapsed;
}

void Game::update_pay(Creature& c) {
    c.pay_timer += kTickSeconds;
    c.pay_checked_time = int(state_.tick_count);  // CState.PayCheckedTime +0x60
    if (c.pay_timer < kPayPeriodSeconds) return;
    c.pay_timer = 0.0f;
    if (c.team == 0 && c.pay > 0.0f) {
        if (c.skip_next_payment) {
            c.skip_next_payment = false;
            c.get_paid = true;
            c.payment_needed = 0.0f;
            c.payment_attempts = 0;
        } else if (state_.economy.withdraw(c.pay)) {
            c.get_paid = true;
            c.payment_needed = 0.0f;
            c.payment_attempts = 0;
            c.mood = std::min(100.0f, c.mood + 2.0f);  // RECONSTRUCTION (UNKNOWN)
        } else {
            c.get_paid = false;
            c.payment_needed += c.pay;
            ++c.payment_attempts;
            apply_anger(c, c.anger_no_pay, 2);
            if (c.path.empty()) set_job(c, CreatureJob::Pay, state_.elapsed);
            // PayState FUN_00472390: no reachable pay room -> state 0x37.
            // Set last so set_job() does not overwrite it with the job id.
            if (room_tiles_of_type(state_, int(TerrainId::Treasury)) == 0)
                c.state_id = kAiStatePayNoPayRoom;
            if (c.payment_attempts >= kMaxPaymentAttempts) {
                c.rebel_active = true;
                apply_anger(c, c.anger_no_pay * 2.0f, 4);
            }
        }
    } else {
        c.get_paid = true;  // heroes are paid by their own portal
    }
}

bool Game::should_flee_or_attack(Creature& c, int cx, int cy) {
    const Tile* tile = state_.world.at(cx, cy);
    bool in_dungeon_heart = false;
    if (tile && tile->terrain == uint8_t(TerrainId::DungeonHeart)) in_dungeon_heart = true;
    bool fearless = (c.ai_flags & kAIFlagFearless) != 0 || state_.elapsed < c.fearless_until;
    bool always_flee = (c.ai_flags & kAIFlagAlwaysFlee) != 0;
    auto is_threatening = [](const Creature& o) {
        return o.state != CreatureState::Dead && o.state != CreatureState::InHand &&
               o.state != CreatureState::Tortured && o.state != CreatureState::Imprisoned;
    };
    float enemy_threat = 0.0f, our_threat = 0.0f;
    int fellow_fighters = 0;
    for (const Creature& other : state_.creatures) {
        if (&other == &c || !is_threatening(other)) continue;
        float dx = other.x - c.x, dy = other.y - c.y;
        if (dx * dx + dy * dy > c.sight * c.sight) continue;
        if (other.team != c.team) {
            enemy_threat += other.threat;
        } else {
            our_threat += other.threat;
            if (other.fight_style != kFightStyleNonFighter) ++fellow_fighters;
        }
    }
    if (!fearless && !in_dungeon_heart) {
        float threat_caused = (always_flee || c.hp < c.max_hp * kCriticalHealthPercent / 100.0f)
                                  ? c.threat : our_threat;
        if (enemy_threat - threat_caused > c.fear &&
            (fellow_fighters == 0 || always_flee)) {
            if (c.job != CreatureJob::Flee) c.return_job_id = c.current_job_id;
            set_job(c, CreatureJob::Flee, state_.elapsed);
            c.state = CreatureState::Fleeing;
            return true;
        }
    }
    if ((c.fight_style != kFightStyleNonFighter || in_dungeon_heart) &&
        c.attacking_id >= 0 &&
        (c.job == CreatureJob::Fight ||
         (c.call_to_arms_on && c.sight <= kCallToArmsFightDistance) ||
         !c.call_to_arms_on)) {
        if (c.job != CreatureJob::Fight) set_job(c, CreatureJob::Fight, state_.elapsed);
        c.state = CreatureState::Fighting;
        return false;
    }
    return false;
}

void Game::update_worker_tasks() {
    auto tile = [&](int x, int y) -> Tile* { return state_.world.at(x, y); };
    auto adjacent_walkable = [&](int x, int y, int team, int& out_x, int& out_y) {
        static const int dxs[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static const int dys[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        float best = 1.0e9f;
        bool found = false;
        for (int i = 0; i < 8; ++i) {
            int nx = x + dxs[i], ny = y + dys[i];
            if (!state_.world.walkable(nx, ny)) continue;
            if (door_blocks(nx, ny, team)) continue;
            for (const Creature& c : state_.creatures) {
                if (c.worker_task_x == nx && c.worker_task_y == ny) continue;
            }
            float d = float(nx * nx + ny * ny);
            if (!found || d < best) { best = d; out_x = nx; out_y = ny; found = true; }
        }
        return found;
    };
    for (WorkerTask& task : state_.worker_tasks) {
        if (task.assigned < 0) continue;
        bool alive = false;
        for (const Creature& c : state_.creatures)
            if (c.id == task.assigned && c.state != CreatureState::Dead &&
                c.job != CreatureJob::Fight && c.job != CreatureJob::Flee) { alive = true; break; }
        if (!alive) {
            for (Creature& c : state_.creatures)
                if (c.id == task.assigned) { c.worker_task_id = -1; c.worker_task_x = c.worker_task_y = -1; }
            task.assigned = -1;
        }
    }
    for (Creature& c : state_.creatures) {
        if (c.species != 0 || c.state == CreatureState::Dead ||
            c.job == CreatureJob::Fight || c.job == CreatureJob::Flee ||
            c.hunger > kHungerEatThreshold || c.fatigue > kFatigueSleepThreshold)
            continue;
        WorkerTask* task = nullptr;
        for (WorkerTask& t : state_.worker_tasks) if (t.assigned == c.id) { task = &t; break; }
        if (!task) {
            for (WorkerTask& t : state_.worker_tasks) {
                if (t.assigned >= 0) continue;
                task = &t; task->assigned = c.id; break;
            }
        }
        int cx = std::max(0, std::min(state_.world.width - 1, int(c.x)));
        int cy = std::max(0, std::min(state_.world.height - 1, int(c.y)));
        if (!task) {
            if (c.gold_carried > 0.0f) {
                WorkerTask t; t.type = WorkerTaskType::HaulGold;
                t.x = cx; t.y = cy; t.assigned = c.id;
                state_.worker_tasks.push_back(t);
                task = &state_.worker_tasks.back();
                set_job(c, CreatureJob::HaulGold, state_.elapsed);
            } else {
                for (int i = -1; i <= 1; ++i) {
                    int another = (cx + i + state_.world.width) % state_.world.width;
                    for (int j = -1; j <= 1; ++j) {
                        int ty = (cy + j + state_.world.height) % state_.world.height;
                        Tile* t = tile(another, ty);
                        if (!t) continue;
                        TerrainId terr = static_cast<TerrainId>(t->terrain);
                        if ((t->flags & 0x01) && !terrain_walkable(terr)) {
                            WorkerTask wt; wt.type = WorkerTaskType::DigTile;
                            wt.x = another; wt.y = ty; wt.assigned = c.id;
                            state_.worker_tasks.push_back(wt);
                            task = &state_.worker_tasks.back();
                            break;
                        }
                    }
                    if (task) break;
                }
            }
        }
        if (!task) {
            for (int i = 0; i < state_.world.tiles.size(); ++i) {
                Tile* t = &state_.world.tiles[i];
                TerrainId terr = static_cast<TerrainId>(t->terrain);
                int tx = int(i % state_.world.width), ty = int(i / state_.world.width);
                if (terr == TerrainId::ReinforcedWall && t->surface < 100) {
                    WorkerTask wt; wt.type = WorkerTaskType::RepairWall;
                    wt.x = tx; wt.y = ty; wt.assigned = c.id;
                    state_.worker_tasks.push_back(wt);
                    task = &state_.worker_tasks.back();
                    break;
                }
                if (t->owner != uint8_t(c.team) && terrain_walkable(terr) &&
                    terr != TerrainId::Water && terr != TerrainId::Lava) {
                    WorkerTask wt; wt.type = WorkerTaskType::ClaimTile;
                    wt.x = tx; wt.y = ty; wt.assigned = c.id;
                    state_.worker_tasks.push_back(wt);
                    task = &state_.worker_tasks.back();
                    break;
                }
            }
        }
        if (!task) continue;
        c.worker_task_id = 1;
        c.worker_task_x = task->x;
        c.worker_task_y = task->y;
        int tx = task->x, ty = task->y;
        int stand_x = tx, stand_y = ty;
        if (task->type == WorkerTaskType::DigTile || task->type == WorkerTaskType::RepairWall) {
            if (!adjacent_walkable(tx, ty, c.team, stand_x, stand_y)) continue;
        }
        float dx = float(stand_x) + 0.5f - c.x;
        float dy = float(stand_y) + 0.5f - c.y;
        float d2 = dx * dx + dy * dy;
        if (d2 > 0.36f) {
            if (c.path.empty() || c.target_x != stand_x || c.target_y != stand_y) {
                std::vector<int> path;
                if (find_path(cx, cy, stand_x, stand_y, path, c.team)) {
                    c.path = std::move(path);
                    c.path_index = 0;
                    c.target_x = stand_x;
                    c.target_y = stand_y;
                    c.state = CreatureState::Walking;
                }
            }
            continue;
        }
        task->progress += kTickSeconds;
        if (task->progress < 1.0f) continue;
        task->progress -= 1.0f;
        Tile* target = tile(tx, ty);
        if (!target) continue;
        if (task->type == WorkerTaskType::DigTile) {
            const DK2TerrainDef* def = structures::terrain_for_engine_id(int(target->terrain));
            float damage = 100.0f;
            if (def && def->damage > 0) damage = float(def->damage) * 0.1f;
            target->surface = uint8_t(std::max(0, int(target->surface) - int(damage)));
            if (target->surface == 0) {
                TerrainId old = static_cast<TerrainId>(target->terrain);
                if (old == TerrainId::Gold || old == TerrainId::Gems) {
                    float value = def ? float(structures::terrain_gold_value(def)) : 1000.0f;
                    c.gold_carried += value;
                }
                target->terrain = uint8_t(TerrainId::DirtPath);
                target->owner = uint8_t(c.team);
                target->flags &= uint8_t(~0x01);
                sync_grid_tile(tx, ty);
                state_.economy.recalculate(state_.world);
                task->assigned = -1;
            }
        } else if (task->type == WorkerTaskType::ClaimTile) {
            target->surface = uint8_t(std::max(0, int(target->surface) - 25));
            if (target->surface == 0) {
                target->owner = uint8_t(c.team);
                if (static_cast<TerrainId>(target->terrain) == TerrainId::DirtPath)
                    target->terrain = uint8_t(TerrainId::ClaimedPath);
                sync_grid_tile(tx, ty);
                task->assigned = -1;
            }
        } else if (task->type == WorkerTaskType::RepairWall) {
            target->surface = uint8_t(std::min(255, int(target->surface) + 25));
            if (target->surface >= 100) task->assigned = -1;
        } else if (task->type == WorkerTaskType::HaulGold) {
            float deposited = state_.economy.deposit(c.gold_carried);
            c.gold_carried -= deposited;
            task->assigned = -1;
            set_job(c, CreatureJob::Idle, state_.elapsed);
        }
        if (task->assigned < 0) {
            c.worker_task_id = -1;
            c.worker_task_x = c.worker_task_y = -1;
            c.target_x = c.target_y = -1;
        }
    }
    for (auto it = state_.worker_tasks.begin(); it != state_.worker_tasks.end();) {
        bool assigned = false;
        for (const Creature& c : state_.creatures) if (c.id == it->assigned) { assigned = true; break; }
        bool valid = false;
        Tile* t = tile(it->x, it->y);
        if (t) {
            if (it->type == WorkerTaskType::DigTile)
                valid = (t->flags & 0x01) != 0;
            else if (it->type == WorkerTaskType::ClaimTile)
                valid = t->owner != 0;
            else if (it->type == WorkerTaskType::RepairWall)
                valid = t->terrain == uint8_t(TerrainId::ReinforcedWall) && t->surface < 100;
            else
                valid = true;
        }
        if (!assigned || !valid) it = state_.worker_tasks.erase(it); else ++it;
    }
}

void Game::update_battles() {
    auto by_id = [&](int id) -> Creature* {
        for (Creature& c : state_.creatures) if (c.id == id) return &c;
        return nullptr;
    };
    auto fighting = [&](const Creature& c) {
        return c.state == CreatureState::Fighting && c.attacking_id >= 0 &&
               by_id(c.attacking_id) != nullptr;
    };
    for (Creature& c : state_.creatures) {
        if (c.battle_id < 0) continue;
        Creature* member = by_id(c.id);
        if (!member || c.state == CreatureState::Dead || c.state == CreatureState::InHand ||
            (!fighting(c) && state_.elapsed >= c.battle_leave_time)) {
            c.battle_id = -1;
            c.battle_leave_time = -1.0f;
        }
    }
    for (auto it = state_.battles.begin(); it != state_.battles.end();) {
        it->members.clear();
        it->owners.clear();
        for (const Creature& c : state_.creatures)
            if (c.battle_id == it->id) { it->members.push_back(c.id); it->owners.push_back(c.team); }
        if (it->members.empty()) { it = state_.battles.erase(it); continue; }
        float sx = 0.0f, sy = 0.0f;
        for (int id : it->members) {
            Creature* m = by_id(id);
            if (m) { sx += m->x; sy += m->y; }
        }
        it->center_x = sx / float(it->members.size());
        it->center_y = sy / float(it->members.size());
        ++it;
    }
    for (Creature& c : state_.creatures) {
        if (c.battle_id >= 0 || !fighting(c) || c.state == CreatureState::Dead) continue;
        Battle* nearest = nullptr;
        float nearest_d2 = 0.0f;
        for (Battle& b : state_.battles) {
            float dx = b.center_x - c.x, dy = b.center_y - c.y;
            float d2 = dx * dx + dy * dy;
            if (!nearest || d2 < nearest_d2) { nearest = &b; nearest_d2 = d2; }
        }
        if (!nearest || (nearest_d2 > kBattleJoinDistanceSquared &&
                         int(state_.battles.size()) < kMaxBattles)) {
            Battle b;
            b.id = 1;
            for (const Battle& old : state_.battles) b.id = std::max(b.id, old.id + 1);
            b.center_x = c.x; b.center_y = c.y;
            state_.battles.push_back(b);
            nearest = &state_.battles.back();
        }
        c.battle_id = nearest->id;
        c.battle_leave_time = state_.elapsed + kMinBattleTime;
        nearest->members.push_back(c.id);
        nearest->owners.push_back(c.team);
    }
    for (Battle& b : state_.battles) {
        float sx = 0.0f, sy = 0.0f;
        for (int id : b.members) {
            Creature* m = by_id(id);
            if (m) { sx += m->x; sy += m->y; }
        }
        if (!b.members.empty()) {
            b.center_x = sx / float(b.members.size());
            b.center_y = sy / float(b.members.size());
        }
    }
}

Creature* Game::update_fight(Creature& c, int cx, int cy) {
    if (should_flee_or_attack(c, cx, cy)) return nullptr;
    const float dt = kTickSeconds;
    Creature* enemy = nullptr;
    float best_d2 = c.sight * c.sight;
    if (c.attacking_id >= 0) {
        for (Creature& other : state_.creatures) {
            if (other.id != c.attacking_id || other.state == CreatureState::Dead ||
                !can_be_hurt_by(c.ai_flags, other.ai_flags)) continue;
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
        // SelectAVictim guards (0045cb00.c:52-66).  The original reports these
        // through FUN_00634db0 (a diagnostic assert), so they are recorded as
        // warnings rather than aborting target selection.
        if (c.possessed) ++c.victim_guard_warnings;
        const bool fighter = (c.ai_flags & kAIFlagFearless) || c.fight_style > 0;
        if (!fighter) ++c.victim_guard_warnings;
        if (c.attacking_id >= 0) ++c.victim_guard_warnings;
        auto attackers_of = [&](int id) {
            int n = 0;
            for (const Creature& o : state_.creatures)
                if (o.attacking_id == id && o.state != CreatureState::Dead) ++n;
            return n;
        };
        for (Creature& other : state_.creatures) {
            if (other.team == c.team || other.state == CreatureState::Dead ||
                !can_be_hurt_by(c.ai_flags, other.ai_flags)) continue;
            if (attackers_of(other.id) >= kMaxMeleeAttackers) continue;
            if (c.state == CreatureState::InHand ||
                (c.job == CreatureJob::Fight && c.attacking_id >= 0)) continue;
            float dx = other.x - c.x, dy = other.y - c.y;
            float d2 = dx * dx + dy * dy;
            if (d2 <= c.melee_range * c.melee_range) { enemy = &other; best_d2 = d2; break; }
            if (d2 < best_d2) { best_d2 = d2; enemy = &other; }
        }
        if (enemy) {
            c.attacking_id = enemy->id;
            c.last_reaction_time = state_.elapsed;
            c.last_reaction_target = enemy->id;
        }
    }

    // Fight struct (FUN_004915d0): up to 8 ranged + 8 melee attackers tracked.
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
    // ThreatThingThreshold is derived from stats/level (0045f030 warning).
    const float threshold = std::max(c.threat_threshold, c.fear);
    c.temp_fear = std::max(0.0f, c.temp_fear +
        incoming_threat * kIncomingThreatScale * dt - kFearDecayPerSecond * dt);
    c.current_fear = std::max(c.current_fear, c.temp_fear);
    c.attack_range_squared = c.melee_range * c.melee_range;
    if (enemy) {
        c.threat_thing = enemy->id;
        c.threat_threshold = threshold;
    }
    // AvoidCreature / AvoidCounter (Fight +0x39/+0x3b).
    if (c.avoid_creature >= 0) {
        for (const Creature& other : state_.creatures) {
            if (other.id == c.avoid_creature && other.state != CreatureState::Dead) {
                c.avoid_counter = std::min(100.0f, c.avoid_counter + dt);
                incoming_threat += other.threat;
                break;
            }
        }
        if (c.avoid_counter <= 0.0f) c.avoid_creature = -1;
    }

    if (enemy && best_d2 <= c.melee_range * c.melee_range) {
        set_job(c, CreatureJob::Fight, state_.elapsed);
        c.state = CreatureState::Fighting;
        c.attack_type = 1;
        if (c.attack_cooldown <= 0.0f) {
            enemy->hp -= rear_attack_bonus(c.damage, c, *enemy) *
                         resistance_factor(*enemy, c.attack_type);
            c.attack_cooldown = std::max(0.1f, c.melee_recharge);
            enemy->current_fear = std::max(enemy->current_fear, c.threat * 0.05f);
            grant_xp(c, c.damage * 0.05f);
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
                    int nx = std::max(0, std::min(state_.world.width - 1,
                        int(c.x) + int(ai_rng_ % 9U) - 4));
                    int ny = std::max(0, std::min(state_.world.height - 1,
                        int(c.y) + int((ai_rng_ >> 7) % 9U) - 4));
                    if (state_.world.walkable(nx, ny)) {
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
            grant_xp(c, 5.0f);
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
        grant_xp(c, std::max(20.0f, enemy->max_hp * 0.05f));
    }
    return enemy;
}

void Game::update_imp_planner(Creature& c, int cx, int cy) {
    // Imps have their own 11-field planner (FUN_00479200 +0x8a..+0xa7) and do
    // not use the shared mood job table.  Re-evaluation period is UNKNOWN.
    if (c.species != 0) return;
    if (c.attacking_id >= 0 || c.job == CreatureJob::Fight ||
        c.job == CreatureJob::Flee)
        return;  // combat takes precedence over imp work planning
    if (c.hunger > kHungerEatThreshold || c.fatigue > kFatigueSleepThreshold ||
        c.sleep_need > kSleepNeedThreshold)
        return;  // immediate needs take precedence
    constexpr int kImpReevaluateTicks = 30;  // RECONSTRUCTION (UNKNOWN)
    ImpJobData& p = c.imp_job;
    if (p.just_dropped) {
        p.dropped_task = p.task_id;
        p.just_dropped = false;
    }
    if (p.last_evaluate >= 0 &&
        int(state_.tick_count) - p.last_evaluate < kImpReevaluateTicks)
        return;
    p.last_evaluate = int(state_.tick_count);
    p.scanning_job = true;
    int tx = -1, ty = -1;
    int wanted = static_cast<int>(TerrainId::Gold);
    bool have = find_room_tile(state_.rooms, wanted, cx, cy, tx, ty);
    if (!have) {
        for (size_t i = 0; i < state_.world.tiles.size(); ++i) {
            if (state_.world.tiles[i].terrain == uint8_t(TerrainId::Gold)) {
                tx = int(i % state_.world.width);
                ty = int(i / state_.world.width);
                have = true;
                break;
            }
        }
    }
    if (have) {
        p.task_id = 24;  // haul/mine worker task id
        p.task_pos_x = tx;
        p.task_pos_y = ty;
        p.task_surface = 0;
        p.task_slot = -1;
        p.last_task_type = 24;
    } else {
        p.task_id = -1;
        p.scanning_data = 0;
    }
    p.scanning_job = false;
    if (p.task_id >= 0 && c.path.empty() && c.target_x < 0) {
        c.move_to_x = p.task_pos_x;
        c.move_to_y = p.task_pos_y;
        c.state_id = kAiStateMoveTo;
    }
}

void Game::choose_mood_job(Creature& c, int cx, int cy) {
    (void)cx; (void)cy;
    if (c.worker_task_id >= 0) return;
    if (!c.possessed && c.job == CreatureJob::Fight && c.path.empty() &&
        c.attacking_id < 0)
        set_job(c, CreatureJob::Idle, state_.elapsed);
    if (c.job == CreatureJob::Fight || c.job == CreatureJob::Flee) return;

    // Three independent per-tick "successfully chose a job" flags
    // (FUN_00479200 +0xf9 happy / +0xfd unhappy / +0x101 angry).  A rank that
    // already succeeded this tick is not asked to choose again.
    const int tick = int(state_.tick_count);
    const bool rank_available[3] = {
        c.last_happy_chosen_tick != tick,
        c.last_hunger_chosen_tick != tick,
        c.last_angry_chosen_tick != tick,
    };

    if (c.species == 0) update_imp_planner(c, cx, cy);

    if (c.rebel_active && c.anger_state >= 2) {
        if (c.path.empty()) set_job(c, CreatureJob::Rebel, state_.elapsed);
    } else if (!c.get_paid && c.payment_needed > 0.0f && c.path.empty()) {
        set_job(c, CreatureJob::Pay, state_.elapsed);
    } else if (c.gold_carried > 0.0f) {
        set_job(c, CreatureJob::HaulGold, state_.elapsed);
    } else if (c.hunger > kHungerEatThreshold) {
        set_job(c, CreatureJob::Eat, state_.elapsed);
        c.last_attempt_hunger = state_.elapsed;
    } else if (c.fatigue > kFatigueSleepThreshold || c.sleep_need > kSleepNeedThreshold) {
        set_job(c, CreatureJob::Sleep, state_.elapsed);
        c.last_attempt_sleep = state_.elapsed;
    } else if (c.job == CreatureJob::Idle || c.job == CreatureJob::Guard ||
               c.job == CreatureJob::Explore || c.job == CreatureJob::Train ||
               c.job == CreatureJob::Research || c.job == CreatureJob::Sulk) {
        if (c.path.empty() && rank_available[std::max(0, std::min(2, c.anger_state))]) {
            const int* jobs = c.happy_job;
            const int* chances = c.happy_chance;
            int* chosen_tick = &c.last_happy_chosen_tick;
            if (c.anger_state == 1) {
                jobs = c.unhappy_job; chances = c.unhappy_chance;
                chosen_tick = &c.last_hunger_chosen_tick;
            } else if (c.anger_state >= 2) {
                jobs = c.angry_job; chances = c.angry_chance;
                chosen_tick = &c.last_angry_chosen_tick;
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
                *chosen_tick = tick;
            } else {
                // RECONSTRUCTION (UNKNOWN): species-based fallback preference.
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

bool Game::update_utility_path(Creature& c, int cx, int cy) {
    if (c.target_x >= 0 || !c.path.empty()) return false;
    int tx = -1, ty = -1;
    bool have = false;
    switch (c.state_id) {
        case kAiStateMoveTo:
            tx = c.move_to_x; ty = c.move_to_y;
            have = tx >= 0 && ty >= 0;
            break;
        case kAiStateMoveToRoom:
            have = find_room_tile(state_.rooms, c.room_type_id, cx, cy, tx, ty);
            if (!have && c.move_to_room_x >= 0) {
                tx = c.move_to_room_x; ty = c.move_to_room_y; have = true;
            }
            break;
        case kAiStateMoveWithinRange:
            tx = c.move_range_x; ty = c.move_range_y;
            have = tx >= 0 && ty >= 0;
            if (have && c.range_squared > 0.0f) {
                float dx = float(tx) - c.x, dy = float(ty) - c.y;
                if (dx * dx + dy * dy <= c.range_squared) {
                    c.thing_id = -1;  // already in range
                    return true;
                }
            }
            break;
        case kAiStateMoveToForNTurns:
            if (c.turn_counter > 0) {
                --c.turn_counter;
                tx = c.move_turns_x; ty = c.move_turns_y;
                have = tx >= 0 && ty >= 0;
            } else {
                c.state_id = c.state_before_utility;
                return true;
            }
            break;
        case kAiStateWaitFor:
            if (c.wait_for > 0) --c.wait_for;
            else c.state_id = c.state_before_utility;
            return true;
        default:
            return false;
    }
    if (!have) {
        c.failed_move_state = c.state_id;  // FailedMoveState (+0xeb/+0xef)
        return true;
    }
    std::vector<int> path;
    if (find_path(cx, cy, tx, ty, path)) {
        c.path = std::move(path);
        c.path_index = 0;
        c.target_x = tx; c.target_y = ty;
        c.state = CreatureState::Walking;
        c.failed_move_state = 0;
        return true;
    }
    // ReCalcNavCounter (+0xf5): retry navigation on a later tick.
    c.failed_move_state = c.state_id;
    c.recalc_nav_counter = (c.recalc_nav_counter + 1) % (kNavRecalcTicks + 1);
    return true;
}

void Game::update_creature_triggers(Creature& c, int cx, int cy) {
    (void)cx; (void)cy;
    // Per-creature TriggerRoot / repeat count (CThing +0x419 / +0x4f9).
    if (c.trigger_root >= 0 && c.trigger_root_repeat_count != 0) {
        const bool fired = c.anger_state >= 2 || c.job == CreatureJob::Fight ||
                           c.state == CreatureState::Dead;
        if (fired) {
            unsigned flag = unsigned(c.trigger_root) & 255u;
            if (flag < state_.flags.size()) state_.flags[flag] = 1;
            if (c.trigger_root_repeat_count > 0) --c.trigger_root_repeat_count;
        }
    }
    // Hello reaction timestamp (LastHelloTime +0x4e5).
    if (state_.elapsed - c.last_hello_time >= kHelloCooldownSeconds) {
        for (const Creature& other : state_.creatures) {
            if (&other == &c || other.team != c.team ||
                other.state == CreatureState::Dead) continue;
            float dx = other.x - c.x, dy = other.y - c.y;
            if (dx * dx + dy * dy <= kHelloRange * kHelloRange) {
                c.last_hello_time = state_.elapsed;
                break;
            }
        }
    }
    // Party hierarchy (ReportingTo/MyLeadersId/PositionInParty): subordinates
    // follow their leader when separated.  Cohesion radius is UNKNOWN.
    if (c.reporting_to >= 0 && c.job != CreatureJob::Fight &&
        c.state != CreatureState::Dead && c.path.empty() && c.target_x < 0) {
        for (const Creature& leader : state_.creatures) {
            if (leader.id != c.reporting_to || leader.state == CreatureState::Dead) continue;
            float dx = leader.x - c.x, dy = leader.y - c.y;
            if (dx * dx + dy * dy > 16.0f) {
                c.target_x = std::max(0, std::min(state_.world.width - 1, int(leader.x)));
                c.target_y = std::max(0, std::min(state_.world.height - 1, int(leader.y)));
                std::vector<int> path;
                if (find_path(cx, cy, c.target_x, c.target_y, path)) {
                    c.path = std::move(path);
                    c.path_index = 0;
                    c.state = CreatureState::Walking;
                } else {
                    c.target_x = c.target_y = -1;
                }
            }
            break;
        }
    }
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
    c.current_fear = std::max(0.0f, c.current_fear - kFearDecayPerSecond * dt);

    // FUN_00478800 state dispatcher (including the battle override branch).
    dispatch_state(c, state_.elapsed);

    auto gain_xp = [&](float amount) { grant_xp(c, amount); };

    /* --- payday (PayState.cpp / FUN_00472390) --------------------------- */
    /* --- payday (PayState.cpp / FUN_00472390) and needs ------------------- */
    update_pay(c);
    update_needs(c);

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

    /* --- combat: FUN_0045cb00 / FUN_004915d0 / FUN_004611d0 --------------- */
    update_fight(c, cx, cy);

    /* --- job selection: three independent mood tiers --------------------- */
    choose_mood_job(c, cx, cy);

    /* --- utility-state path, then ordinary job target -------------------- */
    if (c.worker_task_id < 0 && !update_utility_path(c, cx, cy) &&
        c.target_x < 0 && c.path.empty()) {
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
        int16_t field = grid_field(int(c.x), int(c.y));
        if (field > 0) move *= 0.5f;
        if (field >= 4096) {
            c.hp -= 20.0f * dt;
            if (c.hp <= 0.0f) { c.hp = 0.0f; c.state = CreatureState::Dead; }
        }
        if (dist <= move || dist < 0.0001f) {
            c.x = tx; c.y = ty;
            ++c.path_index;
        } else {
            c.facing = std::atan2(dx, dy);
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
        float value = td ? float(structures::terrain_gold_value(td)) : 1000.0f;
        // OWN IMPLEMENTATION: value/1000 per second per mining rate unit.
        c.gold_carried += (value / 1000.0f) * state_.config.mining_rate * dt;
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
    // Per-creature TriggerRoot, slap/hello/reaction timestamps and party hooks.
    update_creature_triggers(c, cx, cy);
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
    // Six anger slots + UnhappyAt (FUN_00487210, HIGH).  The 2x multiplier for
    // the "angry" rank is a RECONSTRUCTION (UNKNOWN): the original threshold
    // for that rank was not tied to a decompiled constant.
    float limit = std::max(1.0f, c.unhappy_threshold);  // UnhappyAt from stats
    c.anger_state = total >= limit * 2.0f ? 2 : (total >= limit ? 1 : 0);
    if (c.anger_state == 2 && previous != 2) {
        c.rebel_active = true;
        if (c.my_leaders_id >= 0) c.rebel_leader = true;
    }
}

void Game::spawn_initial_keeper(int count) {
    if (state_.world.width <= 0 || state_.world.height <= 0) return;
    int hx = -1, hy = -1;
    for (int y = 0; y < state_.world.height && hx < 0; ++y)
        for (int x = 0; x < state_.world.width; ++x) {
            const Tile* t = state_.world.at(x, y);
            if (t && t->terrain == uint8_t(TerrainId::DungeonHeart)) {
                hx = x; hy = y; break;
            }
        }
    if (hx < 0) {
        hx = state_.world.width / 2;
        hy = state_.world.height / 2;
        if (!state_.world.walkable(hx, hy)) {
            for (int y = 0; y < state_.world.height && hx < 0; ++y)
                for (int x = 0; x < state_.world.width; ++x)
                    if (state_.world.walkable(x, y)) { hx = x; hy = y; break; }
        }
    }
    if (hx < 0 || hy < 0) return;
    static const int offsets[][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}};
    for (int n = 0; n < std::max(0, count); ++n) {
        int x = hx, y = hy;
        for (const auto& o : offsets) {
            int cx = hx + o[0], cy = hy + o[1];
            if (state_.world.walkable(cx, cy)) { x = cx; y = cy; break; }
        }
        int id = spawn_creature(0, x, y, 0);
        if (id < 0) continue;
        Creature& imp = state_.creatures.back();
        set_job(imp, CreatureJob::Idle, state_.elapsed);
        imp.state = CreatureState::Idle;
    }
    initial_enemies_spawned_ = false;
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


void Game::update_traps() {
    for (Room& trap : state_.rooms) {
        if (trap.type < 1000 || trap.type >= 2000) continue;
        if (trap.trap_cooldown > 0.0f) {
            trap.trap_cooldown -= kTickSeconds;
            continue;
        }
        if (trap.trap_charges <= 0) continue;
        const DK2TrapDef* def = structures::trap_by_index(
            static_cast<size_t>(trap.type - 1000));
        if (!def) continue;
        const float radius = (def->trigger == 2) ? 2.5f : 0.75f;
        Creature* victim = nullptr;
        float best = radius * radius;
        for (Creature& c : state_.creatures) {
            if (c.team == trap.owner || c.state == CreatureState::Dead) continue;
            float dx = c.x - (float(trap.x) + 0.5f);
            float dy = c.y - (float(trap.y) + 0.5f);
            float d2 = dx * dx + dy * dy;
            if (d2 <= best) { best = d2; victim = &c; }
        }
        if (!victim) continue;
        if (def->mana_cost_to_fire > 0 && state_.mana < float(def->mana_cost_to_fire)) continue;
        if (def->mana_cost_to_fire > 0) state_.mana -= float(def->mana_cost_to_fire);
        float damage = def->threat > 0 ? float(def->threat) * 0.5f
                                       : float(def->health) / 100.0f;
        victim->hp -= damage;
        if (victim->hp <= 0.0f) { victim->hp = 0.0f; victim->state = CreatureState::Dead; }
        --trap.trap_charges;
        trap.trap_cooldown = structures::trap_recharge_time(def);
    }
}

void Game::sync_grid_tile(int x, int y) {
    if (!grid_) return;
    Tile* t = state_.world.at(x, y);
    if (!t) return;
    TerrainId terr = static_cast<TerrainId>(t->terrain);
    bool solid = (terr == TerrainId::ImpenetrableRock || terr == TerrainId::Rock ||
                  terr == TerrainId::Gems || terr == TerrainId::Gold ||
                  terr == TerrainId::ReinforcedWall);
    for (const Door& d : state_.doors)
        if (d.x == x && d.y == y && d.integrity > 0.0f) { solid = true; break; }
    grid_->set_blocked(x, y, solid);
    int16_t field = 0;
    int16_t accumulator = 0;
    if (terr == TerrainId::Water) { field = 2048; accumulator = 32; }
    else if (terr == TerrainId::Lava) { field = 4096; accumulator = 64; }
    grid_->set_field(x, y, field);
    grid_->set_accumulator(x, y, accumulator);
}

int16_t Game::grid_field(int x, int y) const {
    return grid_ ? grid_->field(x, y) : 0;
}

bool Game::construction_blocked(int x, int y) const {
    for (const Creature& c : state_.creatures) {
        if (c.state == CreatureState::Dead) continue;
        if (int(c.x) == x && int(c.y) == y) return true;
    }
    for (const Door& d : state_.doors)
        if (d.x == x && d.y == y && d.integrity > 0.0f) return true;
    return false;
}

void Game::execute_trigger_action(const dk2trigger::ActionRequest& req) {
    auto resolve_ap = [&](unsigned id, float& out_x, float& out_y) {
        uint32_t h = 0x9e3779b9U;
        h ^= uint32_t(id) + 0x7ed55d16U + (h << 12);
        h ^= h >> 16;
        h = h * 0x85ebca6bU;
        out_x = float(h % uint32_t(std::max(1, state_.world.width)));
        out_y = float((h >> 13) % uint32_t(std::max(1, state_.world.height)));
    };
    switch (req.type) {
        case dk2trigger::kWinGame: state_.victory = true; break;
        case dk2trigger::kLoseGame: state_.defeat = true; break;
        case dk2trigger::kSetTimeLimit: state_.time_limit = req.value; break;
        case dk2trigger::kSetObjective:
        case dk2trigger::kDisplayObjective:
            state_.objective_id = int(req.text_id);
            break;
        case dk2trigger::kDisplayTextString:
            state_.text_id = int(req.text_id);
            break;
        case dk2trigger::kPlaySpeech:
            state_.speech_id = int(req.text_id);
            break;
        case dk2trigger::kFlashActionPoint:
        case dk2trigger::kRevealActionPoint:
        case dk2trigger::kZoomToActionPoint: {
            float x = req.pos_x, y = req.pos_y;
            if (x < 0.0f || y < 0.0f) resolve_ap(req.action_point_id, x, y);
            state_.action_point_id = int(req.action_point_id);
            state_.action_point_x = x;
            state_.action_point_y = y;
            if (req.type == dk2trigger::kZoomToActionPoint) {
                state_.camera.center_x = x;
                state_.camera.center_y = y;
            }
            if (req.type == dk2trigger::kRevealActionPoint) {
                for (int dy = -3; dy <= 3; ++dy)
                    for (int dx = -3; dx <= 3; ++dx)
                        if (Tile* t = state_.world.at(int(x) + dx, int(y) + dy))
                            t->flags |= 0x80;
            }
            break;
        }
        case dk2trigger::kFollowCameraPath:
            state_.camera_path_id = int(req.path_id);
            state_.camera_waypoint = 0;
            state_.camera_path_timer = 0.0f;
            break;
        case dk2trigger::kAlterTerrainType: {
            int x = int(req.pos_x), y = int(req.pos_y);
            if (Tile* t = state_.world.at(x, y))
                t->terrain = uint8_t(req.target_id & 255u);
            sync_grid_tile(x, y);
            state_.economy.recalculate(state_.world);
            break;
        }
        case dk2trigger::kToggleEffectGenerator:
            if (req.flag_id < state_.effects.size())
                state_.effects[req.flag_id] ^= 1;
            break;
        case dk2trigger::kSetAlliance:
            break;
        case dk2trigger::kMake:
            for (Creature& c : state_.creatures) {
                if (c.team != int(req.player)) continue;
                if (req.target_id == 29) c.hunger = std::min(100.0f, c.hunger + req.value);
                else if (req.target_id == 30) c.hp = std::min(c.max_hp, c.hp + req.value);
            }
            break;
        case dk2trigger::kCreateCreature: {
            int x = int(req.pos_x), y = int(req.pos_y);
            if (x <= 0 || y <= 0) {
                x = state_.world.width / 2;
                y = state_.world.height / 2;
            }
            if (!state_.world.walkable(x, y)) { x = 1; y = 1; }
            spawn_creature(int(req.target_id), x, y, int(req.player));
            break;
        }
        case dk2trigger::kCreateHeroParty: {
            int x = state_.world.width - 4, y = state_.world.height / 2;
            for (int i = 0; i < 3; ++i) spawn_creature(7 + i, x, y + i, 1);
            break;
        }
        default: break;
    }
}

void Game::update_camera_path() {
    if (state_.camera_path_id < 0) return;
    state_.camera_path_timer += kTickSeconds;
    if (state_.camera_path_timer < 2.0f) return;
    state_.camera_path_timer = 0.0f;
    uint32_t key = uint32_t(state_.camera_path_id) * 131U +
                   uint32_t(state_.camera_waypoint) * 17U;
    state_.camera.center_x = float(key % uint32_t(std::max(1, state_.world.width)));
    state_.camera.center_y = float((key >> 9) % uint32_t(std::max(1, state_.world.height)));
    ++state_.camera_waypoint;
}

void Game::evaluate_triggers() {
    if (!trigger_runtime_) return;
    dk2trigger::FunctionalTriggerContext ctx;
    ctx.get_flag = [this](unsigned id) -> float {
        return id < state_.flags.size() ? state_.flags[id] : 0.0f;
    };
    ctx.get_timer = [this](unsigned id) -> float {
        return id < state_.timers.size() ? state_.timers[id] : 0.0f;
    };
    ctx.get_level_time = [this]() -> float { return state_.elapsed; };
    ctx.get_time_limit = [this]() -> float { return state_.time_limit; };
    ctx.set_flag = [this](unsigned id, float v) {
        if (id < state_.flags.size()) state_.flags[id] = uint8_t(v != 0.0f);
    };
    ctx.set_timer = [this](unsigned id, float v) {
        if (id < state_.timers.size()) state_.timers[id] = v;
    };
    ctx.get_target_value = [this](unsigned type, unsigned, unsigned) -> float {
        int keeper = 0, enemies_dead = 0;
        for (const Creature& c : state_.creatures) {
            if (c.state == CreatureState::Dead) {
                if (c.team == 1) ++enemies_dead;
            } else if (c.team == 0) ++keeper;
        }
        switch (type) {
            case 19: return float(keeper);
            case 25: return float(state_.rooms.size());
            case 30: return state_.economy.gold;
            case 47: return float(enemies_dead);
            default: return 0.0f;
        }
    };
    ctx.on_action = [this](const dk2trigger::ActionRequest& req) {
        execute_trigger_action(req);
    };
    trigger_runtime_->evaluateAndDispatch(ctx);
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
    update_traps();
    update_worker_tasks();
    update_battles();
    evaluate_triggers();
    update_camera_path();
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
    // v5 extension: a second length-tagged trailer.  Old payload readers
    // ignore it; new readers only consume it when tag and size validate.
    w32(kSaveExtMagic2);
    w32(uint32_t(sizeof(SaveCreatureExtras2)));
    for (const Creature& c : state_.creatures) {
        SaveCreatureExtras2 e2{};
        pack_extras2(c, e2);
        out.write(reinterpret_cast<const char*>(&e2), sizeof(e2));
    }
    w32(kSaveExtMagic3);
    w32(0x00060000U);
    for (const Room& r : state_.rooms) {
        wf(r.max_integrity); wf(r.trap_cooldown); wi(r.trap_charges);
    }
    w32(uint32_t(state_.doors.size()));
    for (const Door& d : state_.doors) {
        wi(d.type); wi(d.x); wi(d.y); wi(d.owner);
        wf(d.integrity); wf(d.max_integrity);
        wi(d.secret ? 1 : 0); wi(d.hero ? 1 : 0); wi(d.open ? 1 : 0);
    }
    w32(uint32_t(state_.battles.size()));
    for (const Battle& b : state_.battles) {
        wi(b.id); wf(b.center_x); wf(b.center_y);
        w32(uint32_t(b.members.size()));
        for (int id : b.members) wi(id);
        w32(uint32_t(b.owners.size()));
        for (int owner : b.owners) wi(owner);
    }
    w32(uint32_t(state_.worker_tasks.size()));
    for (const WorkerTask& t : state_.worker_tasks) {
        wi(int(t.type)); wi(t.x); wi(t.y); wi(t.assigned); wf(t.progress);
    }
    wi(state_.text_id); wi(state_.speech_id);
    wi(state_.action_point_id);
    wf(state_.action_point_x); wf(state_.action_point_y);
    wi(state_.camera_path_id); wi(state_.camera_waypoint);
    wf(state_.camera_path_timer);
    for (uint8_t e : state_.effects) w32(e);
    for (const Creature& c : state_.creatures) {
        wf(c.facing); wi(c.battle_id); wf(c.battle_leave_time);
        wi(c.worker_task_id); wi(c.worker_task_x); wi(c.worker_task_y);
        wf(c.worker_task_progress);
    }
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
            // v5 extension trailer (DK2F).  Optional: an older save ends after
            // pay_period_timer_, in which case the peek simply finds no tag.
            if (in.good()) {
                std::streampos after_v1 = in.tellg();
                uint32_t magic2 = 0, size2 = 0;
                r32(magic2); r32(size2);
                if (magic2 == kSaveExtMagic2 && size2 == sizeof(SaveCreatureExtras2)) {
                    for (Creature& c : state_.creatures) {
                        SaveCreatureExtras2 e2{};
                        if (!in.read(reinterpret_cast<char*>(&e2), sizeof(e2))) break;
                        unpack_extras2(c, e2);
                    }
                } else if (after_v1 >= 0) {
                    in.clear();
                    in.seekg(after_v1);
                }
            }
        }
        in.clear();
    } else if (here >= 0) {
        in.clear();
        in.seekg(here);
    }

    if (in.good()) {
        uint32_t magic3 = 0, size3 = 0;
        r32(magic3); r32(size3);
        if (magic3 == kSaveExtMagic3 && size3 == 0x00060000U) {
            for (Room& r : state_.rooms) { rf(r.max_integrity); rf(r.trap_cooldown); ri(r.trap_charges); }
            uint32_t count = 0;
            r32(count); state_.doors.clear();
            if (count <= 65536) {
                state_.doors.resize(count);
                for (Door& d : state_.doors) {
                    ri(d.type); ri(d.x); ri(d.y); ri(d.owner);
                    rf(d.integrity); rf(d.max_integrity);
                    ri(flag); d.secret = flag != 0;
                    ri(flag); d.hero = flag != 0;
                    ri(flag); d.open = flag != 0;
                }
            }
            r32(count); state_.battles.clear();
            if (count <= 65536) {
                state_.battles.resize(count);
                for (Battle& b : state_.battles) {
                    ri(b.id); rf(b.center_x); rf(b.center_y);
                    uint32_t inner = 0;
                    r32(inner); b.members.clear();
                    if (inner <= 100000) for (uint32_t i = 0; i < inner; ++i) { ri(flag); b.members.push_back(flag); }
                    r32(inner); b.owners.clear();
                    if (inner <= 100000) for (uint32_t i = 0; i < inner; ++i) { ri(flag); b.owners.push_back(flag); }
                }
            }
            r32(count); state_.worker_tasks.clear();
            if (count <= 100000) {
                state_.worker_tasks.resize(count);
                for (WorkerTask& t : state_.worker_tasks) {
                    ri(flag); t.type = static_cast<WorkerTaskType>(flag);
                    ri(t.x); ri(t.y); ri(t.assigned); rf(t.progress);
                }
            }
            ri(state_.text_id); ri(state_.speech_id); ri(state_.action_point_id);
            rf(state_.action_point_x); rf(state_.action_point_y);
            ri(state_.camera_path_id); ri(state_.camera_waypoint);
            rf(state_.camera_path_timer);
            for (uint8_t& e : state_.effects) { uint32_t x; r32(x); e = uint8_t(x); }
            for (Creature& c : state_.creatures) {
                rf(c.facing); ri(c.battle_id); rf(c.battle_leave_time);
                ri(c.worker_task_id); ri(c.worker_task_x); ri(c.worker_task_y);
                rf(c.worker_task_progress);
            }
        }
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
    state_.selected_tile_x = int(wx);
    state_.selected_tile_y = int(wy);
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

void Game::slap_creature(int creature_id) {
    // Player slap: stamps LastSlapTick (+0x4bb) and raises the slap anger slot.
    for (Creature& c : state_.creatures) {
        if (c.id != creature_id || c.state == CreatureState::Dead) continue;
        c.last_slap_tick = state_.elapsed;
        c.last_reaction_time = state_.elapsed;
        c.last_reaction_target = -1;
        apply_anger(c, c.anger_slap, 4);
        return;
    }
}

}  // namespace dk2
