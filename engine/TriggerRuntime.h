// Standalone DK2 mission-trigger evaluator.
// No dependency on DK2.h / GameState: the host supplies state through
// ITriggerContext (or the FunctionalTriggerContext lambda adapter).
#pragma once

#include "MissionTriggers.generated.hpp"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace dk2trigger {

// TriggerGeneric.ComparisonType (KwdFile.java:31-57). 0 = NONE -> never true.
enum class Comparison : std::uint8_t {
    kNone = 0,
    kLess = 1,
    kLessEqual = 2,
    kEqual = 3,
    kGreater = 4,
    kGreaterEqual = 5,
    kNotEqual = 6,
};

bool compareValues(float target, Comparison op, float value);

// ActionType ids from KwdFile.java / TriggerAction.java (mirrored in
// tools/trigger_tool.py). Only the ones with implemented semantics are named.
enum ActionType : unsigned {
    kCreateCreature = 1,
    kDisplayObjective = 4,
    kMake = 6,
    kFlag = 7,
    kInitializeTimer = 8,
    kWinGame = 10,
    kLoseGame = 11,
    kCreateHeroParty = 14,
    kSetObjective = 15,
    kFlashActionPoint = 16,
    kRevealActionPoint = 17,
    kSetAlliance = 18,
    kAlterTerrainType = 22,
    kSetTimeLimit = 23,
    kPlaySpeech = 24,
    kDisplayTextString = 25,
    kZoomToActionPoint = 26,
    kFollowCameraPath = 31,
    kToggleEffectGenerator = 48,
};

// One action for the host to execute. All payloads are the raw decoded record
// fields; resolving text strings, action points and camera paths stays with
// the host (their numeric ids are the only durable evidence in the .kld).
struct ActionRequest {
    unsigned node_id = 0;
    unsigned type = 0;
    float value = 0.0f;
    unsigned flag = 0;             // raw flag bits / literal selector
    unsigned flag_id = 0;          // flag or timer id being modified
    unsigned value_id = 0;         // indirect value source
    unsigned text_id = 0;          // speech / objective / information / text
    unsigned path_id = 0;          // camera path
    unsigned action_point_id = 0;
    unsigned player = 0;
    unsigned target_id = 0;        // creature/room/terrain/... target
    unsigned available = 0;
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    unsigned repeat_times = 0;
};

// Host-provided state. Ids are the raw 0-based bytes from the record; flag 128
// is "level score" and timer 16 is "time limit" (TriggerControl.java:50-51).
struct ITriggerContext {
    virtual ~ITriggerContext() = default;

    // Required reads.
    virtual float getFlag(unsigned id) = 0;
    virtual float getTimer(unsigned id) = 0;
    virtual float getLevelTime() = 0;
    // Called for every TargetType other than FLAG/TIMER/LEVEL_TIME.
    virtual float getTargetValue(unsigned target_type, unsigned target_id,
                                 unsigned player) = 0;

    // Optional reads / hooks.
    virtual float getTimeLimit() { return 0.0f; }
    virtual float getLevelScore() { return getFlag(128u); }
    virtual bool resolveActionPoint(unsigned id, float& x, float& y) {
        (void)id; (void)x; (void)y; return false;
    }
    virtual void onAction(const ActionRequest& request) { (void)request; }
    virtual void onUnsupported(unsigned node_id, unsigned type,
                               const char* what) {
        (void)node_id; (void)type; (void)what;
    }

    // Optional writes, used by applyFlagAction()/the host action executor.
    virtual void setFlag(unsigned id, float value) { (void)id; (void)value; }
    virtual void setTimer(unsigned id, float value) { (void)id; (void)value; }
    virtual void setLevelScore(float value) { (void)value; }
};

