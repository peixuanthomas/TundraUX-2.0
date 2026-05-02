#include "console_screen.hpp"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
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
#endif

    std::cout << "\x1b[?1049h";
    std::cout.flush();
    if (clearOnEnter) {
        clearConsoleScreen();
    }
    active = true;
}

ConsoleScreenGuard::~ConsoleScreenGuard() {
    if (active) {
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
