#include "TundraTUI/input.hpp"

#include <cctype>

#ifdef _WIN32
#include <conio.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace tundra_tui {
namespace {

KeyAuditSink g_keyAuditSink = nullptr;

#ifndef _WIN32
class RawTerminalMode {
public:
    RawTerminalMode() {
        active = tcgetattr(STDIN_FILENO, &original) == 0;
        if (!active) {
            return;
        }

        termios raw = original;
        raw.c_iflag &= static_cast<unsigned int>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
        raw.c_oflag &= static_cast<unsigned int>(~OPOST);
        raw.c_cflag |= CS8;
        raw.c_lflag &= static_cast<unsigned int>(~(ECHO | ICANON | IEXTEN | ISIG));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            active = false;
        }
    }

    ~RawTerminalMode() {
        if (active) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
        }
    }

    RawTerminalMode(const RawTerminalMode&) = delete;
    RawTerminalMode& operator=(const RawTerminalMode&) = delete;

private:
    termios original{};
    bool active = false;
};

int readByte() {
    unsigned char ch = 0;
    const ssize_t count = read(STDIN_FILENO, &ch, 1);
    return count == 1 ? static_cast<int>(ch) : -1;
}

bool hasInput(int timeoutMs) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    return select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout) > 0;
}

int readByteWithTimeout(int timeoutMs) {
    if (!hasInput(timeoutMs)) {
        return -1;
    }
    return readByte();
}

KeyPress decodeEscapeSequence() {
    const int marker = readByteWithTimeout(30);
    if (marker < 0) {
        return {Key::Escape, '\0'};
    }

    if (marker == 'O') {
        const int final = readByteWithTimeout(30);
        switch (final) {
            case 'P': return {Key::F1, '\0'};
            case 'Q': return {Key::F2, '\0'};
            case 'H': return {Key::Home, '\0'};
            case 'F': return {Key::End, '\0'};
            default: return {Key::Unknown, '\0'};
        }
    }

    if (marker != '[') {
        return {Key::Unknown, '\0'};
    }

    int first = readByteWithTimeout(30);
    if (first < 0) {
        return {Key::Unknown, '\0'};
    }

    switch (first) {
        case 'A': return {Key::Up, '\0'};
        case 'B': return {Key::Down, '\0'};
        case 'C': return {Key::Right, '\0'};
        case 'D': return {Key::Left, '\0'};
        case 'H': return {Key::Home, '\0'};
        case 'F': return {Key::End, '\0'};
        default:
            break;
    }

    int value = 0;
    while (first >= '0' && first <= '9') {
        value = (value * 10) + (first - '0');
        first = readByteWithTimeout(30);
        if (first < 0) {
            return {Key::Unknown, '\0'};
        }
    }

    if (first != '~') {
        return {Key::Unknown, '\0'};
    }

    switch (value) {
        case 1:
        case 7:
            return {Key::Home, '\0'};
        case 3:
            return {Key::Delete, '\0'};
        case 4:
        case 8:
            return {Key::End, '\0'};
        case 5:
            return {Key::PageUp, '\0'};
        case 6:
            return {Key::PageDown, '\0'};
        case 11:
            return {Key::F1, '\0'};
        case 12:
            return {Key::F2, '\0'};
        default:
            return {Key::Unknown, '\0'};
    }
}
#endif

}

void setKeyAuditSink(KeyAuditSink sink) {
    g_keyAuditSink = sink;
}

void emitKeyAudit(const KeyPress& key, bool sensitive) {
    if (g_keyAuditSink == nullptr) {
        return;
    }
    g_keyAuditSink(key, sensitive);
}

KeyPress readKey() {
    KeyPress result{Key::Unknown, '\0'};
#ifdef _WIN32
    const int ch = _getch();
    if (ch == 0 || ch == 224) {
        const int ext = _getch();
        switch (ext) {
            case 72: result = {Key::Up, '\0'}; break;
            case 80: result = {Key::Down, '\0'}; break;
            case 75: result = {Key::Left, '\0'}; break;
            case 77: result = {Key::Right, '\0'}; break;
            case 71: result = {Key::Home, '\0'}; break;
            case 79: result = {Key::End, '\0'}; break;
            case 73: result = {Key::PageUp, '\0'}; break;
            case 81: result = {Key::PageDown, '\0'}; break;
            case 83: result = {Key::Delete, '\0'}; break;
            case 59: result = {Key::F1, '\0'}; break;
            case 60: result = {Key::F2, '\0'}; break;
            default: result = {Key::Unknown, '\0'}; break;
        }
        emitKeyAudit(result, false);
        return result;
    }
#else
    RawTerminalMode rawMode;
    const int ch = readByte();
    if (ch < 0) {
        emitKeyAudit(result, false);
        return result;
    }
    if (ch == 27) {
        result = decodeEscapeSequence();
        emitKeyAudit(result, false);
        return result;
    }
#endif

    switch (ch) {
        case 13:
        case 10:
            result = {Key::Enter, '\0'};
            break;
        case 27:
            result = {Key::Escape, '\0'};
            break;
        case 8:
        case 127:
            result = {Key::Backspace, '\0'};
            break;
        case 9:
            result = {Key::Tab, '\0'};
            break;
        case 3:
            result = {Key::Escape, '\0'};
            break;
        default:
            if (std::isprint(static_cast<unsigned char>(ch))) {
                result = {Key::Character, static_cast<char>(ch)};
            } else {
                result = {Key::Unknown, '\0'};
            }
            break;
    }

    emitKeyAudit(result, false);
    return result;
}

}
