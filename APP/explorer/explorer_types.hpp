#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tundraux::explorer {
namespace fs = std::filesystem;

struct FileEntry {
    std::string name;
    fs::path path;
    bool isDirectory = false;
    bool isHidden = false;
    std::uintmax_t size = 0;
};

enum class SearchMode {
    Exact,
    Partial,
    Regex
};

struct SearchState {
    bool active = false;
    bool hasRun = false;
    SearchMode mode = SearchMode::Partial;
    fs::path rootPath;
    std::string query;
    std::vector<FileEntry> results;
    std::size_t cursor = 0;
    std::size_t scroll = 0;
    std::uintmax_t scanErrors = 0;
    std::string error;
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
    SearchState search;
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
    End,
    Tab
};

struct KeyPress {
    Key key = Key::Unknown;
    char character = '\0';
};

}
