#include "explorer_search.hpp"

#include "explorer_directory.hpp"
#include "explorer_navigation.hpp"
#include "explorer_open.hpp"
#include "explorer_style.hpp"
#include "explorer_text.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <system_error>

namespace tundraux::explorer {
namespace {

std::string relativeSearchPath(const fs::path& path, const fs::path& rootPath) {
    std::error_code error;
    const fs::path relative = fs::relative(path, rootPath, error);
    return error ? pathToDisplayString(path) : relative.u8string();
}

bool nameMatches(const std::string& name, const std::string& query, SearchMode mode, const std::regex& regexPattern) {
    if (mode == SearchMode::Regex) {
        return std::regex_search(name, regexPattern);
    }

    const std::string lowerName = toLowerCopy(name);
    const std::string lowerQuery = toLowerCopy(query);
    if (mode == SearchMode::Exact) {
        return lowerName == lowerQuery;
    }
    return lowerName.find(lowerQuery) != std::string::npos;
}

FileEntry makeSearchEntry(const fs::path& path, const fs::file_status& status, bool hidden) {
    FileEntry entry;
    entry.name = path.filename().u8string();
    entry.path = path;
    entry.isDirectory = fs::is_directory(status);
    entry.isHidden = hidden;
    if (!entry.isDirectory && fs::is_regular_file(status)) {
        std::error_code sizeError;
        entry.size = fs::file_size(path, sizeError);
        if (sizeError) {
            entry.size = 0;
        }
    }
    return entry;
}

void clearSearchResults(SearchState& search) {
    search.hasRun = false;
    search.results.clear();
    search.cursor = 0;
    search.scroll = 0;
    search.scanErrors = 0;
    search.error.clear();
}

SearchMode nextSearchMode(SearchMode mode) {
    switch (mode) {
        case SearchMode::Exact:
            return SearchMode::Partial;
        case SearchMode::Partial:
            return SearchMode::Regex;
        case SearchMode::Regex:
            return SearchMode::Exact;
    }
    return SearchMode::Partial;
}

} // namespace

std::string searchModeLabel(SearchMode mode) {
    switch (mode) {
        case SearchMode::Exact:
            return "Exact";
        case SearchMode::Partial:
            return "Partial";
        case SearchMode::Regex:
            return "Regex";
    }
    return "Partial";
}

void beginSearch(ExplorerState& state) {
    state.search.active = true;
    state.search.hasRun = false;
    state.search.rootPath = state.currentPath;
    state.search.query.clear();
    state.search.results.clear();
    state.search.cursor = 0;
    state.search.scroll = 0;
    state.search.scanErrors = 0;
    state.search.error.clear();
    state.showHelp = false;
    state.showDetails = false;
    state.creatingFolder = false;
    state.newFolderName.clear();
    state.message = "Search file names from current directory";
}

void cancelSearch(ExplorerState& state) {
    state.search.active = false;
    state.message = "Search closed";
}

void cycleSearchMode(ExplorerState& state) {
    state.search.mode = nextSearchMode(state.search.mode);
    clearSearchResults(state.search);
    state.message = "Search mode: " + searchModeLabel(state.search.mode);
}

void runSearch(ExplorerState& state) {
    SearchState& search = state.search;
    const std::string query = trimCopy(search.query);
    search.results.clear();
    search.cursor = 0;
    search.scroll = 0;
    search.scanErrors = 0;
    search.error.clear();
    search.hasRun = true;

    if (query.empty()) {
        search.error = "Enter a file name query.";
        state.message = redMessage(search.error);
        return;
    }

    std::regex regexPattern;
    if (search.mode == SearchMode::Regex) {
        try {
            regexPattern = std::regex(
                query,
                std::regex_constants::ECMAScript | std::regex_constants::icase
            );
        } catch (const std::regex_error& error) {
            search.error = std::string("Invalid regex: ") + error.what();
            state.message = redMessage(search.error);
            return;
        }
    }

    std::error_code error;
    fs::recursive_directory_iterator iterator(
        search.rootPath,
        fs::directory_options::skip_permission_denied,
        error
    );
    const fs::recursive_directory_iterator end;
    if (error) {
        ++search.scanErrors;
        error.clear();
    }

    while (iterator != end) {
        if (error) {
            ++search.scanErrors;
            error.clear();
            iterator.increment(error);
            continue;
        }

        const fs::path itemPath = iterator->path();
        std::error_code statusError;
        const fs::file_status status = iterator->symlink_status(statusError);
        if (statusError) {
            ++search.scanErrors;
            iterator.increment(error);
            continue;
        }

        const bool isDirectory = fs::is_directory(status);
        if (!isPathInsideRoot(itemPath, state.rootPath)) {
            if (isDirectory) {
                iterator.disable_recursion_pending();
            }
            iterator.increment(error);
            continue;
        }

        const bool hidden = isHiddenPath(itemPath);
        if (!state.showHidden && hidden) {
            if (isDirectory) {
                iterator.disable_recursion_pending();
            }
            iterator.increment(error);
            continue;
        }

        if (!isDirectory && nameMatches(itemPath.filename().u8string(), query, search.mode, regexPattern)) {
            search.results.push_back(makeSearchEntry(itemPath, status, hidden));
        }

        iterator.increment(error);
    }

    if (error) {
        ++search.scanErrors;
    }

    std::stable_sort(search.results.begin(), search.results.end(), [&search](const FileEntry& left, const FileEntry& right) {
        return toLowerCopy(relativeSearchPath(left.path, search.rootPath)) <
               toLowerCopy(relativeSearchPath(right.path, search.rootPath));
    });

    state.message = std::to_string(search.results.size()) + " search result(s)";
    if (search.scanErrors > 0) {
        state.message += ", " + std::to_string(search.scanErrors) + " scan error(s)";
    }
}

void moveSearchUp(ExplorerState& state) {
    if (state.search.cursor > 0) {
        --state.search.cursor;
    }
}

void moveSearchDown(ExplorerState& state) {
    if (state.search.cursor + 1 < state.search.results.size()) {
        ++state.search.cursor;
    }
}

void jumpSearchTop(ExplorerState& state) {
    state.search.cursor = 0;
}

void jumpSearchBottom(ExplorerState& state) {
    if (!state.search.results.empty()) {
        state.search.cursor = state.search.results.size() - 1;
    }
}

void openSearchResult(ExplorerState& state) {
    if (state.search.results.empty() || state.search.cursor >= state.search.results.size()) {
        state.message = "No search result selected";
        return;
    }

    const fs::path resultPath = state.search.results[state.search.cursor].path;
    const fs::path parentPath = resultPath.has_parent_path() ? resultPath.parent_path() : state.search.rootPath;
    state.search.active = false;
    if (isPathInsideRoot(parentPath, state.rootPath)) {
        state.currentPath = parentPath;
    }
    state.cursor = 0;
    state.scroll = 0;
    refresh(state);
    selectPath(state, resultPath);
    openSelected(state);
}

void handleSearchInput(ExplorerState& state, const KeyPress& key) {
    switch (key.key) {
        case Key::Escape:
            cancelSearch(state);
            break;
        case Key::Tab:
            cycleSearchMode(state);
            break;
        case Key::Enter:
            runSearch(state);
            break;
        case Key::Up:
            moveSearchUp(state);
            break;
        case Key::Down:
            moveSearchDown(state);
            break;
        case Key::Home:
            jumpSearchTop(state);
            break;
        case Key::End:
            jumpSearchBottom(state);
            break;
        case Key::Right:
            openSearchResult(state);
            break;
        case Key::Backspace:
            if (!state.search.query.empty()) {
                state.search.query.pop_back();
                clearSearchResults(state.search);
            }
            break;
        case Key::Character:
            if (std::isprint(static_cast<unsigned char>(key.character)) &&
                state.search.query.size() < 160) {
                state.search.query.push_back(key.character);
                clearSearchResults(state.search);
            }
            break;
        default:
            break;
    }
}

std::vector<std::string> previewSearchSelected(const ExplorerState& state) {
    if (state.search.results.empty() || state.search.cursor >= state.search.results.size()) {
        return {"No search result selected"};
    }

    const FileEntry& entry = state.search.results[state.search.cursor];
    return {
        "Name: " + entry.name,
        "Path: " + relativeSearchPath(entry.path, state.search.rootPath),
        "Size: " + formatSize(entry.size),
        "Content search: "
    };
}

}
