#include "messages.hpp"

#include "log.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

namespace oyr::messages {
namespace {

// In frames of the running game (60 a second), so a pause or a level load stops the clock.
constexpr uint32_t kShownFrames = 300;
constexpr uint32_t kFadeFrames = 30;
constexpr uint32_t kItemGapFrames = 120;
constexpr size_t kMaxShown = 5;

struct Message {
    std::string text;
    uint32_t frames_left;
};

// Written on the game thread, read on the render thread.
std::mutex g_mutex;
std::deque<Message> g_messages;

// Only touched on the game thread.
uint32_t g_frames_since_push = kItemGapFrames;

}  // namespace

void push(std::string text) {
    if (text.empty()) {
        return;
    }
    log::info("message: {}", text);
    std::lock_guard lock(g_mutex);
    g_messages.push_back({std::move(text), kShownFrames});
    if (g_messages.size() > kMaxShown) {
        g_messages.pop_front();
    }
    g_frames_since_push = 0;
}

bool busy() {
    return g_frames_since_push < kItemGapFrames;
}

void on_tick(bool running) {
    if (!running) {
        return;
    }
    g_frames_since_push = std::min(g_frames_since_push + 1, kItemGapFrames);
    std::lock_guard lock(g_mutex);
    for (Message& message : g_messages) {
        --message.frames_left;
    }
    std::erase_if(g_messages, [](const Message& message) { return message.frames_left == 0; });
}

std::vector<Shown> shown() {
    std::lock_guard lock(g_mutex);
    std::vector<Shown> result;
    result.reserve(g_messages.size());
    for (const Message& message : g_messages) {
        result.push_back({message.text, std::min(1.0f, static_cast<float>(message.frames_left) / kFadeFrames)});
    }
    return result;
}

}  // namespace oyr::messages
