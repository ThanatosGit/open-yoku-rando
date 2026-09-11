#include "game.hpp"
#include "log.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace oyr::game {
namespace {

// The pairs the game itself treats as progressive, in the spawn code and in mark_collected (Epic 0x16b040).
constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kStages{{
    {"abilities/slug_vaccum", "abilities/slug_upgrade"},
    {"abilities/dive", "abilities/dive_speed"},
    {"powerups/skvader_1", "powerups/skvader_2"},
}};

}  // namespace

std::string upgrade_pickup_stage(void* pickup) {
    auto& item = field<std::string>(pickup, pickup_offset::item);
    for (const auto& [base, upgrade] : kStages) {
        if (item == base && item_count(item) > 0) {
            log::info("{} pickup at location {} is already held: giving {}", item, pickup_location(pickup), upgrade);
            item = upgrade;
            return std::string(base);
        }
    }
    return {};
}

void drop_replaced_stage(const std::string& base) {
    // game's "mark_collected" ran before the upgrade was set, so the game left the base stage in place.
    if (!base.empty() && item_count(base) > 0) {
        inventory_remove(base);
    }
}

}  // namespace oyr::game
