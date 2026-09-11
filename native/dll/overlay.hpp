#pragma once

#include <string>

// Draws messages::shown() on top of the game with Dear ImGui, from a hook on IDXGISwapChain::Present.
namespace oyr::overlay {

// Patches Present and ResizeBuffers in the swap chain vtable, which every DXGI swap chain in the process shares.
// Returns an error message, empty on success. Without it the game runs as before, only without messages.
std::string install();

}  // namespace oyr::overlay
