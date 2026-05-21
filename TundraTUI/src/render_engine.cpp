#include "TundraTUI/render_engine.hpp"

#include "TundraTUI/screen.hpp"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace tundra_tui {
namespace {

constexpr std::size_t kFooterSeparatorWidth = 3;

}

RenderEngine::RenderEngine(std::ostream& outputStream)
    : output(&outputStream) {}

void RenderEngine::clear() {
    clearConsoleScreen();
}

void RenderEngine::moveCursor(int row, int column) {
    *output << "\x1b[" << (row + 1) << ";" << (column + 1) << "H";
}

void RenderEngine::hideCursor() {
    *output << "\x1b[?25l";
}

void RenderEngine::showCursor() {
    *output << "\x1b[?25h";
}

void RenderEngine::resetStyle() {
    *output << "\x1b[0m";
}

void RenderEngine::write(const std::string& text) {
    *output << text;
}

void RenderEngine::writeLine(const std::string& text) {
    *output << text << '\n';
}

void RenderEngine::flush() {
    output->flush();
}

Size terminalSize() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(output, &info)) {
        return {
            static_cast<int>(info.srWindow.Right - info.srWindow.Left + 1),
            static_cast<int>(info.srWindow.Bottom - info.srWindow.Top + 1)
        };
    }
#else
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        return {static_cast<int>(ws.ws_col), static_cast<int>(ws.ws_row)};
    }
#endif
    return {120, 30};
}

std::size_t footerHintWidth(const FooterHint& hint) {
    if (hint.key.empty()) {
        return hint.label.size();
    }
    if (hint.label.empty()) {
        return hint.key.size();
    }
    return hint.key.size() + 1 + hint.label.size();
}

std::vector<FooterHint> fitFooterHints(
    const std::vector<FooterHint>& hints,
    Size terminal,
    std::size_t reservedWidth
) {
    if (terminal.width <= 0 || terminal.height <= 0) {
        return {};
    }

    const std::size_t availableWidth = static_cast<std::size_t>(terminal.width);
    if (reservedWidth >= availableWidth) {
        return {};
    }

    const std::size_t contentWidth = availableWidth - reservedWidth;
    std::vector<FooterHint> visible;
    std::size_t usedWidth = 0;

    for (const FooterHint& hint : hints) {
        const std::size_t hintWidth = footerHintWidth(hint);
        const std::size_t separatorWidth = visible.empty() ? 0 : kFooterSeparatorWidth;
        if (hintWidth == 0 || usedWidth + separatorWidth + hintWidth > contentWidth) {
            break;
        }

        visible.push_back(hint);
        usedWidth += separatorWidth + hintWidth;
    }

    return visible;
}

}
