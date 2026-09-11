#pragma once

#include <string>

namespace oyr::hooks {

// Installs every hook and patch: tick, pickup dialog, randomizer loader, slot menu, item file redirect.
// Call after game::verify(). Empty on success.
std::string install();

}  // namespace oyr::hooks
