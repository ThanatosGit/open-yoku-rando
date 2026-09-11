#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace oyr::remote {

// Randovania's socket protocol.
inline constexpr uint16_t kDefaultPort = 6970;

struct LuaRequest {
    uint64_t connection;
    std::string code;
};

// Packets pushed by the game: a type byte, a little-endian length and the payload. 1-4 are in remote_api.cpp.
enum class Signal : uint8_t {
    kInventory = 5,
    kCollectedLocations = 6,
    kReceivedPickups = 7,
    kGameState = 8,
};

// Starts the TCP server on 127.0.0.1 in a background thread.
void start(uint16_t port);

// Pushes one game event to the client, if a client is connected and asked for multiworld. Safe from any thread.
void send_signal(Signal signal, std::string_view payload);

// True while a client is connected with the multiworld interest set; false again after it disconnects.
bool multiworld_client_connected();

// Bumped every time a client connects, so the game thread can notice that it has to send its state afresh.
uint64_t connection_generation();

// For the game thread.
bool has_pending_lua();
std::optional<LuaRequest> pop_lua();
void reply_lua(uint64_t connection, bool success, std::string_view data);

}  // namespace oyr::remote
