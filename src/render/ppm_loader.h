#pragma once

#include <filesystem>

#include "render/render_types.h"

namespace asciiomium::render {

// Loads an ASCII PPM (P3) fixture into an RGBA8 ImageBuffer. P3 is used here
// deliberately: fixtures remain tiny, human-readable, diffable text files and
// the renderer does not gain a heavyweight image dependency just to prove its
// deterministic offline core.
[[nodiscard]] ImageBuffer LoadPpmP3(const std::filesystem::path& path);

}  // namespace asciiomium::render
