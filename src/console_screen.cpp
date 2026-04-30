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
    if (clearOnEnter) {
        std::cout << "\x1b[2J\x1b[H";
    }
    std::cout.flush();
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
