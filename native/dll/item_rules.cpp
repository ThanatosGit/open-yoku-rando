// The `onpickup <item>` rules of randomizer_scripts.csv (e.g. the Underbelly key setting "has key"), which the game
// only runs for its own pickups. Run here for held items without a save marker; the marker is set only when applied
// directly, because the game's queue for unloaded levels is not saved.

#include "game.hpp"
#include "log.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <vector>

namespace oyr::game {
namespace {

// One rule, as the randomizer state keeps it.
struct ItemRule {
    int32_t target;  // level number << 16 | object id
    uint8_t unknown_0[0x4];
    std::string item;
    std::string action;
};
static_assert(sizeof(ItemRule) == 0x48);

// An action item_rule_action queued for a level that is not loaded.
struct PendingAction {
    int32_t target;
    uint8_t unknown_0[0x4];
    std::string action;
};
static_assert(sizeof(PendingAction) == 0x28);

constexpr uintptr_t kStateItemRules = 0x38;        // std::vector<ItemRule>
constexpr uintptr_t kStatePendingActions = 0xa0;   // std::vector<PendingAction>, emptied as levels load
constexpr uintptr_t kGlobalsLevelObject = 0x1f80;  // the level being played
constexpr uintptr_t kLevelMainMap = 0x6e8;         // level object -> the main map

uint8_t* randomizer_state() {
    return globals() + globals_offset::randomizer_state;
}

std::span<const ItemRule> rules() {
    return field<std::vector<ItemRule>>(randomizer_state(), kStateItemRules);
}

bool has_rules(const std::string& item) {
    return std::ranges::any_of(rules(), [&](const ItemRule& rule) { return rule.item == item; });
}

bool queued_in_game(int32_t target) {
    auto& pending = field<std::vector<PendingAction>>(randomizer_state(), kStatePendingActions);
    return std::ranges::any_of(pending, [&](const PendingAction& action) { return action.target == target; });
}

bool still_queued(const std::string& item) {
    return std::ranges::any_of(rules(), [&](const ItemRule& rule) { return rule.item == item && queued_in_game(rule.target); });
}

std::string marker(const std::string& item) {
    return std::string(kIdentifierPrefix) + "rules:" + item;
}

// Only in a save the patcher made, so another randomizer game's save is never written to.
bool our_save(const std::vector<std::string>& quests) {
    return std::ranges::any_of(quests, [](const std::string& quest) { return quest.starts_with(kIdentifierPrefix); });
}

bool rules_ran(const std::vector<std::string>& quests, const std::string& item) {
    return std::ranges::find(quests, marker(item)) != quests.end();
}

// item_rule_action finds the target's level on the main map, reached through whichever level is being played.
bool can_run() {
    uint8_t* level = in_game() ? field<uint8_t*>(globals(), kGlobalsLevelObject) : nullptr;
    return level && field<void*>(level, kLevelMainMap);
}

void run(std::vector<std::string>& quests, const std::string& item) {
    for (const ItemRule& rule : rules()) {
        if (rule.item == item) {
            item_rule_action(randomizer_state(), rule.target, rule.action.c_str());
        }
    }
    if (still_queued(item)) {
        log::info("{}: rules queued until their level loads", item);
    } else {
        log::info("{}: rules applied", item);
        quests.push_back(marker(item));
    }
}

}  // namespace

void note_item_rules_ran(const std::string& item) {
    std::vector<std::string>* quests = quests_completed();
    // A queued action is left to check_item_rules, which runs the rules again once the queue has applied it.
    if (globals() && quests && our_save(*quests) && has_rules(item) && !rules_ran(*quests, item) && !still_queued(item)) {
        quests->push_back(marker(item));
    }
}

void check_item_rules(bool running) {
    std::vector<std::string>* quests = running && globals() ? quests_completed() : nullptr;
    if (!quests || !our_save(*quests) || !can_run()) {
        return;
    }
    for (const ItemRule& rule : rules()) {
        const std::string& item = rule.item;
        if (item_count(item) > 0 && !rules_ran(*quests, item) && !still_queued(item)) {
            run(*quests, item);
        }
    }
}

}  // namespace oyr::game
