#include "TundraTUI/color.hpp"

#include "TundraTUI/input.hpp"
#include "TundraTUI/screen.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace tundra_tui {
namespace {

const std::unordered_map<std::string, std::string> colorMap = {
    {"red", "\033[1;38;5;203m"},
    {"green", "\033[1;38;5;119m"},
    {"yellow", "\033[1;38;5;220m"},
    {"blue", "\033[38;5;39m"},
    {"magenta", "\033[1;38;5;213m"},
    {"cyan", "\033[1;38;5;51m"},
    {"white", "\033[38;5;254m"},
    {"grey", "\033[38;5;245m"},
    {"reset", "\033[0m"},
    {"RED", "\033[1;38;5;203m"},
    {"GREEN", "\033[1;38;5;119m"},
    {"YELLOW", "\033[1;38;5;220m"},
    {"BLUE", "\033[38;5;39m"},
    {"MAGENTA", "\033[1;38;5;213m"},
    {"CYAN", "\033[1;38;5;51m"},
    {"WHITE", "\033[38;5;254m"},
    {"GREY", "\033[38;5;245m"},
    {"ERROR", "\033[1;38;5;203m"},
    {"RESET", "\033[0m"}
};

int readChar() {
#ifdef _WIN32
    return _getch();
#else
    return std::cin.get();
#endif
}

#ifndef _WIN32
class HiddenInputMode {
public:
    HiddenInputMode() {
        active = tcgetattr(STDIN_FILENO, &original) == 0;
        if (!active) {
            return;
        }

        termios raw = original;
        raw.c_lflag &= static_cast<unsigned int>(~(ECHO | ICANON | ISIG));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            active = false;
        }
    }

    ~HiddenInputMode() {
        if (active) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
        }
    }

    HiddenInputMode(const HiddenInputMode&) = delete;
    HiddenInputMode& operator=(const HiddenInputMode&) = delete;

private:
    termios original{};
    bool active = false;
};
#endif

}

void colorcout(const std::string& color, const std::string& str) {
    if (color.empty()) {
        std::cout << str;
        return;
    }
    auto it = colorMap.find(color);
    if (it != colorMap.end()) {
        std::cout << it->second << str << "\033[0m";
    } else {
        std::cout << str;
    }
}

bool hasConsoleColor(const std::string& color) {
    return colorMap.find(color) != colorMap.end();
}

std::vector<std::string> getDisplayTestColorNames() {
    const std::vector<std::string> preferredOrder = {
        "green",
        "red",
        "blue",
        "yellow",
        "cyan",
        "magenta",
        "white",
        "grey"
    };

    std::vector<std::string> colorNames;
    for (const auto& color : preferredOrder) {
        if (hasConsoleColor(color)) {
            colorNames.push_back(color);
        }
    }

    std::vector<std::string> extraNames;
    for (const auto& entry : colorMap) {
        const std::string& colorName = entry.first;
        if (colorName == "reset" || colorName == "RESET" || colorName == "ERROR") {
            continue;
        }
        if (!std::all_of(colorName.begin(), colorName.end(), [](char ch) {
            return ch >= 'a' && ch <= 'z';
        })) {
            continue;
        }
        if (std::find(colorNames.begin(), colorNames.end(), colorName) == colorNames.end()) {
            extraNames.push_back(colorName);
        }
    }
    std::sort(extraNames.begin(), extraNames.end());
    colorNames.insert(colorNames.end(), extraNames.begin(), extraNames.end());
    return colorNames;
}

