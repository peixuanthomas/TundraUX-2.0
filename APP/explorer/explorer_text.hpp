#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace tundraux::explorer {
namespace fs = std::filesystem;

std::string toLowerCopy(std::string value);
std::string pathToDisplayString(const fs::path& path);
std::string extensionOf(const fs::path& path);
bool opensWithEditor(const fs::path& path);
bool isDebugUser(const std::string& usertype);
std::string trimCopy(std::string value);
bool isValidFolderName(const std::string& name);
fs::path normalizedPath(const fs::path& path);
bool isPathInsideRoot(const fs::path& candidate, const fs::path& root);
bool isSamePath(const fs::path& left, const fs::path& right);
std::string formatSize(std::uintmax_t size);
std::string trimToWidth(const std::string& value, std::size_t width);
std::string yesNo(bool value);
std::string hexValue(unsigned long value);

}
