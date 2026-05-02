#pragma once

#include <memory>

void clearConsoleScreen();

struct ConsoleScreenSnapshot;

class ConsoleScreenGuard {
public:
    explicit ConsoleScreenGuard(bool clearOnEnter = true);
    ~ConsoleScreenGuard();

    ConsoleScreenGuard(const ConsoleScreenGuard&) = delete;
    ConsoleScreenGuard& operator=(const ConsoleScreenGuard&) = delete;

private:
    bool active = false;
    bool enteredAlternateScreen = false;
    std::unique_ptr<ConsoleScreenSnapshot> snapshot;

#ifdef _WIN32
    void* outputHandle = nullptr;
    unsigned long originalOutputMode = 0;
    bool outputModeSaved = false;
#endif
};
