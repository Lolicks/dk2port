#include "TriggerRuntime.h"

#include <algorithm>
#include <utility>

namespace dk2trigger {
namespace {

constexpr unsigned kSyntheticBase = 0x80000000u;
constexpr unsigned kFlagLiteral = 1u;
constexpr unsigned kLevelScoreFlag = 128u;
constexpr unsigned kTimeLimitTimer = 16u;

const DK2TriggerNode* findNode(const DK2MissionTriggers& set, unsigned id) {
    return findDK2TriggerNode(set, id);
}

float resolveBase(const ActionRequest& request, ITriggerContext& ctx) {
    if (request.flag_id == kLevelScoreFlag) return ctx.getLevelScore();
    return ctx.getFlag(request.flag_id);
}

}  // namespace

bool compareValues(float target, Comparison op, float value) {
    switch (op) {
        case Comparison::kLess:       return target < value;
        case Comparison::kLessEqual:  return target <= value;
        case Comparison::kEqual:      return target == value;
        case Comparison::kGreater:    return target > value;
        case Comparison::kGreaterEqual: return target >= value;
        case Comparison::kNotEqual:   return target != value;
        case Comparison::kNone:
        default:                      return false;
    }
}

// --- FunctionalTriggerContext ----------------------------------------------

float FunctionalTriggerContext::getFlag(unsigned id) {
    return get_flag ? get_flag(id) : 0.0f;
}
float FunctionalTriggerContext::getTimer(unsigned id) {
    return get_timer ? get_timer(id) : 0.0f;
}
float FunctionalTriggerContext::getLevelTime() {
    return get_level_time ? get_level_time() : 0.0f;
}
float FunctionalTriggerContext::getTargetValue(unsigned target_type,
                                               unsigned target_id,
                                               unsigned player) {
    return get_target_value ? get_target_value(target_type, target_id, player) : 0.0f;
}
float FunctionalTriggerContext::getTimeLimit() {
    return get_time_limit ? get_time_limit() : 0.0f;
}
float FunctionalTriggerContext::getLevelScore() {
    if (get_level_score) return get_level_score();
    return getFlag(kLevelScoreFlag);
}
bool FunctionalTriggerContext::resolveActionPoint(unsigned id, float& x, float& y) {
    return resolve_action_point ? resolve_action_point(id, x, y) : false;
}
void FunctionalTriggerContext::onAction(const ActionRequest& request) {
    if (on_action) on_action(request);
}
void FunctionalTriggerContext::onUnsupported(unsigned node_id, unsigned type,
                                             const char* what) {
    if (on_unsupported) on_unsupported(node_id, type, what);
}
void FunctionalTriggerContext::setFlag(unsigned id, float value) {
    if (set_flag) set_flag(id, value);
}
void FunctionalTriggerContext::setTimer(unsigned id, float value) {
    if (set_timer) set_timer(id, value);
}
void FunctionalTriggerContext::setLevelScore(float value) {
    if (set_level_score) set_level_score(value);
}

// --- FLAG action reference implementation ----------------------------------

float resolveFlagActionValue(const ActionRequest& request, ITriggerContext& ctx) {
    constexpr unsigned kValueBit = 0x002u;
    constexpr unsigned kTargetBit = 0x004u;
    constexpr unsigned kEqualBit = 0x008u;
    constexpr unsigned kPlusBit = 0x010u;
    constexpr unsigned kMinusBit = 0x020u;

    float operand = request.value;
    if (request.flag & kTargetBit) operand = ctx.getFlag(static_cast<unsigned>(request.value));
    if (request.flag & kEqualBit) return operand;
    const float base = resolveBase(request, ctx);
    if (request.flag & kPlusBit) return base + operand;
    if (request.flag & kMinusBit) return base - operand;
    if (request.flag & kValueBit) return operand;
    return operand;
}

void applyFlagAction(const ActionRequest& request, ITriggerContext& ctx) {
    const float result = resolveFlagActionValue(request, ctx);
    if (request.flag_id == kLevelScoreFlag) ctx.setLevelScore(result);
    else ctx.setFlag(request.flag_id, result);
}

// --- TriggerRuntime --------------------------------------------------------

TriggerRuntime::TriggerRuntime(const DK2MissionTriggers& set)
    : TriggerRuntime(set, deriveRoots(set)) {}

TriggerRuntime::TriggerRuntime(const DK2MissionTriggers& set,
                               std::vector<unsigned> roots)
    : set_(set), roots_(std::move(roots)) {
    build();
}

std::vector<unsigned> TriggerRuntime::deriveRoots(const DK2MissionTriggers& set) {
    std::vector<unsigned> all, referenced;
    all.reserve(set.count);
    for (unsigned i = 0; i < set.count; ++i) {
        const DK2TriggerNode& n = set.nodes[i];
        all.push_back(n.id);
        if (n.next && findNode(set, n.next)) referenced.push_back(n.next);
        if (n.child && findNode(set, n.child)) referenced.push_back(n.child);
    }
    std::vector<unsigned> roots;
    for (unsigned id : all) {
        if (std::find(referenced.begin(), referenced.end(), id) == referenced.end())
            roots.push_back(id);
    }
    if (roots.empty()) {
        for (unsigned i = 0; i < set.count; ++i)
            if (!set.nodes[i].is_action) roots.push_back(set.nodes[i].id);
    }
    return roots;
}

void TriggerRuntime::reset() {
    build();
    stats_ = RuntimeStats{};
}

void TriggerRuntime::setRoots(std::vector<unsigned> roots) {
    roots_ = std::move(roots);
    build();
    stats_ = RuntimeStats{};
}

void TriggerRuntime::build() {
    nodes_.clear();
    states_.clear();
    synthetic_.clear();
    root_ids_.clear();

    for (unsigned i = 0; i < set_.count; ++i)
        nodes_.emplace(set_.nodes[i].id, &set_.nodes[i]);

    // Build each generic's child list: first idChild, then its idNext siblings.
    for (unsigned i = 0; i < set_.count; ++i) {
        const DK2TriggerNode& n = set_.nodes[i];
        NodeState& st = states_[n.id];
        st.repeat_left = static_cast<int>(n.repeat_times);
        st.last_index = -1;
        unsigned id = n.child;
        while (id && nodes_.count(id)) {
            if (std::find(st.children.begin(), st.children.end(), id) != st.children.end())
                break;  // malformed cycle guard
            st.children.push_back(id);
            states_[id].parent = n.id;
            id = nodes_.at(id)->next;
        }
    }

    // A synthetic root owns the root chain (root id followed by idNext).
    for (unsigned i = 0; i < roots_.size(); ++i) {
        const unsigned sid = kSyntheticBase + i;
        const unsigned rid = roots_[i];
        if (!nodes_.count(rid)) continue;
        synthetic_.push_back(sid);
        root_ids_.push_back(rid);
        NodeState& root = states_[sid];
        root.repeat_left = 0;
        root.last_index = -1;
        root.current = sid;  // OpenKeeper starts at the synthetic root
        unsigned id = rid;
        while (id && nodes_.count(id)) {
            if (std::find(root.children.begin(), root.children.end(), id) != root.children.end())
                break;
            root.children.push_back(id);
            states_[id].parent = sid;
            id = nodes_.at(id)->next;
        }
    }
}

void TriggerRuntime::detachChild(unsigned parent, unsigned child) {
    auto it = states_.find(parent);
    if (it == states_.end()) return;
    auto& kids = it->second.children;
    for (std::size_t i = 0; i < kids.size(); ++i) {
        if (kids[i] != child) continue;
        kids.erase(kids.begin() + static_cast<std::ptrdiff_t>(i));
        if (it->second.last_index > static_cast<int>(i)) --it->second.last_index;
        else if (it->second.last_index == static_cast<int>(i)) it->second.last_index = -1;
        ++stats_.nodes_detached;
        return;
    }
}

ActionRequest TriggerRuntime::makeRequest(const DK2TriggerNode& node) const {
    ActionRequest r;
    r.node_id = node.id;
    r.type = node.type;
    r.value = node.value;
    r.flag = node.flag;
    r.flag_id = node.target_id;
    r.value_id = node.value_id;
    r.text_id = node.text_id;
    r.path_id = node.path_id;
    r.action_point_id = node.action_point_id;
    r.player = node.player;
    r.target_id = node.target_id;
    r.available = node.available;
    r.pos_x = node.pos_x;
    r.pos_y = node.pos_y;
    r.repeat_times = node.repeat_times;
    return r;
}

bool TriggerRuntime::isActive(const DK2TriggerNode& node, ITriggerContext& ctx) {
    ++stats_.condition_checks;
    float target = 0.0f;
    // Layouts without a `flag` byte (LEVEL_TIME and most generic targets)
    // leave both flag and value_id at zero; those always use the literal.
    const bool literal = (node.flag == kFlagLiteral) || (node.value_id == 0);
    float value = literal ? node.value : 0.0f;

    switch (node.type) {
        case 1:  // FLAG: byte 1 = targetId, byte 3 = indirect flagId
            target = (node.target_id == kLevelScoreFlag)
                         ? ctx.getLevelScore() : ctx.getFlag(node.target_id);
            if (!literal) value = ctx.getFlag(node.value_id);
            break;
        case 2:  // TIMER: byte 1 = targetId, byte 3 = indirect timerId
            target = (node.target_id == kTimeLimitTimer)
                         ? ctx.getTimeLimit() : ctx.getTimer(node.target_id);
            if (!literal) value = ctx.getTimer(node.value_id);
            break;
        case 34:  // LEVEL_TIME: TriggerControl.java:179-182 always uses value
            target = ctx.getLevelTime();
            value = node.value;
            break;
        default:
            target = ctx.getTargetValue(node.type, node.target_id, node.player);
            if (!literal)
                value = ctx.getTargetValue(node.type, node.value_id, node.player);
            break;
    }

    const Comparison op = static_cast<Comparison>(node.comparison);
    if (op == Comparison::kNone || node.comparison > 6u) {
        ++stats_.unsupported;
        ctx.onUnsupported(node.id, node.type, "comparison");
        return false;
    }
    return compareValues(target, op, value);
}

void TriggerRuntime::step(unsigned synthetic_root, ITriggerContext& ctx,
                          std::vector<ActionRequest>& out) {
    auto root_it = states_.find(synthetic_root);
    if (root_it == states_.end()) return;
    NodeState& root = root_it->second;

    unsigned current = root.current;
    auto st_it = states_.find(current);
    if (st_it == states_.end()) {
        current = synthetic_root;
        st_it = states_.find(current);
    }
    NodeState* st = &st_it->second;
    if (st->repeat_left != 255 && st->repeat_left > 0) --st->repeat_left;
    const bool repeatable = st->repeat_left > 0;

    unsigned next = 0;
    const int start = st->last_index < 0 ? 0 : st->last_index + 1;
    for (int i = start; i < static_cast<int>(st->children.size()); ++i) {
        const unsigned id = st->children[static_cast<std::size_t>(i)];
        auto node_it = nodes_.find(id);
        if (node_it == nodes_.end()) continue;
        const DK2TriggerNode& child = *node_it->second;
        if (!child.is_action) {
            if (next == 0 && isActive(child, ctx)) {
                st->last_index = i;
                next = id;
            }
        } else {
            out.push_back(makeRequest(child));
            ++stats_.actions_emitted;
            if (!repeatable) {
                st->children.erase(st->children.begin() + static_cast<std::ptrdiff_t>(i));
                ++stats_.nodes_detached;
                if (st->last_index > i) --st->last_index;
                else if (st->last_index == i) st->last_index = -1;
                --i;
            }
        }
    }

    if (st->children.empty() && st->parent != 0) {
        next = st->parent;
        detachChild(st->parent, current);
    }

    if (next == 0) {
        st->last_index = -1;
        current = st->parent != 0 ? st->parent : synthetic_root;
    } else {
        current = next;
    }
    root.current = current;
}

std::vector<ActionRequest> TriggerRuntime::evaluate(ITriggerContext& ctx) {
    std::vector<ActionRequest> out;
    ++stats_.ticks;
    for (unsigned sid : synthetic_) step(sid, ctx, out);
    return out;
}

void TriggerRuntime::evaluateAndDispatch(ITriggerContext& ctx) {
    std::vector<ActionRequest> out = evaluate(ctx);
    for (const ActionRequest& request : out) ctx.onAction(request);
}

unsigned TriggerRuntime::currentNode(unsigned root_id) const {
    for (std::size_t i = 0; i < root_ids_.size(); ++i) {
        if (root_ids_[i] != root_id) continue;
        auto it = states_.find(synthetic_[i]);
        return it == states_.end() ? 0u : it->second.current;
    }
    return 0;
}

}  // namespace dk2trigger
