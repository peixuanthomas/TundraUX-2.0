#include "explorer_input.hpp"

#include "explorer_actions.hpp"
#include "explorer_directory.hpp"

#include <cctype>
#include <conio.h>
#include <string>

namespace tundraux::explorer {

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
        case 9: return {Key::Tab, '\0'};
        default:
            if (std::isprint(ch)) {
                return {Key::Character, static_cast<char>(ch)};
            }
            return {Key::Unknown, '\0'};
    }
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
    if (state.search.active) {
        handleSearchInput(state, key);
        return true;
    }

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
                case 's':
                case 'S':
                case '/':
                    beginSearch(state);
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
