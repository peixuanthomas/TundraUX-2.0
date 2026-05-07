#include "explorer.hpp"

#include "console_screen.hpp"
#include "editor.hpp"
#include "TUXfile.hpp"

#include <algorithm>
#include <cctype>
#include <conio.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <windows.h>
#include <shellapi.h>

namespace {
namespace fs = std::filesystem;

struct FileEntry {
    std::string name;
    fs::path path;
    bool isDirectory = false;
    bool isHidden = false;
    std::uintmax_t size = 0;
};

enum class ClipboardMode {
    None,
    Copy,
    Cut
};

struct ClipboardState {
    ClipboardMode mode = ClipboardMode::None;
    fs::path path;
    std::string name;
    bool isDirectory = false;
};

struct DetailLine {
    std::string label;
    std::string value;
    bool section = false;
};

struct ExplorerState {
    fs::path rootPath;
    fs::path currentPath;
    std::vector<FileEntry> entries;
    std::vector<FileEntry> parentEntries;
    ClipboardState clipboard;
    fs::path pendingDeletePath;
    std::string pendingDeleteName;
    bool pendingDelete = false;
    std::size_t cursor = 0;
    std::size_t scroll = 0;
    bool showHidden = false;
    bool showHelp = false;
    bool showDetails = false;
    bool creatingFolder = false;
    std::vector<DetailLine> detailLines;
    std::size_t detailScroll = 0;
    std::string detailName;
    std::string newFolderName;
    std::string username;
    std::string usertype;
    std::string message = "Ready";
};

enum class Key {
    Unknown,
    Character,
    Enter,
    Escape,
    Backspace,
    Up,
    Down,
    Left,
    Right,
    Home,
    End
};

struct KeyPress {
    Key key = Key::Unknown;
    char character = '\0';
};

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

bool isPrivilegedUser(const std::string& usertype) {
    const std::string normalized = toLowerCopy(usertype);
    return normalized == "admin" || normalized == "debug";
}

bool isDebugUser(const std::string& usertype) {
    return toLowerCopy(usertype) == "debug";
}

constexpr const char* kResetStyle = "\x1b[0m";
constexpr const char* kTitleStyle = "\x1b[1;38;5;51m";
constexpr const char* kRoleStyle = "\x1b[1;38;5;213m";
constexpr const char* kUserStyle = "\x1b[1;38;5;220m";
constexpr const char* kPathStyle = "\x1b[38;5;117m";
constexpr const char* kBorderStyle = "\x1b[38;5;39m";
constexpr const char* kHeaderStyle = "\x1b[1;38;5;195;48;5;24m";
constexpr const char* kSectionStyle = "\x1b[1;38;5;87m";
constexpr const char* kKeyStyle = "\x1b[1;38;5;220m";
constexpr const char* kHintStyle = "\x1b[38;5;245m";
constexpr const char* kHelpTextStyle = "\x1b[38;5;252m";
constexpr const char* kDirStyle = "\x1b[1;38;5;87m";
constexpr const char* kFileStyle = "\x1b[38;5;254m";
constexpr const char* kTextFileStyle = "\x1b[38;5;229m";
constexpr const char* kTuxFileStyle = "\x1b[1;38;5;213m";
constexpr const char* kDataFileStyle = "\x1b[38;5;203m";
constexpr const char* kHiddenStyle = "\x1b[38;5;244m";
constexpr const char* kSizeStyle = "\x1b[38;5;250m";
constexpr const char* kCopyStyle = "\x1b[1;38;5;119m";
constexpr const char* kCutStyle = "\x1b[38;5;246m";
constexpr const char* kSelectedBgStyle = "\x1b[48;5;24m";
constexpr const char* kSelectedMarkStyle = "\x1b[1;38;5;229m";
constexpr const char* kInputStyle = "\x1b[1;38;5;230m";
constexpr const char* kWarningStyle = "\x1b[1;38;5;203m";

std::string colorText(const std::string& text, const char* style) {
    if (text.empty()) {
        return text;
    }
    return std::string(style) + text + kResetStyle;
}

std::string colorCellPart(const std::string& text, const char* style, bool selected) {
    if (text.empty()) {
        return text;
    }

    std::string prefix;
    if (selected) {
        prefix += kSelectedBgStyle;
    }
    prefix += style;
    return prefix + text + kResetStyle;
}

std::string redMessage(const std::string& message) {
    return colorText(message, kWarningStyle);
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

bool clipboardMatches(const ExplorerState& state, const FileEntry& entry) {
    return state.clipboard.mode != ClipboardMode::None &&
           isSamePath(state.clipboard.path, entry.path);
}

bool directoryContainsExtension(const fs::path& directory, const std::string& extension) {
    std::error_code error;
    for (const auto& item : fs::recursive_directory_iterator(
             directory,
             fs::directory_options::skip_permission_denied,
             error
         )) {
        if (error) {
            error.clear();
            continue;
        }
        if (!item.is_directory(error) && extensionOf(item.path()) == extension) {
            return true;
        }
        error.clear();
    }
    return false;
}

bool isHiddenPath(const fs::path& path) {
    const std::string filename = path.filename().u8string();
    if (!filename.empty() && filename.front() == '.') {
        return true;
    }

    const DWORD attributes = GetFileAttributesW(path.wstring().c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
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

std::string formatFileTime(const FILETIME& fileTime) {
    if (fileTime.dwLowDateTime == 0 && fileTime.dwHighDateTime == 0) {
        return "Unknown";
    }

    FILETIME localFileTime{};
    SYSTEMTIME systemTime{};
    if (!FileTimeToLocalFileTime(&fileTime, &localFileTime) ||
        !FileTimeToSystemTime(&localFileTime, &systemTime)) {
        return "Unknown";
    }

    std::ostringstream out;
    out << std::setfill('0')
        << std::setw(4) << systemTime.wYear << "-"
        << std::setw(2) << systemTime.wMonth << "-"
        << std::setw(2) << systemTime.wDay << " "
        << std::setw(2) << systemTime.wHour << ":"
        << std::setw(2) << systemTime.wMinute << ":"
        << std::setw(2) << systemTime.wSecond;
    return out.str();
}

void addSection(std::vector<DetailLine>& lines, const std::string& label) {
    lines.push_back({label, "", true});
}

void addDetail(std::vector<DetailLine>& lines, const std::string& label, const std::string& value) {
    lines.push_back({label, value, false});
}

std::string fileTypeDescription(const FileEntry& entry) {
    if (entry.isDirectory) {
        return "Directory";
    }

    const std::string extension = extensionOf(entry.path);
    if (extension == ".tux") {
        return "Encrypted TUX file";
    }
    if (extension == ".dat") {
        return "User data file";
    }
    if (extension == ".md") {
        return "Markdown text file";
    }
    if (extension == ".txt") {
        return "Plain text file";
    }
    if (extension.empty()) {
        return "File without extension";
    }
    return "File";
}

std::string fileStatusTypeName(fs::file_type type) {
    switch (type) {
        case fs::file_type::none: return "none";
        case fs::file_type::not_found: return "not found";
        case fs::file_type::regular: return "regular";
        case fs::file_type::directory: return "directory";
        case fs::file_type::symlink: return "symlink";
        case fs::file_type::block: return "block";
        case fs::file_type::character: return "character";
        case fs::file_type::fifo: return "fifo";
        case fs::file_type::socket: return "socket";
        case fs::file_type::unknown: return "unknown";
        default: return "implementation-defined";
    }
}

std::string permissionSet(fs::perms permissions, fs::perms read, fs::perms write, fs::perms exec) {
    std::string value;
    value += (permissions & read) != fs::perms::none ? 'r' : '-';
    value += (permissions & write) != fs::perms::none ? 'w' : '-';
    value += (permissions & exec) != fs::perms::none ? 'x' : '-';
    return value;
}

std::string formatPermissions(fs::perms permissions) {
    return "owner " + permissionSet(permissions, fs::perms::owner_read, fs::perms::owner_write, fs::perms::owner_exec) +
           ", group " + permissionSet(permissions, fs::perms::group_read, fs::perms::group_write, fs::perms::group_exec) +
           ", others " + permissionSet(permissions, fs::perms::others_read, fs::perms::others_write, fs::perms::others_exec);
}

std::string attributeList(DWORD attributes) {
    std::vector<std::string> names;
    auto add = [&](DWORD flag, const std::string& name) {
        if ((attributes & flag) != 0) {
            names.push_back(name);
        }
    };

    add(FILE_ATTRIBUTE_READONLY, "read-only");
    add(FILE_ATTRIBUTE_HIDDEN, "hidden");
    add(FILE_ATTRIBUTE_SYSTEM, "system");
    add(FILE_ATTRIBUTE_DIRECTORY, "directory");
    add(FILE_ATTRIBUTE_ARCHIVE, "archive");
    add(FILE_ATTRIBUTE_DEVICE, "device");
    add(FILE_ATTRIBUTE_NORMAL, "normal");
    add(FILE_ATTRIBUTE_TEMPORARY, "temporary");
    add(FILE_ATTRIBUTE_SPARSE_FILE, "sparse");
    add(FILE_ATTRIBUTE_REPARSE_POINT, "reparse point");
    add(FILE_ATTRIBUTE_COMPRESSED, "compressed");
    add(FILE_ATTRIBUTE_OFFLINE, "offline");
    add(FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, "not content-indexed");
    add(FILE_ATTRIBUTE_ENCRYPTED, "encrypted");

    if (names.empty()) {
        return "none";
    }

    std::ostringstream out;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << names[index];
    }
    return out.str();
}

std::string relativeToRoot(const fs::path& path, const fs::path& rootPath) {
    const fs::path relative = normalizedPath(path).lexically_relative(normalizedPath(rootPath));
    if (relative.empty()) {
        return ".";
    }
    return pathToDisplayString(relative);
}

std::string allocatedSizeString(const fs::path& path) {
    DWORD high = 0;
    const DWORD low = GetCompressedFileSizeW(path.wstring().c_str(), &high);
    if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
        return "Unavailable";
    }

    ULARGE_INTEGER size{};
    size.LowPart = low;
    size.HighPart = high;
    return formatSize(size.QuadPart) + " (" + std::to_string(size.QuadPart) + " bytes)";
}

struct DirectoryStats {
    std::uintmax_t directChildren = 0;
    std::uintmax_t recursiveFiles = 0;
    std::uintmax_t recursiveDirectories = 0;
    std::uintmax_t totalFileSize = 0;
    std::uintmax_t scanErrors = 0;
};

DirectoryStats scanDirectoryStats(const fs::path& path) {
    DirectoryStats stats;
    std::error_code error;

    for (const auto& item : fs::directory_iterator(path, fs::directory_options::skip_permission_denied, error)) {
        if (error) {
            ++stats.scanErrors;
            error.clear();
            continue;
        }
        ++stats.directChildren;
    }

    error.clear();
    fs::recursive_directory_iterator iterator(path, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    while (iterator != end) {
        if (error) {
            ++stats.scanErrors;
            error.clear();
            iterator.increment(error);
            continue;
        }

        std::error_code statusError;
        const fs::file_status status = iterator->symlink_status(statusError);
        if (statusError) {
            ++stats.scanErrors;
        } else if (fs::is_directory(status)) {
            ++stats.recursiveDirectories;
        } else {
            ++stats.recursiveFiles;
            if (fs::is_regular_file(status)) {
                std::error_code sizeError;
                stats.totalFileSize += fs::file_size(iterator->path(), sizeError);
                if (sizeError) {
                    ++stats.scanErrors;
                }
            }
        }

        iterator.increment(error);
    }

    if (error) {
        ++stats.scanErrors;
    }

    return stats;
}

const char* entryNameStyle(const FileEntry& entry) {
    if (entry.isHidden) {
        return kHiddenStyle;
    }
    if (entry.isDirectory) {
        return kDirStyle;
    }

    const std::string extension = extensionOf(entry.path);
    if (extension == ".tux") {
        return kTuxFileStyle;
    }
    if (extension == ".md" || extension == ".txt") {
        return kTextFileStyle;
    }
    if (extension == ".dat") {
        return kDataFileStyle;
    }
    return kFileStyle;
}

const char* previewLineStyle(const std::string& line) {
    const std::string normalized = toLowerCopy(line);
    if (normalized.find("cannot") != std::string::npos ||
        normalized.find("failed") != std::string::npos ||
        normalized.find("corrupted") != std::string::npos) {
        return kWarningStyle;
    }
    if (normalized.find("empty") != std::string::npos ||
        normalized.find("too large") != std::string::npos ||
        normalized.find("binary") != std::string::npos ||
        normalized.find("encrypted") != std::string::npos) {
        return kHintStyle;
    }
    if (line.rfind("[D] ", 0) == 0) {
        return kDirStyle;
    }
    return kHelpTextStyle;
}

std::string headerCell(const std::string& title, std::size_t width) {
    return colorText(trimToWidth(" " + title, width), kHeaderStyle);
}

std::string previewCell(const std::string& text, std::size_t width) {
    return colorText(trimToWidth(text, width), previewLineStyle(text));
}

std::vector<FileEntry> readDirectory(const fs::path& path, const fs::path& rootPath, bool showHidden) {
    std::vector<FileEntry> entries;
    std::error_code error;

    for (const auto& item : fs::directory_iterator(path, fs::directory_options::skip_permission_denied, error)) {
        std::error_code statusError;
        const fs::file_status status = item.symlink_status(statusError);
        if (statusError) {
            continue;
        }

        const fs::path itemPath = item.path();
        if (!isPathInsideRoot(itemPath, rootPath)) {
            continue;
        }

        const bool hidden = isHiddenPath(itemPath);
        if (!showHidden && hidden) {
            continue;
        }

        FileEntry entry;
        entry.name = itemPath.filename().u8string();
        entry.path = itemPath;
        entry.isDirectory = fs::is_directory(status);
        entry.isHidden = hidden;
        if (!entry.isDirectory) {
            entry.size = fs::file_size(itemPath, statusError);
            if (statusError) {
                entry.size = 0;
            }
        }
        entries.push_back(entry);
    }

    std::stable_sort(entries.begin(), entries.end(), [](const FileEntry& left, const FileEntry& right) {
        if (left.isDirectory != right.isDirectory) {
            return left.isDirectory;
        }
        return toLowerCopy(left.name) < toLowerCopy(right.name);
    });

    return entries;
}

void refresh(ExplorerState& state) {
    try {
        if (!isPathInsideRoot(state.currentPath, state.rootPath)) {
            state.currentPath = state.rootPath;
        }

        state.entries = readDirectory(state.currentPath, state.rootPath, state.showHidden);
        state.parentEntries.clear();
        if (!isSamePath(state.currentPath, state.rootPath) && state.currentPath.has_parent_path()) {
            const fs::path parentPath = state.currentPath.parent_path();
            if (isPathInsideRoot(parentPath, state.rootPath)) {
                state.parentEntries = readDirectory(parentPath, state.rootPath, state.showHidden);
            }
        }
        if (state.entries.empty()) {
            state.cursor = 0;
            state.scroll = 0;
        } else {
            state.cursor = std::min(state.cursor, state.entries.size() - 1);
            state.scroll = std::min(state.scroll, state.cursor);
        }
        state.message = std::to_string(state.entries.size()) + " item(s)";
    } catch (const std::exception& ex) {
        state.entries.clear();
        state.message = std::string("Read failed: ") + ex.what();
    }
}

std::vector<std::string> previewDirectory(const fs::path& path, const fs::path& rootPath) {
    std::vector<std::string> lines;
    std::error_code error;
    const auto entries = readDirectory(path, rootPath, false);
    if (entries.empty()) {
        lines.push_back("Empty directory");
        return lines;
    }

    for (const auto& entry : entries) {
        lines.push_back(std::string(entry.isDirectory ? "[D] " : "    ") + entry.name);
        if (lines.size() >= 80) {
            break;
        }
    }
    if (error) {
        lines.push_back("Cannot preview directory");
    }
    return lines;
}

std::vector<std::string> previewFile(const fs::path& path) {
    if (extensionOf(path) == ".tux") {
        return {"Encrypted TUX file.", "Press Enter to decrypt and edit."};
    }
    if (extensionOf(path) == ".dat") {
        return {"User data file."};
    }

    std::vector<std::string> lines;
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (!error && size > 96 * 1024) {
        lines.push_back("File is too large to preview.");
        lines.push_back("Size: " + formatSize(size));
        return lines;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        lines.push_back("Cannot read file.");
        return lines;
    }

    std::string content(4096, '\0');
    file.read(content.data(), static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<std::size_t>(file.gcount()));
    if (content.find('\0') != std::string::npos) {
        lines.push_back("Binary file preview is not supported.");
        return lines;
    }

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line) && lines.size() < 80) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    if (lines.empty()) {
        lines.push_back("Empty file");
    }
    return lines;
}

std::vector<std::string> previewSelected(const ExplorerState& state) {
    if (state.entries.empty() || state.cursor >= state.entries.size()) {
        return {"No file selected"};
    }

    const FileEntry& entry = state.entries[state.cursor];
    try {
        return entry.isDirectory ? previewDirectory(entry.path, state.rootPath) : previewFile(entry.path);
    } catch (const std::exception& ex) {
        return {std::string("Preview failed: ") + ex.what()};
    }
}

std::string deletePermissionError(const ExplorerState& state, const FileEntry& entry);

std::string selectedTuxPermissionStatus(const ExplorerState& state) {
    if (state.entries.empty() || state.cursor >= state.entries.size()) {
        return "";
    }

    const FileEntry& entry = state.entries[state.cursor];
    if (entry.isDirectory || extensionOf(entry.path) != ".tux") {
        return "";
    }

    if (can_modify_tux_file(pathToDisplayString(entry.path), state.username, state.usertype)) {
        return "";
    }

    return redMessage("Access denied: current user cannot modify this TUX file.");
}

std::vector<DetailLine> buildDetailLines(const ExplorerState& state) {
    std::vector<DetailLine> lines;
    if (state.entries.empty() || state.cursor >= state.entries.size()) {
        addDetail(lines, "Selection", "No file selected");
        return lines;
    }

    const FileEntry& entry = state.entries[state.cursor];
    const std::string extension = extensionOf(entry.path);
    const std::string fullPath = pathToDisplayString(normalizedPath(entry.path));
    const std::string parentPath = pathToDisplayString(entry.path.parent_path());
    const std::string stem = entry.path.stem().u8string();

    std::error_code statusError;
    const fs::file_status status = fs::symlink_status(entry.path, statusError);

    WIN32_FILE_ATTRIBUTE_DATA attributeData{};
    const bool hasAttributes = GetFileAttributesExW(
        entry.path.wstring().c_str(),
        GetFileExInfoStandard,
        &attributeData
    ) != 0;

    addSection(lines, "Basic");
    addDetail(lines, "Name", entry.name);
    addDetail(lines, "Type", fileTypeDescription(entry));
    addDetail(lines, "Filesystem type", statusError ? "Unavailable: " + statusError.message() : fileStatusTypeName(status.type()));
    addDetail(lines, "Directory", yesNo(entry.isDirectory));
    addDetail(lines, "Hidden", yesNo(entry.isHidden));
    addDetail(lines, "Extension", extension.empty() ? "(none)" : extension);
    addDetail(lines, "Stem", stem.empty() ? "(none)" : stem);
    addDetail(lines, "Name length", std::to_string(entry.name.size()) + " characters");
    addDetail(lines, "Path length", std::to_string(fullPath.size()) + " characters");

    if (!entry.isDirectory) {
        addDetail(lines, "Size", formatSize(entry.size) + " (" + std::to_string(entry.size) + " bytes)");
        addDetail(lines, "Allocated size", allocatedSizeString(entry.path));
        addDetail(lines, "Opens in editor", yesNo(opensWithEditor(entry.path) || extension == ".tux"));
    }

    addSection(lines, "Paths");
    addDetail(lines, "Full path", fullPath);
    addDetail(lines, "Parent", parentPath.empty() ? "(none)" : parentPath);
    addDetail(lines, "Explorer root", pathToDisplayString(state.rootPath));
    addDetail(lines, "Relative to root", relativeToRoot(entry.path, state.rootPath));

    addSection(lines, "Timestamps");
    if (hasAttributes) {
        addDetail(lines, "Created", formatFileTime(attributeData.ftCreationTime));
        addDetail(lines, "Modified", formatFileTime(attributeData.ftLastWriteTime));
        addDetail(lines, "Accessed", formatFileTime(attributeData.ftLastAccessTime));
    } else {
        addDetail(lines, "Created", "Unavailable");
        addDetail(lines, "Modified", "Unavailable");
        addDetail(lines, "Accessed", "Unavailable");
    }

    addSection(lines, "Windows attributes");
    if (hasAttributes) {
        addDetail(lines, "Raw value", hexValue(attributeData.dwFileAttributes));
        addDetail(lines, "Flags", attributeList(attributeData.dwFileAttributes));
        addDetail(lines, "Read-only", yesNo((attributeData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0));
        addDetail(lines, "System", yesNo((attributeData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0));
        addDetail(lines, "Archive", yesNo((attributeData.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) != 0));
        addDetail(lines, "Compressed", yesNo((attributeData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) != 0));
        addDetail(lines, "Encrypted", yesNo((attributeData.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) != 0));
        addDetail(lines, "Reparse point", yesNo((attributeData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0));
    } else {
        addDetail(lines, "Attributes", "Unavailable");
    }

    addSection(lines, "Permissions");
    addDetail(lines, "Current user", state.username.empty() ? "(none)" : state.username);
    addDetail(lines, "User type", state.usertype.empty() ? "(none)" : state.usertype);
    if (!statusError) {
        addDetail(lines, "Filesystem perms", formatPermissions(status.permissions()));
    }
    const std::string permissionError = deletePermissionError(state, entry);
    addDetail(
        lines,
        "Explorer modify",
        permissionError.empty() ? "yes" : "no - " + permissionError
    );
    if (extension == ".tux") {
        const bool canModify = can_modify_tux_file(pathToDisplayString(entry.path), state.username, state.usertype);
        addDetail(lines, "TUX edit allowed", yesNo(canModify));
        addDetail(lines, "TUX open mode", canModify ? "edit" : "read-only");
    }

    addSection(lines, "Filesystem identity");
    const DWORD flags = entry.isDirectory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL;
    HANDLE handle = CreateFileW(
        entry.path.wstring().c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        flags,
        nullptr
    );
    if (handle != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info{};
        if (GetFileInformationByHandle(handle, &info)) {
            ULARGE_INTEGER fileIndex{};
            fileIndex.HighPart = info.nFileIndexHigh;
            fileIndex.LowPart = info.nFileIndexLow;
            ULARGE_INTEGER fileSize{};
            fileSize.HighPart = info.nFileSizeHigh;
            fileSize.LowPart = info.nFileSizeLow;
            addDetail(lines, "Volume serial", hexValue(info.dwVolumeSerialNumber));
            addDetail(lines, "File index", std::to_string(fileIndex.QuadPart));
            addDetail(lines, "Hard links", std::to_string(info.nNumberOfLinks));
            if (!entry.isDirectory) {
                addDetail(lines, "Handle size", formatSize(fileSize.QuadPart) + " (" + std::to_string(fileSize.QuadPart) + " bytes)");
            }
        } else {
            addDetail(lines, "Handle info", "Unavailable");
        }
        CloseHandle(handle);
    } else {
        addDetail(lines, "Handle info", "Unavailable");
    }

    if (entry.isDirectory) {
        addSection(lines, "Directory contents");
        const DirectoryStats stats = scanDirectoryStats(entry.path);
        addDetail(lines, "Direct children", std::to_string(stats.directChildren));
        addDetail(lines, "Recursive files", std::to_string(stats.recursiveFiles));
        addDetail(lines, "Recursive folders", std::to_string(stats.recursiveDirectories));
        addDetail(lines, "Recursive file size", formatSize(stats.totalFileSize) + " (" + std::to_string(stats.totalFileSize) + " bytes)");
        addDetail(lines, "Scan errors", std::to_string(stats.scanErrors));
    }

    return lines;
}

COORD consoleSize() {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(output, &info)) {
        return {
            static_cast<SHORT>(info.srWindow.Right - info.srWindow.Left + 1),
            static_cast<SHORT>(info.srWindow.Bottom - info.srWindow.Top + 1)
        };
    }
    return {120, 30};
}

std::string border(std::size_t parentWidth, std::size_t currentWidth, std::size_t previewWidth) {
    return "+" + std::string(parentWidth, '-') +
           "+" + std::string(currentWidth, '-') +
           "+" + std::string(previewWidth, '-') + "+";
}

std::size_t findCurrentInParent(const ExplorerState& state) {
    for (std::size_t index = 0; index < state.parentEntries.size(); ++index) {
        if (isSamePath(state.parentEntries[index].path, state.currentPath)) {
            return index;
        }
    }
    return state.parentEntries.size();
}

std::size_t parentScrollForCurrent(std::size_t currentIndex, std::size_t totalEntries, std::size_t rows) {
    if (rows == 0 || currentIndex >= totalEntries || currentIndex < rows) {
        return 0;
    }
    return currentIndex - rows + 1;
}

std::string formatParentEntry(const FileEntry& entry, bool current) {
    return std::string(current ? "-> " : "   ") +
           std::string(entry.isDirectory ? "[D] " : "    ") +
           entry.name;
}

std::string fitText(const std::string& value, std::size_t width) {
    if (value.size() <= width) {
        return value;
    }
    if (width == 0) {
        return "";
    }

    std::string fitted = value.substr(0, width);
    fitted.back() = '.';
    return fitted;
}

std::string formatParentCell(const FileEntry* entry, bool current, std::size_t width) {
    if (entry == nullptr) {
        return std::string(width, ' ');
    }

    const std::string marker = current ? "-> " : "   ";
    const std::string badge = entry->isDirectory ? "[D] " : "    ";
    const std::size_t fixedWidth = marker.size() + badge.size();
    if (fixedWidth >= width) {
        return colorCellPart(trimToWidth(formatParentEntry(*entry, current), width), entryNameStyle(*entry), current);
    }

    const std::string name = fitText(entry->name, width - fixedWidth);
    const std::size_t visibleWidth = fixedWidth + name.size();
    const std::string padding = visibleWidth < width ? std::string(width - visibleWidth, ' ') : "";

    return colorCellPart(marker, current ? kSelectedMarkStyle : kHintStyle, current) +
           colorCellPart(badge, entry->isDirectory ? kDirStyle : kHintStyle, current) +
           colorCellPart(name, entryNameStyle(*entry), current) +
           colorCellPart(padding, kFileStyle, current);
}

std::string formatCurrentEntry(const FileEntry& entry, bool selected) {
    std::ostringstream out;
    out << (selected ? "> " : "  ")
        << (entry.isDirectory ? "[D]" : "   ")
        << (entry.isHidden ? "." : " ")
        << ' ' << entry.name << "  "
        << (entry.isDirectory ? "<DIR>" : formatSize(entry.size));
    return out.str();
}

std::string formatCurrentCell(
    const ExplorerState& state,
    const FileEntry* entry,
    bool selected,
    std::size_t width
) {
    if (entry == nullptr) {
        return std::string(width, ' ');
    }

    const std::string marker = selected ? "> " : "  ";
    const std::string badge = entry->isDirectory ? "[D]" : "   ";
    const std::string hiddenMarker = entry->isHidden ? "." : " ";
    const std::string gap = " ";
    const std::string suffix = "  " + std::string(entry->isDirectory ? "<DIR>" : formatSize(entry->size));
    const std::string prefix = marker + badge + hiddenMarker + gap;
    const std::size_t fixedWidth = prefix.size() + suffix.size();

    if (fixedWidth >= width) {
        return colorCellPart(trimToWidth(formatCurrentEntry(*entry, selected), width), entryNameStyle(*entry), selected);
    }

    const std::size_t nameWidth = width - fixedWidth;
    const std::string name = fitText(entry->name, nameWidth);
    const char* nameStyle = entryNameStyle(*entry);
    if (clipboardMatches(state, *entry)) {
        nameStyle = state.clipboard.mode == ClipboardMode::Copy ? kCopyStyle : kCutStyle;
    }

    std::string cell =
        colorCellPart(marker, selected ? kSelectedMarkStyle : kHintStyle, selected) +
        colorCellPart(badge, entry->isDirectory ? kDirStyle : kHintStyle, selected) +
        colorCellPart(hiddenMarker, entry->isHidden ? kHiddenStyle : kHintStyle, selected) +
        colorCellPart(gap, kFileStyle, selected) +
        colorCellPart(name, nameStyle, selected) +
        colorCellPart(suffix, entry->isDirectory ? kDirStyle : kSizeStyle, selected);

    const std::size_t visibleWidth = prefix.size() + name.size() + suffix.size();
    if (visibleWidth < width) {
        cell += colorCellPart(std::string(width - visibleWidth, ' '), kFileStyle, selected);
    }
    return cell;
}

void keepCursorVisible(ExplorerState& state, std::size_t rows) {
    if (rows == 0) {
        state.scroll = 0;
        return;
    }
    if (state.cursor < state.scroll) {
        state.scroll = state.cursor;
    } else if (state.cursor >= state.scroll + rows) {
        state.scroll = state.cursor - rows + 1;
    }
}

void renderHelpSection(const std::string& title) {
    std::cout << colorText(title, kSectionStyle) << "\n";
}

void renderHelpBinding(const std::string& keys, const std::string& description) {
    std::cout << "  "
              << colorText(trimToWidth(keys, 22), kKeyStyle)
              << colorText(description, kHelpTextStyle)
              << "\n";
}

void renderHelp(const ExplorerState& state, const std::string& username, const std::string& usertype) {
    std::cout << "\x1b[0m\x1b[2J\x1b[H\x1b[?25l";
    std::cout << colorText("TundraUX Explorer Help", kTitleStyle)
              << colorText(" - ", kHintStyle)
              << colorText(usertype, kRoleStyle)
              << colorText(": ", kHintStyle)
              << colorText(username, kUserStyle)
              << "\n";
    std::cout << colorText(pathToDisplayString(state.currentPath), kPathStyle) << "\n\n";
    renderHelpSection("Navigation");
    renderHelpBinding("Up/Down or j/k", "Move cursor");
    renderHelpBinding("Enter or o", "Open file or enter directory");
    renderHelpBinding("Left, Backspace, b", "Go to parent directory");
    renderHelpBinding("g / G", "Jump to top / bottom");
    std::cout << "  "
              << colorText("Pro tip", kTuxFileStyle)
              << colorText(": Arrow keys cover navigation: Up/Down select, Right open, Left parent.", kHelpTextStyle)
              << "\n";
    std::cout << "\n";

    renderHelpSection("File operations");
    renderHelpBinding("c", "Copy selected file or folder, shown in green until paste");
    renderHelpBinding("x", "Cut selected file or folder, shown in gray until paste");
    renderHelpBinding("p", "Paste into current directory");
    std::cout << "\n";
    renderHelpBinding("n", "Create a new folder in the current directory");
    renderHelpBinding("d", "Request delete for selected item");
    renderHelpBinding("D", "Confirm pending delete");
    std::cout << "\n";

    renderHelpSection("View");
    renderHelpBinding(".", "Show or hide hidden files");
    renderHelpBinding("r", "Refresh current directory");
    renderHelpBinding("i", "Show detailed properties for the selected item");
    renderHelpBinding("h", "Toggle this help menu");
    renderHelpBinding("q or Esc", "Quit explorer from main view, close help from here");
    std::cout << "\n";
    std::cout << colorText("Press h, q, Esc, or Enter to return.", kHintStyle) << std::flush;
}

std::string singleBorder(std::size_t width) {
    if (width < 2) {
        return "";
    }
    return "+" + std::string(width - 2, '-') + "+";
}

std::size_t detailVisibleRows(std::size_t height) {
    return height > 7 ? height - 7 : 8;
}

const char* detailValueStyle(const DetailLine& line) {
    const std::string normalized = toLowerCopy(line.value);
    if (normalized.rfind("no -", 0) == 0 ||
        normalized.find("unavailable") != std::string::npos ||
        normalized.find("error") != std::string::npos) {
        return kWarningStyle;
    }
    if (normalized == "yes" || normalized == "edit") {
        return kCopyStyle;
    }
    if (normalized == "read-only" || normalized == "none" || normalized == "(none)") {
        return kHintStyle;
    }
    return kHelpTextStyle;
}

std::string detailLineText(const DetailLine& line, std::size_t width) {
    if (line.section) {
        return colorText(trimToWidth(" " + line.label, width), kHeaderStyle);
    }

    const std::size_t labelWidth = std::min<std::size_t>(24, std::max<std::size_t>(12, width / 3));
    const std::size_t valueWidth = width > labelWidth + 1 ? width - labelWidth - 1 : 0;
    return colorText(trimToWidth(line.label + ":", labelWidth), kKeyStyle) +
           " " +
           colorText(trimToWidth(line.value, valueWidth), detailValueStyle(line));
}

void renderDetails(const ExplorerState& state, const std::string& username, const std::string& usertype) {
    const COORD size = consoleSize();
    const std::size_t width = std::max<int>(size.X, 90);
    const std::size_t height = std::max<int>(size.Y, 18);
    const std::size_t contentWidth = width > 2 ? width - 2 : width;
    const std::size_t rows = detailVisibleRows(height);
    const std::size_t totalLines = state.detailLines.size();
    const std::size_t maxScroll = totalLines > rows ? totalLines - rows : 0;
    const std::size_t scroll = std::min(state.detailScroll, maxScroll);

    std::cout << "\x1b[0m\x1b[2J\x1b[H\x1b[?25l";
    std::cout << colorText("TundraUX Explorer Properties", kTitleStyle)
              << colorText(" - ", kHintStyle)
              << colorText(usertype, kRoleStyle)
              << colorText(": ", kHintStyle)
              << colorText(username, kUserStyle)
              << "\n";
    std::cout << colorText(state.detailName.empty() ? "(no selection)" : state.detailName, kPathStyle) << "\n";
    std::cout << colorText(singleBorder(width), kBorderStyle) << "\n";

    for (std::size_t rowIndex = 0; rowIndex < rows; ++rowIndex) {
        const std::size_t detailIndex = scroll + rowIndex;
        const std::string content = detailIndex < totalLines
            ? detailLineText(state.detailLines[detailIndex], contentWidth)
            : std::string(contentWidth, ' ');
        std::cout << colorText("|", kBorderStyle)
                  << content
                  << colorText("|", kBorderStyle)
                  << "\n";
    }

    std::cout << colorText(singleBorder(width), kBorderStyle) << "\n";
    const std::string lineStatus = totalLines == 0
        ? "No detail lines"
        : "Lines " + std::to_string(scroll + 1) + "-" +
          std::to_string(std::min(totalLines, scroll + rows)) +
          " of " + std::to_string(totalLines);
    std::cout << colorText("Up/Down", kKeyStyle)
              << colorText(" scroll | ", kHintStyle)
              << colorText("i/Esc/Enter/q", kKeyStyle)
              << colorText(" return | ", kHintStyle)
              << colorText(lineStatus, kSectionStyle)
              << std::flush;
}

void render(const ExplorerState& state, const std::string& username, const std::string& usertype) {
    if (state.showHelp) {
        renderHelp(state, username, usertype);
        return;
    }
    if (state.showDetails) {
        renderDetails(state, username, usertype);
        return;
    }

    const COORD size = consoleSize();
    const std::size_t width = std::max<int>(size.X, 90);
    const std::size_t height = std::max<int>(size.Y, 18);
    const std::size_t rows = height > 8 ? height - 8 : 10;
    const std::size_t usableWidth = width - 4;
    const std::size_t parentWidth = std::max<std::size_t>(18, usableWidth * 24 / 100);
    const std::size_t currentWidth = std::max<std::size_t>(30, usableWidth * 42 / 100);
    const std::size_t previewWidth = usableWidth - parentWidth - currentWidth;
    const auto previewLines = previewSelected(state);
    const std::size_t parentCurrentIndex = findCurrentInParent(state);
    const std::size_t parentScroll = parentScrollForCurrent(
        parentCurrentIndex,
        state.parentEntries.size(),
        rows
    );

    std::cout << "\x1b[0m\x1b[2J\x1b[H\x1b[?25l";
    std::cout << colorText("TundraUX Explorer", kTitleStyle)
              << colorText(" - ", kHintStyle)
              << colorText(usertype, kRoleStyle)
              << colorText(": ", kHintStyle)
              << colorText(username, kUserStyle)
              << "\n";
    std::cout << colorText(pathToDisplayString(state.currentPath), kPathStyle) << "\n";
    std::cout << colorText(border(parentWidth, currentWidth, previewWidth), kBorderStyle) << "\n";
    std::cout << colorText("|", kBorderStyle)
              << headerCell("Parent", parentWidth)
              << colorText("|", kBorderStyle)
              << headerCell("Current", currentWidth)
              << colorText("|", kBorderStyle)
              << headerCell("Preview", previewWidth)
              << colorText("|", kBorderStyle)
              << "\n";
    std::cout << colorText(border(parentWidth, currentWidth, previewWidth), kBorderStyle) << "\n";

    for (std::size_t rowIndex = 0; rowIndex < rows; ++rowIndex) {
        const std::size_t parentIndex = parentScroll + rowIndex;
        const FileEntry* parentEntry = parentIndex < state.parentEntries.size()
            ? &state.parentEntries[parentIndex]
            : nullptr;
        const std::string parentText = formatParentCell(
            parentEntry,
            parentIndex == parentCurrentIndex,
            parentWidth
        );
        const std::size_t entryIndex = state.scroll + rowIndex;
        const FileEntry* currentEntry = entryIndex < state.entries.size()
            ? &state.entries[entryIndex]
            : nullptr;
        const std::string currentText = formatCurrentCell(
            state,
            currentEntry,
            entryIndex == state.cursor,
            currentWidth
        );
        const std::string previewText = rowIndex < previewLines.size() ? previewLines[rowIndex] : "";
        std::cout << colorText("|", kBorderStyle)
                  << parentText
                  << colorText("|", kBorderStyle)
                  << currentText
                  << colorText("|", kBorderStyle)
                  << previewCell(previewText, previewWidth)
                  << colorText("|", kBorderStyle)
                  << "\n";
    }

    std::cout << colorText(border(parentWidth, currentWidth, previewWidth), kBorderStyle) << "\n";
    if (state.creatingFolder) {
        std::cout << colorText("Enter", kKeyStyle)
                  << colorText(" create | ", kHintStyle)
                  << colorText("Backspace", kKeyStyle)
                  << colorText(" edit | ", kHintStyle)
                  << colorText("Esc", kKeyStyle)
                  << colorText(" cancel", kHintStyle)
                  << "\n";
        std::cout << colorText("New folder name: ", kSectionStyle)
                  << colorText(state.newFolderName, kInputStyle)
                  << std::flush;
    } else {
        std::cout << colorText("Enter", kKeyStyle)
                  << colorText(" open | ", kHintStyle)
                  << colorText("Backspace", kKeyStyle)
                  << colorText(" parent | ", kHintStyle)
                  << colorText("n", kKeyStyle)
                  << colorText(" mkdir | ", kHintStyle)
                  << colorText("c", kKeyStyle)
                  << colorText(" copy | ", kHintStyle)
                  << colorText("x", kKeyStyle)
                  << colorText(" cut | ", kHintStyle)
                  << colorText("p", kKeyStyle)
                  << colorText(" paste | ", kHintStyle)
                  << colorText("d", kKeyStyle)
                  << colorText(" delete | ", kHintStyle)
                  << colorText("i", kKeyStyle)
                  << colorText(" info | ", kHintStyle)
                  << colorText("h", kKeyStyle)
                  << colorText(" help | ", kHintStyle)
                  << colorText("q", kKeyStyle)
                  << colorText(" quit", kHintStyle)
                  << "\n";
        const std::string selectedPermissionStatus = selectedTuxPermissionStatus(state);
        const std::string statusMessage = selectedPermissionStatus.empty()
            ? state.message
            : selectedPermissionStatus;
        std::cout << colorText("Status: ", kSectionStyle)
                  << statusMessage
                  << std::flush;
    }
}

KeyPress readKey() {
    const int ch = _getch();
    if (ch == 0 || ch == 224) {
        const int ext = _getch();
        switch (ext) {
            case 72: return {Key::Up, '\0'};
            case 80: return {Key::Down, '\0'};
            case 75: return {Key::Left, '\0'};
            case 77: return {Key::Right, '\0'};
            case 71: return {Key::Home, '\0'};
            case 79: return {Key::End, '\0'};
            default: return {Key::Unknown, '\0'};
        }
    }

    switch (ch) {
        case 13: return {Key::Enter, '\0'};
        case 27: return {Key::Escape, '\0'};
        case 8: return {Key::Backspace, '\0'};
        default:
            if (std::isprint(ch)) {
                return {Key::Character, static_cast<char>(ch)};
            }
            return {Key::Unknown, '\0'};
    }
}

void moveUp(ExplorerState& state) {
    if (state.cursor > 0) {
        --state.cursor;
    }
}

void moveDown(ExplorerState& state) {
    if (state.cursor + 1 < state.entries.size()) {
        ++state.cursor;
    }
}

void goParent(ExplorerState& state) {
    if (isSamePath(state.currentPath, state.rootPath) || !state.currentPath.has_parent_path()) {
        state.message = "Already at explorer root";
        return;
    }

    const fs::path parentPath = state.currentPath.parent_path();
    if (!isPathInsideRoot(parentPath, state.rootPath)) {
        state.message = "Explorer root boundary reached";
        return;
    }

    state.currentPath = parentPath;
    state.cursor = 0;
    state.scroll = 0;
    refresh(state);
}

const FileEntry* selectedEntry(const ExplorerState& state) {
    if (state.entries.empty() || state.cursor >= state.entries.size()) {
        return nullptr;
    }
    return &state.entries[state.cursor];
}

void selectPath(ExplorerState& state, const fs::path& path) {
    for (std::size_t index = 0; index < state.entries.size(); ++index) {
        if (isSamePath(state.entries[index].path, path)) {
            state.cursor = index;
            return;
        }
    }
}

fs::path uniquePasteTarget(const fs::path& requestedTarget) {
    std::error_code error;
    if (!fs::exists(requestedTarget, error)) {
        return requestedTarget;
    }

    const fs::path parent = requestedTarget.parent_path();
    const std::string stem = requestedTarget.stem().u8string();
    const std::string extension = requestedTarget.extension().u8string();

    for (int copyIndex = 1; copyIndex < 1000; ++copyIndex) {
        std::string filename = stem + " - copy";
        if (copyIndex > 1) {
            filename += " " + std::to_string(copyIndex);
        }
        filename += extension;

        fs::path candidate = parent / fs::u8path(filename);
        if (!fs::exists(candidate, error)) {
            return candidate;
        }
    }

    return requestedTarget;
}

void markClipboard(ExplorerState& state, ClipboardMode mode) {
    const FileEntry* entry = selectedEntry(state);
    if (entry == nullptr) {
        state.message = "Nothing selected";
        return;
    }

    const std::string permissionError = deletePermissionError(state, *entry);
    if (!permissionError.empty()) {
        state.message = redMessage(permissionError);
        return;
    }

    state.clipboard.mode = mode;
    state.clipboard.path = entry->path;
    state.clipboard.name = entry->name;
    state.clipboard.isDirectory = entry->isDirectory;
    state.message = mode == ClipboardMode::Copy
        ? "Copied " + entry->name
        : "Cut " + entry->name;
}

bool copyClipboardItem(const ClipboardState& clipboard, const fs::path& target, std::error_code& error) {
    if (clipboard.isDirectory) {
        fs::copy(clipboard.path, target, fs::copy_options::recursive, error);
        return !error;
    }

    fs::copy_file(clipboard.path, target, error);
    return !error;
}

void pasteClipboard(ExplorerState& state) {
    if (state.clipboard.mode == ClipboardMode::None) {
        state.message = "Clipboard is empty";
        return;
    }

    std::error_code error;
    if (!fs::exists(state.clipboard.path, error)) {
        state.message = redMessage("Clipboard source no longer exists.");
        state.clipboard = {};
        return;
    }

    FileEntry sourceEntry;
    sourceEntry.name = state.clipboard.name;
    sourceEntry.path = state.clipboard.path;
    sourceEntry.isDirectory = fs::is_directory(state.clipboard.path, error);
    if (error) {
        state.message = redMessage("Clipboard source check failed: " + error.message());
        return;
    }
    const std::string permissionError = deletePermissionError(state, sourceEntry);
    if (!permissionError.empty()) {
        state.message = redMessage(permissionError);
        return;
    }

    const fs::path requestedTarget = state.currentPath / fs::u8path(state.clipboard.name);
    if (state.clipboard.mode == ClipboardMode::Cut && isSamePath(state.clipboard.path, requestedTarget)) {
        state.clipboard = {};
        refresh(state);
        state.message = "Cut cancelled: item is already here";
        return;
    }

    fs::path target = uniquePasteTarget(requestedTarget);
    if (!isPathInsideRoot(target, state.rootPath)) {
        state.message = redMessage("Cannot paste outside explorer root.");
        return;
    }

    if (state.clipboard.isDirectory && isPathInsideRoot(target, state.clipboard.path)) {
        state.message = redMessage("Cannot paste a directory into itself.");
        return;
    }

    if (state.clipboard.mode == ClipboardMode::Copy) {
        copyClipboardItem(state.clipboard, target, error);
    } else {
        fs::rename(state.clipboard.path, target, error);
        if (error) {
            error.clear();
            if (copyClipboardItem(state.clipboard, target, error)) {
                if (state.clipboard.isDirectory) {
                    fs::remove_all(state.clipboard.path, error);
                } else {
                    fs::remove(state.clipboard.path, error);
                }
            }
        }
    }

    if (error) {
        state.message = redMessage("Paste failed: " + error.message());
        return;
    }

    const std::string pastedName = target.filename().u8string();
    state.clipboard = {};
    refresh(state);
    selectPath(state, target);
    state.message = "Pasted " + pastedName;
}

void beginCreateFolder(ExplorerState& state) {
    state.creatingFolder = true;
    state.newFolderName.clear();
    state.message = "Enter new folder name";
}

void createFolderFromInput(ExplorerState& state) {
    const std::string folderName = trimCopy(state.newFolderName);
    if (!isValidFolderName(folderName)) {
        state.message = redMessage("Invalid folder name.");
        return;
    }

    const fs::path target = state.currentPath / fs::u8path(folderName);
    if (!isPathInsideRoot(target, state.rootPath)) {
        state.message = redMessage("Cannot create outside explorer root.");
        return;
    }

    std::error_code error;
    if (fs::exists(target, error)) {
        state.message = redMessage("Folder already exists: " + folderName);
        return;
    }

    fs::create_directory(target, error);
    if (error) {
        state.message = redMessage("Create folder failed: " + error.message());
        return;
    }

    state.creatingFolder = false;
    state.newFolderName.clear();
    refresh(state);
    selectPath(state, target);
    state.message = "Created folder " + folderName;
}

void handleCreateFolderInput(ExplorerState& state, const KeyPress& key) {
    switch (key.key) {
        case Key::Escape:
            state.creatingFolder = false;
            state.newFolderName.clear();
            state.message = "Create folder cancelled";
            break;
        case Key::Enter:
            createFolderFromInput(state);
            break;
        case Key::Backspace:
            if (!state.newFolderName.empty()) {
                state.newFolderName.pop_back();
            }
            break;
        case Key::Character:
            if (std::isprint(static_cast<unsigned char>(key.character)) &&
                state.newFolderName.size() < 120) {
                state.newFolderName.push_back(key.character);
            }
            break;
        default:
            break;
    }
}

std::string deletePermissionError(const ExplorerState& state, const FileEntry& entry) {
    const std::string extension = extensionOf(entry.path);
    if (!isDebugUser(state.usertype)) {
        if (!entry.isDirectory && extension == ".dat") {
            return "User data files are debug-only.";
        }
        if (entry.isDirectory && directoryContainsExtension(entry.path, ".dat")) {
            return "Folders containing user data files are debug-only.";
        }
    }

    if (!entry.isDirectory && extension == ".tux" &&
        !can_modify_tux_file(pathToDisplayString(entry.path), state.username, state.usertype)) {
        return "You can only modify TUX files you created.";
    }
    if (entry.isDirectory &&
        directory_has_protected_tux_files(pathToDisplayString(entry.path), state.username, state.usertype)) {
        return "Folder contains TUX files you cannot modify.";
    }

    return "";
}

void requestDelete(ExplorerState& state) {
    const FileEntry* entry = selectedEntry(state);
    if (entry == nullptr) {
        state.message = "Nothing selected";
        return;
    }

    const std::string permissionError = deletePermissionError(state, *entry);
    if (!permissionError.empty()) {
        state.pendingDelete = false;
        state.message = redMessage(permissionError);
        return;
    }

    state.pendingDelete = true;
    state.pendingDeletePath = entry->path;
    state.pendingDeleteName = entry->name;
    state.message = redMessage("Press D to confirm delete: " + entry->name);
}

void confirmDelete(ExplorerState& state) {
    if (!state.pendingDelete) {
        state.message = "No pending delete";
        return;
    }

    const fs::path target = state.pendingDeletePath;
    if (!isPathInsideRoot(target, state.rootPath)) {
        state.pendingDelete = false;
        state.message = redMessage("Cannot delete outside explorer root.");
        return;
    }

    std::error_code error;
    if (!fs::exists(target, error)) {
        state.pendingDelete = false;
        refresh(state);
        state.message = "Delete skipped: item no longer exists";
        return;
    }

    FileEntry entry;
    entry.name = target.filename().u8string();
    entry.path = target;
    entry.isDirectory = fs::is_directory(target, error);
    if (error) {
        state.message = redMessage("Delete failed: " + error.message());
        return;
    }

    const std::string permissionError = deletePermissionError(state, entry);
    if (!permissionError.empty()) {
        state.pendingDelete = false;
        state.message = redMessage(permissionError);
        return;
    }

    if (entry.isDirectory) {
        fs::remove_all(target, error);
    } else {
        fs::remove(target, error);
    }

    if (error) {
        state.message = redMessage("Delete failed: " + error.message());
        return;
    }

    if (state.clipboard.mode != ClipboardMode::None &&
        (isSamePath(state.clipboard.path, target) ||
         (entry.isDirectory && isPathInsideRoot(state.clipboard.path, target)))) {
        state.clipboard = {};
    }

    const std::string deletedName = state.pendingDeleteName;
    state.pendingDelete = false;
    refresh(state);
    state.message = "Deleted " + deletedName;
}

void openSelected(ExplorerState& state) {
    const FileEntry* entry = selectedEntry(state);
    if (entry == nullptr) {
        state.message = "Nothing selected";
        return;
    }

    const FileEntry selected = *entry;
    if (selected.isDirectory) {
        if (!isPathInsideRoot(selected.path, state.rootPath)) {
            state.message = "Cannot leave explorer root";
            return;
        }
        state.currentPath = selected.path;
        state.cursor = 0;
        state.scroll = 0;
        refresh(state);
        return;
    }

    if (extensionOf(selected.path) == ".tux") {
        std::cout << "\x1b[?25h" << std::flush;
        const int result = open_tux_file_in_editor(
            pathToDisplayString(selected.path),
            selected.name,
            state.username,
            state.usertype,
            true
        );
        std::cout << "\x1b[?25l" << std::flush;
        if (result == 0) {
            state.message = "Decrypted and edited " + selected.name;
            refresh(state);
        } else if (result == 2) {
            state.message = redMessage("TUX file is corrupted or invalid.");
        } else if (result == 3) {
            state.message = redMessage("Access denied: only the creator, admin, or debug can edit this TUX file.");
        } else if (result == 7) {
            state.message = "Viewed read-only " + selected.name;
        } else {
            state.message = redMessage("Failed to decrypt and open TUX file.");
        }
        return;
    }
    if (extensionOf(selected.path) == ".dat") {
        state.message = redMessage("User data file cannot be opened from explorer.");
        return;
    }

    if (opensWithEditor(selected.path)) {
        std::cout << "\x1b[?25h" << std::flush;
        const int result = run_editor(pathToDisplayString(selected.path), selected.name);
        std::cout << "\x1b[?25l" << std::flush;
        state.message = result == 0
            ? "Edited " + selected.name
            : "Editor exited with code " + std::to_string(result);
        refresh(state);
        return;
    }

    const HINSTANCE result = ShellExecuteW(
        nullptr,
        L"open",
        selected.path.wstring().c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL
    );
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        state.message = "Open failed";
    } else {
        state.message = "Opened " + selected.name;
    }
}

void beginShowDetails(ExplorerState& state) {
    if (state.entries.empty() || state.cursor >= state.entries.size()) {
        state.message = redMessage("Nothing selected");
        return;
    }

    state.detailLines = buildDetailLines(state);
    state.detailName = state.entries[state.cursor].name;
    state.detailScroll = 0;
    state.showDetails = true;
}

std::size_t maxDetailScroll(const ExplorerState& state) {
    const COORD size = consoleSize();
    const std::size_t height = std::max<int>(size.Y, 18);
    const std::size_t rows = detailVisibleRows(height);
    return state.detailLines.size() > rows ? state.detailLines.size() - rows : 0;
}

void scrollDetailsUp(ExplorerState& state) {
    if (state.detailScroll > 0) {
        --state.detailScroll;
    }
}

void scrollDetailsDown(ExplorerState& state) {
    state.detailScroll = std::min(state.detailScroll + 1, maxDetailScroll(state));
}

bool handleDetailsKey(ExplorerState& state, const KeyPress& key) {
    switch (key.key) {
        case Key::Escape:
        case Key::Enter:
            state.showDetails = false;
            return true;
        case Key::Up:
            scrollDetailsUp(state);
            return true;
        case Key::Down:
            scrollDetailsDown(state);
            return true;
        case Key::Home:
            state.detailScroll = 0;
            return true;
        case Key::End:
            state.detailScroll = maxDetailScroll(state);
            return true;
        case Key::Character:
            switch (key.character) {
                case 'i':
                case 'I':
                case 'q':
                case 'Q':
                    state.showDetails = false;
                    break;
                case 'k':
                    scrollDetailsUp(state);
                    break;
                case 'j':
                    scrollDetailsDown(state);
                    break;
                default:
                    break;
            }
            return true;
        default:
            return true;
    }
}

bool handleKey(ExplorerState& state, const KeyPress& key) {
    if (state.creatingFolder) {
        handleCreateFolderInput(state, key);
        return true;
    }

    if (state.showDetails) {
        return handleDetailsKey(state, key);
    }

    if (state.showHelp) {
        if (key.key == Key::Escape || key.key == Key::Enter ||
            (key.key == Key::Character &&
             (key.character == 'h' || key.character == 'H' ||
              key.character == 'q' || key.character == 'Q'))) {
            state.showHelp = false;
        }
        return true;
    }

    switch (key.key) {
        case Key::Escape:
            return false;
        case Key::Up:
            moveUp(state);
            break;
        case Key::Down:
            moveDown(state);
            break;
        case Key::Left:
        case Key::Backspace:
            goParent(state);
            break;
        case Key::Home:
            state.cursor = 0;
            break;
        case Key::End:
            state.cursor = state.entries.empty() ? 0 : state.entries.size() - 1;
            break;
        case Key::Enter:
        case Key::Right:
            openSelected(state);
            break;
        case Key::Character:
            switch (key.character) {
                case 'q':
                case 'Q':
                    return false;
                case 'k':
                    moveUp(state);
                    break;
                case 'j':
                    moveDown(state);
                    break;
                case 'g':
                    state.cursor = 0;
                    break;
                case 'G':
                    state.cursor = state.entries.empty() ? 0 : state.entries.size() - 1;
                    break;
                case 'b':
                    goParent(state);
                    break;
                case 'h':
                case 'H':
                    state.showHelp = true;
                    break;
                case 'i':
                case 'I':
                    beginShowDetails(state);
                    break;
                case 'c':
                case 'C':
                    markClipboard(state, ClipboardMode::Copy);
                    break;
                case 'x':
                case 'X':
                    markClipboard(state, ClipboardMode::Cut);
                    break;
                case 'p':
                case 'P':
                    pasteClipboard(state);
                    break;
                case 'n':
                case 'N':
                    beginCreateFolder(state);
                    break;
                case 'd':
                    requestDelete(state);
                    break;
                case 'D':
                    confirmDelete(state);
                    break;
                case 'o':
                    openSelected(state);
                    break;
                case '.':
                    state.showHidden = !state.showHidden;
                    refresh(state);
                    break;
                case 'r':
                case 'R':
                    refresh(state);
                    break;
                default:
                    state.message = std::string("Unknown command: ") + key.character;
                    break;
            }
            break;
        case Key::Unknown:
            break;
    }
    return true;
}
}

void open_explorer(const std::string& username, const std::string& usertype) {
    ConsoleScreenGuard screenGuard;

    ExplorerState state;
    state.rootPath = normalizedPath(fs::current_path());
    state.currentPath = state.rootPath;
    state.username = username;
    state.usertype = usertype;
    refresh(state);

    bool running = true;
    while (running) {
        const COORD size = consoleSize();
        const std::size_t rows = std::max<int>(size.Y, 18) > 8
            ? static_cast<std::size_t>(std::max<int>(size.Y, 18) - 8)
            : 10;
        keepCursorVisible(state, rows);
        render(state, username, usertype);
        running = handleKey(state, readKey());
    }
}
