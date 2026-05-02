#include "console_screen.hpp"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <vector>
#endif

namespace {
int activeScreenGuardDepth = 0;
}

#ifdef _WIN32
struct ConsoleScreenSnapshot {
    HANDLE outputHandle = INVALID_HANDLE_VALUE;
    COORD cursorPosition{0, 0};
    SMALL_RECT window{0, 0, 0, 0};
    SHORT width = 0;
    SHORT height = 0;
    WORD attributes = 0;
    std::vector<CHAR_INFO> buffer;

    bool save(HANDLE handle) {
        outputHandle = handle;
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (outputHandle == INVALID_HANDLE_VALUE ||
            !GetConsoleScreenBufferInfo(outputHandle, &info)) {
            return false;
        }

        cursorPosition = info.dwCursorPosition;
        window = info.srWindow;
        width = window.Right - window.Left + 1;
        height = window.Bottom - window.Top + 1;
        attributes = info.wAttributes;
        if (width <= 0 || height <= 0) {
            return false;
        }

        buffer.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
        COORD bufferSize{width, height};
        COORD bufferCoord{0, 0};
        SMALL_RECT readRegion = window;
        return ReadConsoleOutput(outputHandle, buffer.data(), bufferSize, bufferCoord, &readRegion) != 0;
    }

    void restore() const {
        if (outputHandle == INVALID_HANDLE_VALUE || buffer.empty()) {
            return;
        }

        COORD bufferSize{width, height};
        COORD bufferCoord{0, 0};
        SMALL_RECT writeRegion = window;
        WriteConsoleOutput(outputHandle, buffer.data(), bufferSize, bufferCoord, &writeRegion);
        SetConsoleTextAttribute(outputHandle, attributes);
        SetConsoleCursorPosition(outputHandle, cursorPosition);
    }
};
#else
struct ConsoleScreenSnapshot {};
#endif

ConsoleScreenGuard::ConsoleScreenGuard(bool clearOnEnter) {
#ifdef _WIN32
    outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (outputHandle != INVALID_HANDLE_VALUE &&
        GetConsoleMode(static_cast<HANDLE>(outputHandle), &mode)) {
        originalOutputMode = mode;
        outputModeSaved = true;
        SetConsoleMode(
            static_cast<HANDLE>(outputHandle),
            mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING
        );
    }

    if (activeScreenGuardDepth > 0) {
        auto nestedSnapshot = std::make_unique<ConsoleScreenSnapshot>();
        if (nestedSnapshot->save(static_cast<HANDLE>(outputHandle))) {
            snapshot = std::move(nestedSnapshot);
        }
    }
#endif

    enteredAlternateScreen = (activeScreenGuardDepth == 0);
    if (enteredAlternateScreen) {
        std::cout << "\x1b[?1049h";
        std::cout.flush();
    }

    ++activeScreenGuardDepth;
    if (clearOnEnter) {
        clearConsoleScreen();
    }
    active = true;
}

ConsoleScreenGuard::~ConsoleScreenGuard() {
    if (!active) {
        return;
    }

    if (activeScreenGuardDepth > 0) {
        --activeScreenGuardDepth;
    }

#ifdef _WIN32
    if (snapshot) {
        std::cout << "\x1b[0m\x1b[?25h";
        std::cout.flush();
        snapshot->restore();
    } else if (enteredAlternateScreen) {
#else
    if (enteredAlternateScreen) {
#endif
        std::cout << "\x1b[0m\x1b[?25h\x1b[?1049l";
        std::cout.flush();
    }

#ifdef _WIN32
    if (outputModeSaved && outputHandle != INVALID_HANDLE_VALUE) {
        SetConsoleMode(static_cast<HANDLE>(outputHandle), originalOutputMode);
    }
#endif
}

void clearConsoleScreen() {
#ifdef _WIN32
    HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (outputHandle != INVALID_HANDLE_VALUE &&
        GetConsoleScreenBufferInfo(outputHandle, &info)) {
        const DWORD consoleSize = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
        const COORD origin{0, 0};
        DWORD written = 0;
        FillConsoleOutputCharacter(outputHandle, ' ', consoleSize, origin, &written);
        FillConsoleOutputAttribute(outputHandle, info.wAttributes, consoleSize, origin, &written);
        SetConsoleCursorPosition(outputHandle, origin);
        return;
    }
#endif

    std::cout << "\x1b[2J\x1b[H";
    std::cout.flush();
}