void rollcout(const std::string& color, const std::string& str) {
    bool hasNonAscii = false;
    for (char c : str) {
        if (static_cast<unsigned char>(c) > 127) {
            hasNonAscii = true;
            break;
        }
    }
    if (hasNonAscii) {
        colorcout(color, str + "\n");
        return;
    }

    const std::string resetSeq = "\033[0m";
    std::string colorSeq;
    bool useColor = false;
    auto it = colorMap.find(color);
    if (it != colorMap.end()) {
        colorSeq = it->second;
        useColor = true;
    }

    std::cout << "\x1b[?25l" << std::flush;

    auto isPrintable = [](unsigned char ch) {
        return ch >= 32 && ch <= 126;
    };

    std::string printable;
    for (int c = 32; c <= 126; ++c) {
        printable.push_back(static_cast<char>(c));
    }

    static std::mt19937 rng(
        static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
        static_cast<unsigned int>(std::hash<std::thread::id>{}(std::this_thread::get_id()))
    );
    std::uniform_int_distribution<size_t> dist(0, printable.size() - 1);

    std::string curr(str.size(), ' ');
    std::vector<bool> done(str.size(), false);
    for (size_t i = 0; i < str.size(); ++i) {
        if (!isPrintable(static_cast<unsigned char>(str[i]))) {
            done[i] = true;
            curr[i] = ' ';
            continue;
        }
        curr[i] = printable[dist(rng)];
    }

    auto nextPrintable = [&](char ch) -> char {
        auto pos = printable.find(ch);
        if (pos == std::string::npos) {
            return printable[0];
        }
        return printable[(pos + 1) % printable.size()];
    };

    if (useColor) {
        std::cout << colorSeq << curr << resetSeq << '\r' << std::flush;
    } else {
        std::cout << curr << '\r' << std::flush;
    }

    while (true) {
        bool allDone = true;
        for (size_t i = 0; i < str.size(); ++i) {
            if (done[i]) {
                continue;
            }
            if (!isPrintable(static_cast<unsigned char>(str[i]))) {
                done[i] = true;
                curr[i] = ' ';
                continue;
            }
            allDone = false;
            curr[i] = nextPrintable(curr[i]);

            if (curr[i] == str[i]) {
                done[i] = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (useColor) {
            std::cout << colorSeq << curr << resetSeq << '\r' << std::flush;
        } else {
            std::cout << curr << '\r' << std::flush;
        }
        if (allDone) {
            break;
        }
    }

    if (useColor) {
        std::cout << colorSeq << str << resetSeq << std::endl;
    } else {
        std::cout << str << std::endl;
    }
    std::cout << "\x1b[?25h" << std::flush;
}

bool getYN(const std::string& prompt) {
    while (true) {
        colorcout("yellow", prompt + " (y/n): ");
        const int ch = readChar();
        std::cout << static_cast<char>(ch) << std::endl;
        if (ch == 'y' || ch == 'Y') {
            std::cout << std::endl;
            return true;
        }
        if (ch == 'n' || ch == 'N') {
            std::cout << std::endl;
            return false;
        }
        colorcout("red", "Invalid input. Please press 'y' or 'n'.\n");
    }
}

void clear_screen() {
    clearConsoleScreen();
}

void set_title(const std::string& console_title) {
    std::cout << "\033]0;" << console_title << "\007";
}

void sleepFor(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void Sleep(int milliseconds) {
    sleepFor(milliseconds);
}

void pause() {
    colorcout("white", "Press Enter to continue...");
    std::cin.get();
}

std::string getHiddenInput(const std::string& prompt, char symbol) {
    std::string input;
    const bool showSymbol = (symbol != '\0');
    colorcout("white", prompt);
#ifndef _WIN32
    HiddenInputMode hiddenInputMode;
#endif
    while (true) {
        const int ch = readChar();
        if (ch == '\r' || ch == '\n') {
            emitKeyAudit({Key::Enter, '\0'}, true);
            std::cout << std::endl;
            break;
        }
        if (ch == 8 || ch == 127) {
            emitKeyAudit({Key::Backspace, '\0'}, true);
            if (!input.empty()) {
                input.pop_back();
                if (showSymbol) {
                    std::cout << "\b \b";
                }
            }
            continue;
        }
        if (ch == 3) {
            emitKeyAudit({Key::Escape, '\0'}, true);
            std::cout << std::endl;
            input.clear();
            break;
        }
        emitKeyAudit({Key::Character, static_cast<char>(ch)}, true);
        input.push_back(static_cast<char>(ch));
        if (showSymbol) {
            std::cout << symbol;
        }
    }
    return input;
}

void print_icon() {
    colorcout("cyan", R"(
     _______              _           _    ___   _____
    |__   __|            | |         | |  | \ \ / /__ \
       | |_   _ _ __   __| |_ __ __ _| |  | |\ V /   ) |
       | | | | | '_ \ / _` | '__/ _` | |  | | > <   / /
       | | |_| | | | | (_| | | | (_| | |__| |/ . \ / /_
       |_|\__,_|_| |_|\__,_|_|  \__,_|\____//_/ \_\____|

)");
    for (int i = 0; i <= 60; i++) {
        colorcout("cyan", "=");
    }
    std::cout << std::endl;
}

std::string readLineWithHistory(std::vector<std::string>& history, int& historyIndex) {
    std::string current;
    std::string saved;
    int cursorPos = 0;

    while (true) {
        const int ch = readChar();
        if (ch == '\r' || ch == '\n') {
            emitKeyAudit({Key::Enter, '\0'}, false);
            std::cout << std::endl;
            return current;
        }
        if (ch == 8 || ch == 127) {
            emitKeyAudit({Key::Backspace, '\0'}, false);
            if (cursorPos > 0) {
                current.erase(cursorPos - 1, 1);
                cursorPos--;
                std::cout << "\b";
                for (size_t i = cursorPos; i < current.length(); ++i) {
                    std::cout << current[i];
                }
                std::cout << " ";
                for (size_t i = cursorPos; i < current.length() + 1; ++i) {
                    std::cout << "\b";
                }
            }
        }
#ifdef _WIN32
        else if (ch == 0 || ch == 224) {
            const int ext = readChar();
            KeyPress keyPress{Key::Unknown, '\0'};
            if (ext == 72) {
                keyPress.key = Key::Up;
            } else if (ext == 80) {
                keyPress.key = Key::Down;
            } else if (ext == 75) {
                keyPress.key = Key::Left;
            } else if (ext == 77) {
                keyPress.key = Key::Right;
            }
            emitKeyAudit(keyPress, false);
            if (ext == 72) {
                if (!history.empty()) {
                    if (historyIndex == -1) {
                        saved = current;
                        historyIndex = static_cast<int>(history.size()) - 1;
                    } else if (historyIndex > 0) {
                        historyIndex--;
                    }

                    while (cursorPos > 0) {
                        std::cout << "\b";
                        cursorPos--;
                    }
                    for (size_t i = 0; i < current.length(); ++i) {
                        std::cout << " ";
                    }
                    for (size_t i = 0; i < current.length(); ++i) {
                        std::cout << "\b";
                    }

                    current = history[historyIndex];
                    std::cout << current;
                    cursorPos = static_cast<int>(current.length());
                }
            } else if (ext == 80) {
                if (historyIndex != -1) {
                    while (cursorPos > 0) {
                        std::cout << "\b";
                        cursorPos--;
                    }
                    for (size_t i = 0; i < current.length(); ++i) {
                        std::cout << " ";
                    }
                    for (size_t i = 0; i < current.length(); ++i) {
                        std::cout << "\b";
                    }

                    if (historyIndex < static_cast<int>(history.size()) - 1) {
                        historyIndex++;
                        current = history[historyIndex];
                    } else {
                        historyIndex = -1;
                        current = saved;
                    }
                    std::cout << current;
                    cursorPos = static_cast<int>(current.length());
                }
            } else if (ext == 75) {
                if (cursorPos > 0) {
                    std::cout << "\b";
                    cursorPos--;
                }
            } else if (ext == 77) {
                if (cursorPos < static_cast<int>(current.length())) {
                    std::cout << current[cursorPos];
                    cursorPos++;
                }
            }
        }
#endif
        else if (std::isprint(static_cast<unsigned char>(ch))) {
            emitKeyAudit({Key::Character, static_cast<char>(ch)}, false);
            current.insert(cursorPos, 1, static_cast<char>(ch));
            for (size_t i = cursorPos; i < current.length(); ++i) {
                std::cout << current[i];
            }
            cursorPos++;
            for (size_t i = cursorPos; i < current.length(); ++i) {
                std::cout << "\b";
            }
        }
    }
}

}
