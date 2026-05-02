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
    bool creatingFolder = false;
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

std::string redMessage(const std::string& message) {
    return "\x1b[31m" + message + "\x1b[0m";
}

std::string greenText(const std::string& message) {
    return "\x1b[32m" + message + "\x1b[0m";
}

std::string grayText(const std::string& message) {
    return "\x1b[90m" + message + "\x1b[0m";
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

std::string row(
    const std::string& left,
    const std::string& center,
    const std::string& right,
    std::size_t parentWidth,
    std::size_t currentWidth,
    std::size_t previewWidth
) {
    return "|" + trimToWidth(left, parentWidth) +
           "|" + trimToWidth(center, currentWidth) +
           "|" + trimToWidth(right, previewWidth) + "|";
}

std::string formatParentEntry(const FileEntry& entry) {
    return std::string(entry.isDirectory ? "[D] " : "    ") + entry.name;
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

    const std::string prefix = std::string(selected ? "> " : "  ") +
        (entry->isDirectory ? "[D]" : "   ") +
        (entry->isHidden ? "." : " ") + " ";
    const std::string suffix = "  " + std::string(entry->isDirectory ? "<DIR>" : formatSize(entry->size));
    const std::size_t fixedWidth = prefix.size() + suffix.size();

    if (fixedWidth >= width) {
        return trimToWidth(formatCurrentEntry(*entry, selected), width);
    }

    const std::size_t nameWidth = width - fixedWidth;
    const std::string name = fitText(entry->name, nameWidth);
    std::string coloredName = name;
    if (clipboardMatches(state, *entry)) {
        coloredName = state.clipboard.mode == ClipboardMode::Copy
            ? greenText(name)
            : grayText(name);
    }

    std::string cell = prefix + coloredName + suffix;
    const std::size_t visibleWidth = prefix.size() + name.size() + suffix.size();
    if (visibleWidth < width) {
        cell.append(width - visibleWidth, ' ');
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

void renderHelp(const ExplorerState& state, const std::string& username, const std::string& usertype) {
    std::cout << "\x1b[2J\x1b[H\x1b[?25l";
    std::cout << "TundraUX Explorer Help - " << usertype << ": " << username << "\n";
    std::cout << pathToDisplayString(state.currentPath) << "\n\n";
    std::cout << "Navigation\n";
    std::cout << "  Up/Down or j/k       Move cursor\n";
    std::cout << "  Enter or o           Open file or enter directory\n";
    std::cout << "  Left, Backspace, b   Go to parent directory\n";
    std::cout << "  g / G                Jump to top / bottom\n\n";
    std::cout << "File operations\n";
    std::cout << "  c                    Copy selected file or folder, shown in green until paste\n";
    std::cout << "  x                    Cut selected file or folder, shown in gray until paste\n";
    std::cout << "  p                    Paste into current directory\n\n";
    std::cout << "  n                    Create a new folder in the current directory\n";
    std::cout << "  d                    Request delete for selected item\n";
    std::cout << "  D                    Confirm pending delete\n\n";
    std::cout << "View\n";
    std::cout << "  .                    Show or hide hidden files\n";
    std::cout << "  r                    Refresh current directory\n";
    std::cout << "  h                    Toggle this help menu\n";
    std::cout << "  q or Esc             Quit explorer from main view, close help from here\n\n";
    std::cout << "Special file handling\n";
    std::cout << "  .md / .txt           Open with Tundra editor\n";
    std::cout << "  .tux                 Decrypt and open with Tundra editor\n";
    std::cout << "  .dat                 Preview as user data file; cannot open here\n\n";
    std::cout << "Press h, q, Esc, or Enter to return." << std::flush;
}

void render(const ExplorerState& state, const std::string& username, const std::string& usertype) {
    if (state.showHelp) {
        renderHelp(state, username, usertype);
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

    std::cout << "\x1b[2J\x1b[H\x1b[?25l";
    std::cout << "TundraUX Explorer - " << usertype << ": " << username << "\n";
    std::cout << pathToDisplayString(state.currentPath) << "\n";
    std::cout << border(parentWidth, currentWidth, previewWidth) << "\n";
    std::cout << row("Parent", "Current", "Preview", parentWidth, currentWidth, previewWidth) << "\n";
    std::cout << border(parentWidth, currentWidth, previewWidth) << "\n";

    for (std::size_t rowIndex = 0; rowIndex < rows; ++rowIndex) {
        const std::string parentText = rowIndex < state.parentEntries.size()
            ? formatParentEntry(state.parentEntries[rowIndex])
            : "";
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
        std::cout << "|"
                  << trimToWidth(parentText, parentWidth)
                  << "|"
                  << currentText
                  << "|"
                  << trimToWidth(previewText, previewWidth)
                  << "|\n";
    }

    std::cout << border(parentWidth, currentWidth, previewWidth) << "\n";
    if (state.creatingFolder) {
        std::cout << "Enter create | Backspace edit | Esc cancel\n";
        std::cout << "New folder name: " << state.newFolderName << std::flush;
    } else {
        std::cout << "Enter open | Backspace parent | n mkdir | c copy | x cut | p paste | d delete | h help | q quit\n";
        std::cout << state.message << std::flush;
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

std::string deletePermissionError(const ExplorerState& state, const FileEntry& entry);

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

bool handleKey(ExplorerState& state, const KeyPress& key) {
    if (state.creatingFolder) {
        handleCreateFolderInput(state, key);
        return true;
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
