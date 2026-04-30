#pragma once

class ConsoleScreenGuard {
public:
    explicit ConsoleScreenGuard(bool clearOnEnter = true);
    ~ConsoleScreenGuard();

    ConsoleScreenGuard(const ConsoleScreenGuard&) = delete;
    ConsoleScreenGuard& operator=(const ConsoleScreenGuard&) = delete;

private:
    bool active = false;

#ifdef _WIN32
    void* outputHandle = nullptr;
    unsigned long originalOutputMode = 0;
    bool outputModeSaved = false;
#endif
};
