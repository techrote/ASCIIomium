#pragma once
#include <filesystem>
#include <string_view>

namespace asciiomium {
[[nodiscard]] std::filesystem::path ExecutableDirectory();
[[nodiscard]] std::filesystem::path BundledFixture(std::string_view filename);
[[nodiscard]] std::filesystem::path DevelopmentSourceDirectory();
}  // namespace asciiomium
