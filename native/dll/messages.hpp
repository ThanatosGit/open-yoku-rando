#pragma once

#include <string>
#include <vector>

// Short messages drawn on top of the game by the overlay, newest last; see overlay.hpp.
namespace oyr::messages {

void push(std::string text);

// True for a short gap after a message; remote items wait for it, so they do not arrive all at once.
bool busy();

// Called from the tick hook on the game thread; a message's time only runs while the game does.
void on_tick(bool running);

struct Shown {
    std::string text;
    float alpha;  // 0..1, fading out at the end
};
// What to draw now; safe to call from the render thread.
std::vector<Shown> shown();

}  // namespace oyr::messages