// Lambda adapter so a host can plug in without subclassing ITriggerContext.
class FunctionalTriggerContext final : public ITriggerContext {
public:
    std::function<float(unsigned)> get_flag;
    std::function<float(unsigned)> get_timer;
    std::function<float()> get_level_time;
    std::function<float(unsigned, unsigned, unsigned)> get_target_value;
    std::function<float()> get_time_limit;
    std::function<float()> get_level_score;
    std::function<bool(unsigned, float&, float&)> resolve_action_point;
    std::function<void(const ActionRequest&)> on_action;
    std::function<void(unsigned, unsigned, const char*)> on_unsupported;
    std::function<void(unsigned, float)> set_flag;
    std::function<void(unsigned, float)> set_timer;
    std::function<void(float)> set_level_score;

    float getFlag(unsigned id) override;
    float getTimer(unsigned id) override;
    float getLevelTime() override;
    float getTargetValue(unsigned target_type, unsigned target_id,
                         unsigned player) override;
    float getTimeLimit() override;
    float getLevelScore() override;
    bool resolveActionPoint(unsigned id, float& x, float& y) override;
    void onAction(const ActionRequest& request) override;
    void onUnsupported(unsigned node_id, unsigned type, const char* what) override;
    void setFlag(unsigned id, float value) override;
    void setTimer(unsigned id, float value) override;
    void setLevelScore(float value) override;
};

struct RuntimeStats {
    unsigned ticks = 0;
    unsigned condition_checks = 0;
    unsigned actions_emitted = 0;
    unsigned nodes_detached = 0;
    unsigned unsupported = 0;
};

// Reference implementation of the FLAG action (TriggerControl.java:242-257,
// 384-396). flag bits: 0x002 VALUE, 0x004 TARGET, 0x008 EQUAL,
// 0x010 PLUS, 0x020 MINUS.
float resolveFlagActionValue(const ActionRequest& request, ITriggerContext& ctx);
void applyFlagAction(const ActionRequest& request, ITriggerContext& ctx);

class TriggerRuntime {
public:
    explicit TriggerRuntime(const DK2MissionTriggers& set);
    TriggerRuntime(const DK2MissionTriggers& set, std::vector<unsigned> roots);

    // Recompute roots from the graph (nodes never referenced by next/child).
    // Falls back to every condition when the graph has no unreferenced node.
    static std::vector<unsigned> deriveRoots(const DK2MissionTriggers& set);

    void reset();
    void setRoots(std::vector<unsigned> roots);

    // Advances every root by one tick. Only reads state through ctx; returned
    // requests are executed by the host (evaluateAndDispatch also invokes
    // ctx.onAction for each request).
    std::vector<ActionRequest> evaluate(ITriggerContext& ctx);
    void evaluateAndDispatch(ITriggerContext& ctx);

    const DK2MissionTriggers& set() const { return set_; }
    const std::vector<unsigned>& roots() const { return roots_; }
    const RuntimeStats& stats() const { return stats_; }

    unsigned currentNode(unsigned root_id) const;

private:
    struct NodeState {
        std::vector<unsigned> children;  // ids, in idNext order
        unsigned parent = 0;             // 0 for a synthetic root
        int repeat_left = 0;
        int last_index = -1;
        unsigned current = 0;            // synthetic roots only
    };

    void build();
    void step(unsigned synthetic_root, ITriggerContext& ctx,
              std::vector<ActionRequest>& out);
    bool isActive(const DK2TriggerNode& node, ITriggerContext& ctx);
    ActionRequest makeRequest(const DK2TriggerNode& node) const;
    void detachChild(unsigned parent, unsigned child);

    const DK2MissionTriggers& set_;
    std::vector<unsigned> roots_;
    std::vector<unsigned> synthetic_;
    std::vector<unsigned> root_ids_;
    std::unordered_map<unsigned, const DK2TriggerNode*> nodes_;
    std::unordered_map<unsigned, NodeState> states_;  // synthetic + real ids
    RuntimeStats stats_;
};

}  // namespace dk2trigger
