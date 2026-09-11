#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// Packets 5 (inventory), 6 (collected locations), 7 (received pickups) and 8 (game state), built from the game's
// own state; see docs/multiworld-state.md.
namespace oyr::multiworld {

// Randovania sends these once, during the handshake, before anything is polled.

// The location ids in Randovania's PickupIndex order, comma separated decimals. Bit n of packet 6 is the n-th id.
// Returns how many ids were accepted, or 0 if the list could not be parsed.
size_t set_locations(std::string_view ids);
// The item ids in the order of Randovania's resource database, comma separated. Packet 5 reports their counts.
size_t set_inventory_items(std::string_view items);
// The patcher's configuration identifier. While set, a save without it is neither reported nor written to.
// Empty accepts any save.
void set_identifier(std::string_view identifier);

// True once set_locations succeeded, which is what gates every packet.
bool armed();

// A counter that goes up whenever the item counts packet 5 reports change; packet 5 carries it as its index.
void update_inventory_index();
size_t inventory_index();

// Sends packets 5, 6 and 7 again on the next poll, even if unchanged.
void resend_save_state();

// Whether the loaded save carries the identifier marker; true while no identifier is set.
bool save_matches();
// The layout UUID the loaded save was started with, or empty if it has none.
std::string layout_uuid();

// How many remote pickups this save has already been given. Read from the save, so a reload cannot lose it.
size_t received_count();
// Records that one more remote pickup was given. Writes the count into the save's `quests_completed`.
void note_pickup_received();

// Called from the tick hook on the game thread: sends whatever changed since the last tick.
void on_tick();

}  // namespace oyr::multiworld
