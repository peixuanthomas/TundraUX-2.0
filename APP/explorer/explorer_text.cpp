#include "explorer_text.hpp"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <system_error>

namespace tundraux::explorer {

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string pathToDisplayString(const fs::path& path) {
    return path.u8string();
}

std::string extensionOf(const fs::path& path) {
    return toLowerCopy(path.extension().u8string());
}

bool opensWithEditor(const fs::path& path) {
    const std::string extension = extensionOf(path);
    return extension == ".md" || extension == ".txt";
}

bool isDebugUser(const std::string& usertype) {
    return toLowerCopy(usertype) == "debug";
}

std::string trimCopy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool isValidFolderName(const std::string& name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    return name.find_first_of("<>:\"/\\|?*") == std::string::npos;
}

fs::path normalizedPath(const fs::path& path) {
    std::error_code error;
    fs::path normalized = fs::weakly_canonical(path, error);
    if (error) {
        normalized = fs::absolute(path, error);
    }
    if (error) {
        normalized = path;
    }
    return normalized.lexically_normal();
}

namespace {

std::wstring normalizedPathPart(const fs::path& path) {
    std::wstring value = path.wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        if (ch == L'/') {
            return L'\\';
        }
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

}

bool isPathInsideRoot(const fs::path& candidate, const fs::path& root) {
    const fs::path candidatePath = normalizedPath(candidate);
    const fs::path rootPath = normalizedPath(root);
    auto candidateIt = candidatePath.begin();

    for (auto rootIt = rootPath.begin(); rootIt != rootPath.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == candidatePath.end()) {
            return false;
        }
        if (normalizedPathPart(*candidateIt) != normalizedPathPart(*rootIt)) {
            return false;
        }
    }

    return true;
}

bool isSamePath(const fs::path& left, const fs::path& right) {
    return normalizedPath(left) == normalizedPath(right);
}

std::string formatSize(std::uintmax_t size) {
    constexpr const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(size);
    std::size_t unit = 0;

    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }

    std::ostringstream out;
    if (unit == 0) {
        out << size << ' ' << units[unit];
    } else {
        out << std::fixed << std::setprecision(1) << value << ' ' << units[unit];
    }
    return out.str();
}

std::string trimToWidth(const std::string& value, std::size_t width) {
    if (width == 0) {
        return "";
    }

    std::string clean;
    clean.reserve(value.size());
    for (char ch : value) {
        if (ch == '\t') {
            clean += "    ";
        } else if (static_cast<unsigned char>(ch) >= 32) {
            clean.push_back(ch);
        }
    }

    if (clean.size() > width) {
        clean.resize(width);
        clean.back() = '.';
    }
    if (clean.size() < width) {
        clean.append(width - clean.size(), ' ');
    }
    return clean;
}

std::string yesNo(bool value) {
    return value ? "yes" : "no";
}

std::string hexValue(unsigned long value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << value;
    return out.str();
}

}
