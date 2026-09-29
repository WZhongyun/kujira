#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace FileUtil
{
// UTF-8 string <-> path, safe for Chinese file names on Windows.
fs::path FromUtf8(const std::string& utf8);
std::string ToUtf8(const fs::path& path);

std::optional<std::string> ReadText(const fs::path& path);

// Writes to a temporary file next to `path`, then renames it over the target,
// so a crash never leaves a half-written file behind.
bool WriteTextAtomic(const fs::path& path, const std::string& text, std::string* error = nullptr);

// The running executable, and the directory that contains it.
fs::path ExecutablePath();
fs::path ExecutableDir();
}
