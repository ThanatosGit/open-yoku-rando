#include "remote_api.hpp"

#include "log.hpp"

#include <winsock2.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <deque>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace oyr::remote {
namespace {

enum PacketType : uint8_t {
    kHandshake = 1,
    kLogMessage = 2,
    kRemoteLuaExec = 3,
    kKeepAlive = 4,
};

enum Interest : uint8_t {
    kLogging = 1,
    kMultiworld = 2,
};

constexpr uint32_t kMaxLuaSize = 1 << 20;

// Guards the client socket and everything sent on it.
std::mutex g_send_mutex;
SOCKET g_client = INVALID_SOCKET;
uint8_t g_request_number = 0;
uint8_t g_interests = 0;
uint64_t g_connection = 0;

std::mutex g_queue_mutex;
std::deque<LuaRequest> g_queue;
std::atomic<bool> g_pending = false;

bool send_all(SOCKET socket, const uint8_t* data, size_t size) {
    while (size > 0) {
        int sent = send(socket, reinterpret_cast<const char*>(data), static_cast<int>(std::min<size_t>(size, INT_MAX)), 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        size -= sent;
    }
    return true;
}

bool recv_all(SOCKET socket, uint8_t* data, size_t size) {
    while (size > 0) {
        int received = recv(socket, reinterpret_cast<char*>(data), static_cast<int>(std::min<size_t>(size, INT_MAX)), 0);
        if (received <= 0) {
            return false;
        }
        data += received;
        size -= received;
    }
    return true;
}

void append_u32(std::vector<uint8_t>& packet, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        packet.push_back(static_cast<uint8_t>(value >> shift));
    }
}

// Replies carry the rolling request number right after the packet type.
void send_numbered(uint64_t connection, PacketType type, std::span<const uint8_t> payload) {
    std::lock_guard lock(g_send_mutex);
    if (g_client == INVALID_SOCKET || connection != g_connection) {
        return;
    }
    std::vector<uint8_t> packet{type, g_request_number++};
    packet.insert(packet.end(), payload.begin(), payload.end());
    send_all(g_client, packet.data(), packet.size());
}

// Packets 2 and 5-8: a type byte, a little-endian length and the payload, no request number.
void send_length_prefixed(uint8_t type, uint8_t interest, std::string_view payload) {
    std::lock_guard lock(g_send_mutex);
    if (g_client == INVALID_SOCKET || !(g_interests & interest)) {
        return;
    }
    std::vector<uint8_t> packet{type};
    append_u32(packet, static_cast<uint32_t>(payload.size()));
    packet.insert(packet.end(), payload.begin(), payload.end());
    send_all(g_client, packet.data(), packet.size());
}

void send_log(std::string_view message) {
    send_length_prefixed(kLogMessage, kLogging, message);
}

void serve_client(SOCKET client) {
    uint64_t connection;
    {
        std::lock_guard lock(g_send_mutex);
        g_client = client;
        g_request_number = 0;
        g_interests = 0;
        connection = ++g_connection;
    }
    log::info("client connected");

    for (;;) {
        uint8_t type;
        if (!recv_all(client, &type, 1)) {
            break;
        }
        if (type == kHandshake) {
            uint8_t interests;
            if (!recv_all(client, &interests, 1)) {
                break;
            }
            {
                std::lock_guard lock(g_send_mutex);
                g_interests = interests;
            }
            send_numbered(connection, kHandshake, {});
        } else if (type == kRemoteLuaExec) {
            uint8_t length_bytes[4];
            if (!recv_all(client, length_bytes, sizeof(length_bytes))) {
                break;
            }
            uint32_t length = length_bytes[0] | length_bytes[1] << 8 | length_bytes[2] << 16 | length_bytes[3] << 24;
            if (length > kMaxLuaSize) {
                log::info("Lua request of {} bytes is too large, closing", length);
                break;
            }
            std::string code(length, '\0');
            if (length > 0 && !recv_all(client, reinterpret_cast<uint8_t*>(code.data()), length)) {
                break;
            }
            std::lock_guard lock(g_queue_mutex);
            g_queue.push_back({connection, std::move(code)});
            g_pending = true;
        } else if (type == kKeepAlive) {
            continue;
        } else {
            log::info("unknown packet type {}, closing", type);
            break;
        }
    }

    {
        std::lock_guard lock(g_send_mutex);
        g_client = INVALID_SOCKET;
        g_interests = 0;
    }
    {
        std::lock_guard lock(g_queue_mutex);
        g_queue.clear();
        g_pending = false;
    }
    closesocket(client);
    log::info("client disconnected");
}

void server_thread(uint16_t port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        log::info("WSAStartup failed");
        return;
    }
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int exclusive = 1;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR || listen(listener, 1) == SOCKET_ERROR) {
        log::info("cannot listen on 127.0.0.1:{} (error {})", port, WSAGetLastError());
        closesocket(listener);
        return;
    }
    log::info("listening on 127.0.0.1:{}", port);

    for (;;) {
        SOCKET client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            Sleep(100);
            continue;
        }
        int no_delay = 1;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));
        serve_client(client);
    }
}

}  // namespace

void start(uint16_t port) {
    log::set_sink(&send_log);
    std::thread(server_thread, port).detach();
}

void send_signal(Signal signal, std::string_view payload) {
    send_length_prefixed(static_cast<uint8_t>(signal), kMultiworld, payload);
}

bool multiworld_client_connected() {
    std::lock_guard lock(g_send_mutex);
    return g_client != INVALID_SOCKET && (g_interests & kMultiworld) != 0;
}

uint64_t connection_generation() {
    std::lock_guard lock(g_send_mutex);
    return g_connection;
}

bool has_pending_lua() {
    return g_pending.load(std::memory_order_relaxed);
}

std::optional<LuaRequest> pop_lua() {
    std::lock_guard lock(g_queue_mutex);
    if (g_queue.empty()) {
        g_pending = false;
        return std::nullopt;
    }
    LuaRequest request = std::move(g_queue.front());
    g_queue.pop_front();
    g_pending = !g_queue.empty();
    return request;
}

void reply_lua(uint64_t connection, bool success, std::string_view data) {
    std::vector<uint8_t> payload{static_cast<uint8_t>(success)};
    append_u32(payload, static_cast<uint32_t>(data.size()));
    payload.insert(payload.end(), data.begin(), data.end());
    send_numbered(connection, kRemoteLuaExec, payload);
}

}  // namespace oyr::remote
