//Attention Windows only code.

#include "color.h"
#include <iostream>
#include <string>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <random>
#include <vector>
#include <conio.h>

//TODO: Consider updating color into 256bit or True Color in the future.
std::unordered_map<std::string, std::string> colorMap = {
    {"red", "\033[31m"},          //For warning/error messages
    {"green", "\033[32m"},        //For success messages
    {"yellow", "\033[33m"},       //For prompts/instructions
    {"blue", "\033[34m"},         //For informational messages
    {"magenta", "\033[35m"},      //For highlights/alerts
    {"cyan", "\033[36m"},         //For titles/headings
    {"white", "\033[37m"},        //For regular text
    {"reset", "\033[0m"},
    {"RED", "\033[31m"},          //For warning/error messages
    {"GREEN", "\033[32m"},        //For success messages
    {"YELLOW", "\033[33m"},       //For prompts/instructions
    {"BLUE", "\033[34m"},         //For informational messages
    {"MAGENTA", "\033[35m"},      //For highlights/alerts
    {"CYAN", "\033[36m"},         //For titles/headings
    {"WHITE", "\033[37m"},        //For regular text
    {"RESET", "\033[0m"}
};

void colorcout(const std::string& color, const std::string& str) {
    if (color.empty()) {
        std::cout << str;
        return;
    }
    if (color == "red" || color == "ERROR" || color == "RED") std::cout << "\a"; // Beep for red color (warning/error)
    auto it = colorMap.find(color);
    if (it != colorMap.end()) {
        std::cout << it->second << str << "\033[0m";
    } else {
        std::cout << str;
    }
}

void rollcout(const std::string& color, const std::string& str) {
    using namespace std;
    // Check if string contains non-ASCII characters
    bool hasNonAscii = false;
    for (char c : str) {
        if (static_cast<unsigned char>(c) > 127) {
            hasNonAscii = true;
            break;
        }
    }
    // If contains non-ASCII (non-English) characters, output directly
    if (hasNonAscii) {
        colorcout(color, str + "\n");
        return;
    }
    // ANSI color map
    const string resetSeq = "\033[0m";
    string colorSeq;
    bool useColor = false;
    auto it = colorMap.find(color);
    if (it != colorMap.end()) {
        colorSeq = it->second;
        useColor = true;
    }
    // Hide cursor
    cout << "\x1b[?25l" << flush;
    // Printable characters: ASCII 32 ~ 126
    auto isPrintable = [](unsigned char ch) {
        return ch >= 32 && ch <= 126;
    };
    string printable;
    for (int c = 32; c <= 126; ++c) {
        printable.push_back(static_cast<char>(c));
    }
    // Random engine (seeded with time + thread id for better randomness in multi-thread)
    static mt19937 rng(chrono::steady_clock::now().time_since_epoch().count() ^
                       hash<thread::id>{}(this_thread::get_id()));
    uniform_int_distribution<size_t> dist(0, printable.size() - 1);
    // Current displayed string: each char starts from a random printable char
    string curr(str.size(), ' ');
    vector<bool> done(str.size(), false);
    for (size_t i = 0; i < str.size(); ++i) {
        if (!isPrintable(static_cast<unsigned char>(str[i]))) {
            done[i] = true;           // non-printable, mark as done
            curr[i] = ' ';            // placeholder space to avoid outputting newlines etc.
            continue;
        }
        curr[i] = printable[dist(rng)];
    }
    // Helper: get next printable char (cyclic)
    auto nextPrintable = [&](char ch) -> char {
        auto pos = printable.find(ch);
        if (pos == string::npos) return printable[0];
        return printable[(pos + 1) % printable.size()];
    };
    // First frame: print random initial state immediately
    if (useColor) {
        cout << colorSeq << curr << resetSeq << '\r' << flush;
    } else {
        cout << curr << '\r' << flush;
    }
    // Rolling loop
    while (true) {
        bool allDone = true;
        for (size_t i = 0; i < str.size(); ++i) {
            if (done[i]) continue;
            // If not printable, mark done and set to space
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
        this_thread::sleep_for(chrono::milliseconds(20));
        if (useColor) {
            cout << colorSeq << curr << resetSeq << '\r' << flush;
        } else {
            cout << curr << '\r' << flush;
        }
        if (allDone) break;
    }
    // Final output with newline
    if (useColor) {
        cout << colorSeq << str << resetSeq << endl;
    } else {
        cout << str << endl;
    }
    // Show cursor again
    cout << "\x1b[?25h" << flush;
}

bool getYN(const std::string& prompt) {
    using namespace std;
    while (true) {
        colorcout("yellow", prompt + " (y/n): ");
        char ch = _getch();
        cout << ch << endl;  // Echo the input character and newline
        if (ch == 'y' || ch == 'Y') {
            std::cout << std::endl;
            return true;
        } else if (ch == 'n' || ch == 'N') {
            std::cout << std::endl;
            return false;
        } else {
            colorcout("red", "Invalid input. Please press 'y' or 'n'.\n");
        }
    }
}

void clear_screen() {
    std::cout << "\x1B[2J\x1B[H";
}

void set_title(const std::string& console_title) {
    std::cout << "\033]0;" << console_title << "\007";
}

void Sleep(int milsec) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milsec));
}

void pause() {
    colorcout("white", "Press Enter to continue...");
    std::cin.get();
}

