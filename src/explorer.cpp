#include "explorer.hpp"

#include "console_screen.hpp"
#include "editor.hpp"

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

struct ExplorerState {
    fs::path rootPath;
    fs::path currentPath;
    std::vector<FileEntry> entries;
    std::vector<FileEntry> parentEntries;
    std::size_t cursor = 0;
    std::size_t scroll = 0;
    bool showHidden = false;
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

std::string redMessage(const std::string& message) {
    return "\x1b[31m" + message + "\x1b[0m";
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
        return {"This file needs TUXfile manager to open."};
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

std::string formatCurrentEntry(const FileEntry& entry, bool selected) {
    std::ostringstream out;
    out << (selected ? "> " : "  ")
        << (entry.isDirectory ? "[D]" : "   ")
        << (entry.isHidden ? "." : " ")
        << ' ' << entry.name << "  "
        << (entry.isDirectory ? "<DIR>" : formatSize(entry.size));
    return out.str();
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

void render(const ExplorerState& state, const std::string& username, const std::string& usertype) {
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
        const std::string currentText = entryIndex < state.entries.size()
            ? formatCurrentEntry(state.entries[entryIndex], entryIndex == state.cursor)
            : "";
        const std::string previewText = rowIndex < previewLines.size() ? previewLines[rowIndex] : "";
        std::cout << row(parentText, currentText, previewText, parentWidth, currentWidth, previewWidth) << "\n";
    }

    std::cout << border(parentWidth, currentWidth, previewWidth) << "\n";
    std::cout << "Up/Down/j/k move | Enter/o open | Left/Backspace/h parent | . hidden | r refresh | q/Esc quit\n";
    std::cout << state.message << std::flush;
}

KeyPress readKey() {
    const int ch = _getch();
    if (ch == 0 || ch == 224) {
        const int ext = _getch();
        switch (ext) {
            case 72: return {Key::Up, '\0'};
            case 80: return {Key::Down, '\0'};
            case 75: return {Key::Left, '\0'};
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

void openSelected(ExplorerState& state) {
    if (state.entries.empty() || state.cursor >= state.entries.size()) {
        state.message = "Nothing selected";
        return;
    }

    const FileEntry selected = state.entries[state.cursor];
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
        state.message = redMessage("This file needs TUXfile manager to open.");
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
                case 'h':
                case 'b':
                    goParent(state);
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