std::string getHiddenInput(const std::string& prompt, char symbol) {
    std::string input;
    const bool showSymbol = (symbol != '\0');
    colorcout("white", prompt);
    while (true) {
        int ch = _getch();
        if (ch == '\r' || ch == '\n') {
            std::cout << std::endl;
            break;
        }
        if (ch == 8) { // backspace
            if (!input.empty()) {
                input.pop_back();
                if (showSymbol) {
                    std::cout << "\b \b";
                }
            }
            continue;
        }
        if (ch == 3) { // Ctrl+C
            std::cout << std::endl;
            input.clear();
            break;
        }
        input.push_back(static_cast<char>(ch));
        if (showSymbol) {
            std::cout << symbol;
        }
    }
    return input;
}

void print_icon(){
    colorcout("cyan", R"(
     _______              _           _    ___   _____
    |__   __|            | |         | |  | \ \ / /__ \
       | |_   _ _ __   __| |_ __ __ _| |  | |\ V /   ) |
       | | | | | '_ \ / _` | '__/ _` | |  | | > <   / /
       | | |_| | | | | (_| | | | (_| | |__| |/ . \ / /_
       |_|\__,_|_| |_|\__,_|_|  \__,_|\____//_/ \_\____|
                                                        
)");
    for(int i=0; i<=60; i++) colorcout("cyan","=");
    std::cout << std::endl;
}

//How to use this function:

//Add thses lines before calling the function:
//std::vector<std::string> commandHistory;
//int historyIndex = -1;
//const int MAX_HISTORY = 100;

//Add thses in the main loop where you want to read input with history:
/*
    std::string line = readLineWithHistory(commandHistory, historyIndex);
    if (line.empty()) continue;
    if (commandHistory.empty() || commandHistory.back() != line) {
        if (static_cast<int>(commandHistory.size()) >= MAX_HISTORY) {
            commandHistory.erase(commandHistory.begin());
        }
        commandHistory.push_back(line);
    }
    historyIndex = -1;
*/
std::string readLineWithHistory(std::vector<std::string>& history, int& historyIndex) {
    std::string current;
    std::string saved;  // Saves current input when navigating history
    int cursorPos = 0;  // Track cursor position

    while (true) {
        int ch = _getch();
        if (ch == '\r' || ch == '\n') {
            // Enter: submit command
            std::cout << std::endl;
            return current;
        } 
        else if (ch == 8) {
            // Backspace
            if (cursorPos > 0) {
                current.erase(cursorPos - 1, 1);
                cursorPos--;
                std::cout << "\b"; // Move back
                // Re-print the rest of the line
                for (size_t i = cursorPos; i < current.length(); ++i) {
                    std::cout << current[i];
                }
                std::cout << " "; // Erase last char
                // Move cursor back to correct position
                for (size_t i = cursorPos; i < current.length() + 1; ++i) {
                    std::cout << "\b";
                }
            }
        }
        else if (ch == 0 || ch == 224) {
            // Extended key (arrow keys, etc.)
            int ext = _getch();
            
            if (ext == 72) {
                // Up arrow: go to previous command in history
                if (!history.empty()) {
                    // Save current input when first pressing up
                    if (historyIndex == -1) {
                        saved = current;
                        historyIndex = static_cast<int>(history.size()) - 1;
                    } else if (historyIndex > 0) {
                        historyIndex--;
                    }       
                    
                    // Move cursor to start
                    while (cursorPos > 0) {
                        std::cout << "\b";
                        cursorPos--;
                    }
                    // Clear line
                    for (size_t i = 0; i < current.length(); ++i) std::cout << " ";
                    for (size_t i = 0; i < current.length(); ++i) std::cout << "\b";

                    // Display history entry
                    current = history[historyIndex];
                    std::cout << current;
                    cursorPos = static_cast<int>(current.length());
                }
            }
            else if (ext == 80) {
                // Down arrow: go to next command in history
                if (historyIndex != -1) {
                    // Move cursor to start
                    while (cursorPos > 0) {
                        std::cout << "\b";
                        cursorPos--;
                    }
                    // Clear line
                    for (size_t i = 0; i < current.length(); ++i) std::cout << " ";
                    for (size_t i = 0; i < current.length(); ++i) std::cout << "\b";

                    if (historyIndex < static_cast<int>(history.size()) - 1) {
                        historyIndex++;
                        current = history[historyIndex];
                    } else {
                        // Reached end of history, restore saved input
                        historyIndex = -1;
                        current = saved;
                    }
                    std::cout << current;
                    cursorPos = static_cast<int>(current.length());
                }
            }
            else if (ext == 75) {
                // Left arrow
                if (cursorPos > 0) {
                    std::cout << "\b";
                    cursorPos--;
                }
            }
            else if (ext == 77) {
                // Right arrow
                if (cursorPos < static_cast<int>(current.length())) {
                    std::cout << current[cursorPos];
                    cursorPos++;
                }
            }
        }
        else if (isprint(ch)) {
            // Printable character
            current.insert(cursorPos, 1, static_cast<char>(ch));
            // Print the rest of the line
            for (size_t i = cursorPos; i < current.length(); ++i) {
                std::cout << current[i];
            }
            cursorPos++;
            // Move cursor back to correct position
            for (size_t i = cursorPos; i < current.length(); ++i) {
                std::cout << "\b";
            }
        }
    }
}