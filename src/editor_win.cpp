#include <windows.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <direct.h>
#include <cerrno>
#include "editor_win.h"

// Editor version info
#define EDITOR_VERSION "1.0.0"
#define EDITOR_NAME "Tundra Editor"

// Shortcut key definitions (nano-style)
#define KEY_CTRL_X 24   // Ctrl+X Exit
#define KEY_CTRL_O 15   // Ctrl+O Save
#define KEY_CTRL_N 14   // Ctrl+N New
#define KEY_CTRL_R 18   // Ctrl+R Open
#define KEY_CTRL_W 23   // Ctrl+W Search
#define KEY_CTRL_K 11   // Ctrl+K Cut line
#define KEY_CTRL_U 21   // Ctrl+U Paste
#define KEY_CTRL_G 7    // Ctrl+G Help

// Use high values for special keys to avoid collision with ASCII/control chars
#define SPECIAL_KEY_BASE  0x100
#define KEY_UP_SPECIAL      (SPECIAL_KEY_BASE + 1)
#define KEY_DOWN_SPECIAL    (SPECIAL_KEY_BASE + 2)
#define KEY_LEFT_SPECIAL    (SPECIAL_KEY_BASE + 3)
#define KEY_RIGHT_SPECIAL   (SPECIAL_KEY_BASE + 4)
#define KEY_HOME_SPECIAL    (SPECIAL_KEY_BASE + 5)
#define KEY_END_SPECIAL     (SPECIAL_KEY_BASE + 6)
#define KEY_PPAGE_SPECIAL   (SPECIAL_KEY_BASE + 7)
#define KEY_NPAGE_SPECIAL   (SPECIAL_KEY_BASE + 8)
#define KEY_DC_SPECIAL      (SPECIAL_KEY_BASE + 9)
#define KEY_ENTER_SPECIAL   (SPECIAL_KEY_BASE + 10)
#define KEY_BACKSPACE_SPECIAL (SPECIAL_KEY_BASE + 11)

// Color pair indices (Windows console attributes)
#define COLOR_PAIR_TITLE    1   // White on Blue
#define COLOR_PAIR_STATUS   2   // Black on White
#define COLOR_PAIR_HIGHLIGHT 3  // Yellow on Black
#define COLOR_PAIR_SUCCESS  4   // Green on Black
#define COLOR_PAIR_ERROR    5   // Red on Black
// Syntax highlight color pairs
#define COLOR_PAIR_KEYWORD  6   // Blue - keywords
#define COLOR_PAIR_STRING   7   // Green - strings/chars
#define COLOR_PAIR_COMMENT  8   // Cyan - comments
#define COLOR_PAIR_NUMBER   9   // Magenta - numbers
#define COLOR_PAIR_PREPROC  10  // Yellow - preprocessor

// Editor mode enum
enum EditorMode {
    MODE_NORMAL,      // Normal editing mode
    MODE_COMMAND,     // Command input mode
    MODE_PROMPT,      // Prompt/confirm mode
    MODE_SEARCH       // Search mode
};

// Prompt type enum
enum PromptType {
    PROMPT_NONE,
    PROMPT_SAVE_AS,       // Save as
    PROMPT_OPEN_FILE,     // Open file
    PROMPT_NEW_FILE,      // New file
    PROMPT_OVERWRITE,     // Overwrite confirmation
    PROMPT_EXIT_UNSAVED   // Unsaved exit confirmation
};

// ---- Path utilities ----

static std::string unifySlashes(const std::string& path) {
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

static bool isValidPath(const std::string& path) {
    if (path.empty()) return false;
    const std::string illegal = "<>\"*|?";
    for (char c : illegal) {
        if (path.find(c) != std::string::npos) return false;
    }
    std::string trimmed = path;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    size_t last = trimmed.find_last_not_of(" \t");
    if (last == std::string::npos) return false;
    trimmed.erase(last + 1);
    if (trimmed.empty() || trimmed == "/" || trimmed == "\\") return false;
    return true;
}

static bool ensureDirectoryExists(const std::string& dirPath) {
    std::string path = unifySlashes(dirPath);
    while (!path.empty() && path.back() == '/') path.pop_back();
    if (path.empty()) return true;

    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return (st.st_mode & S_IFDIR) != 0;
    }

    size_t pos = path.rfind('/');
    if (pos != std::string::npos) {
        std::string parent = path.substr(0, pos);
        if (!ensureDirectoryExists(parent)) return false;
    }

    return (_mkdir(path.c_str()) == 0 || errno == EEXIST);
}

static std::string normalizePath(const std::string& input) {
    std::string path = unifySlashes(input);
    while (!path.empty() && path.front() == '/') path = path.substr(1);
    if (path.size() >= 2 && path.substr(0, 2) == "~/") {
        return path;
    }
    return "~/" + path;
}

// ---- End of path utilities ----

/**
 * WindowsConsole class - wraps Windows Console API
 */
class WindowsConsole {
private:
    HANDLE hStdout;
    HANDLE hStdin;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    WORD originalAttributes;
    WORD currentAttributes;
    bool colorsEnabled;
    DWORD originalOutMode;
    DWORD originalInMode;
    COORD savedCursorPos;
    SMALL_RECT savedWindow;
    SHORT savedWidth;
    SHORT savedHeight;
    std::vector<CHAR_INFO> savedBuffer;

public:
    WindowsConsole() : colorsEnabled(false) {
        hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        hStdin = GetStdHandle(STD_INPUT_HANDLE);
        GetConsoleScreenBufferInfo(hStdout, &csbi);
        originalAttributes = csbi.wAttributes;
        currentAttributes = originalAttributes;

        // Save current screen content for restoration on exit
        savedCursorPos = csbi.dwCursorPosition;
        savedWindow = csbi.srWindow;
        savedWidth  = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
        savedHeight = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
        savedBuffer.resize(savedWidth * savedHeight);
        COORD bufSize  = {savedWidth, savedHeight};
        COORD bufCoord = {0, 0};
        SMALL_RECT readRegion = savedWindow;
        ReadConsoleOutput(hStdout, savedBuffer.data(), bufSize, bufCoord, &readRegion);

        // Enable virtual terminal processing for better control
        DWORD mode;
        GetConsoleMode(hStdout, &mode);
        originalOutMode = mode;
        SetConsoleMode(hStdout, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

        // Set input mode
        GetConsoleMode(hStdin, &mode);
        originalInMode = mode;
        SetConsoleMode(hStdin, (mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT)) | ENABLE_WINDOW_INPUT);

        colorsEnabled = true;
    }

    ~WindowsConsole() {
        SetConsoleTextAttribute(hStdout, originalAttributes);
        CONSOLE_CURSOR_INFO cci;
        cci.dwSize = 100;
        cci.bVisible = TRUE;
        SetConsoleCursorInfo(hStdout, &cci);
        SetConsoleMode(hStdout, originalOutMode);
        SetConsoleMode(hStdin, originalInMode);

        // Restore saved screen content
        if (!savedBuffer.empty()) {
            COORD bufSize  = {savedWidth, savedHeight};
            COORD bufCoord = {0, 0};
            SMALL_RECT writeRegion = savedWindow;
            WriteConsoleOutput(hStdout, savedBuffer.data(), bufSize, bufCoord, &writeRegion);
            SetConsoleCursorPosition(hStdout, savedCursorPos);
        }
    }

    void clear() {
        COORD coord = {0, 0};
        DWORD written;
        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(hStdout, &info);
        DWORD consoleSize = info.dwSize.X * info.dwSize.Y;
        FillConsoleOutputCharacter(hStdout, ' ', consoleSize, coord, &written);
        FillConsoleOutputAttribute(hStdout, originalAttributes, consoleSize, coord, &written);
        SetConsoleCursorPosition(hStdout, coord);
    }

    void refresh() {
        // No-op for Windows, changes are immediate
    }

    void getmaxyx(int& height, int& width) {
        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(hStdout, &info);
        width = info.srWindow.Right - info.srWindow.Left + 1;
        height = info.srWindow.Bottom - info.srWindow.Top + 1;
    }

    void move(int y, int x) {
        COORD coord = {(SHORT)x, (SHORT)y};
        SetConsoleCursorPosition(hStdout, coord);
    }

    void printw(const char* fmt, ...) {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        DWORD written;
        WriteConsoleA(hStdout, buf, (DWORD)strlen(buf), &written, NULL);
    }

    void mvprintw(int y, int x, const char* fmt, ...) {
        move(y, x);
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        DWORD written;
        WriteConsoleA(hStdout, buf, (DWORD)strlen(buf), &written, NULL);
    }

    void clrtoeol() {
        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(hStdout, &info);
        COORD coord = info.dwCursorPosition;
        DWORD written;
        DWORD length = info.srWindow.Right - coord.X + 1;
        if (length <= 0) return;
        FillConsoleOutputCharacter(hStdout, ' ', length, coord, &written);
        FillConsoleOutputAttribute(hStdout, currentAttributes, length, coord, &written);
        SetConsoleCursorPosition(hStdout, coord);
    }

    void clearLine(int y) {
        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(hStdout, &info);
        COORD coord = {0, (SHORT)y};
        DWORD written;
        DWORD length = info.srWindow.Right - info.srWindow.Left + 1;
        FillConsoleOutputCharacter(hStdout, ' ', length, coord, &written);
        FillConsoleOutputAttribute(hStdout, originalAttributes, length, coord, &written);
    }

    void curs_set(int visibility) {
        CONSOLE_CURSOR_INFO cci;
        GetConsoleCursorInfo(hStdout, &cci);
        cci.bVisible = (visibility != 0);
        SetConsoleCursorInfo(hStdout, &cci);
    }

    bool has_colors() {
        return colorsEnabled;
    }

    void attron(WORD attributes) {
        currentAttributes = attributes;
        SetConsoleTextAttribute(hStdout, attributes);
    }

    void attroff() {
        currentAttributes = originalAttributes;
        SetConsoleTextAttribute(hStdout, originalAttributes);
    }

    WORD getColorPair(int pair) {
        switch (pair) {
            case COLOR_PAIR_TITLE:      return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY | BACKGROUND_BLUE; // White on Blue
            case COLOR_PAIR_STATUS:     return 0 | BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE; // Black on White
            case COLOR_PAIR_HIGHLIGHT:  return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; // Yellow on Black
            case COLOR_PAIR_SUCCESS:    return FOREGROUND_GREEN | FOREGROUND_INTENSITY; // Green on Black
            case COLOR_PAIR_ERROR:      return FOREGROUND_RED | FOREGROUND_INTENSITY; // Red on Black
            case COLOR_PAIR_KEYWORD:    return FOREGROUND_BLUE | FOREGROUND_INTENSITY; // Blue - keywords
            case COLOR_PAIR_STRING:     return FOREGROUND_GREEN | FOREGROUND_INTENSITY; // Green - strings/chars
            case COLOR_PAIR_COMMENT:    return FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY; // Cyan
            case COLOR_PAIR_NUMBER:     return FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;   // Magenta
            case COLOR_PAIR_PREPROC:    return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;  // Yellow
            default:                    return originalAttributes;
        }
    }

    int getch() {
        INPUT_RECORD ir;
        DWORD read;

        while (true) {
            ReadConsoleInput(hStdin, &ir, 1, &read);

            if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
                return -2;
            }

            if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
                KEY_EVENT_RECORD& key = ir.Event.KeyEvent;
                char ch = key.uChar.AsciiChar;

                if (ch >= 1 && ch <= 26 && key.wVirtualKeyCode != VK_RETURN &&
                    key.wVirtualKeyCode != VK_BACK && key.wVirtualKeyCode != VK_TAB &&
                    key.wVirtualKeyCode != VK_ESCAPE) {
                    return ch;
                }

                switch (key.wVirtualKeyCode) {
                    case VK_UP:     return KEY_UP_SPECIAL;
                    case VK_DOWN:   return KEY_DOWN_SPECIAL;
                    case VK_LEFT:   return KEY_LEFT_SPECIAL;
                    case VK_RIGHT:  return KEY_RIGHT_SPECIAL;
                    case VK_HOME:   return KEY_HOME_SPECIAL;
                    case VK_END:    return KEY_END_SPECIAL;
                    case VK_PRIOR:  return KEY_PPAGE_SPECIAL;
                    case VK_NEXT:   return KEY_NPAGE_SPECIAL;
                    case VK_DELETE: return KEY_DC_SPECIAL;
                    case VK_RETURN: return KEY_ENTER_SPECIAL;
                    case VK_BACK:   return KEY_BACKSPACE_SPECIAL;
                    case VK_TAB:    return '\t';
                    case VK_ESCAPE: return 27;
                }

                if (ch >= 32 && ch < 127) {
                    return ch;
                }
            }
        }
    }

    HANDLE getStdout() { return hStdout; }
    HANDLE getStdin() { return hStdin; }
};

// Global console instance
WindowsConsole* g_console = nullptr;

/**
 * TextBuffer class - manages text content and cursor position
 */
class TextBuffer {
public:
    std::vector<std::string> lines;
    int cursorX = 0;
    int cursorY = 0;
    int scrollOffset = 0;
    int horizontalScroll = 0;
    bool modified = false;
    std::string filename = "";

    TextBuffer() {
        lines.push_back("");
    }

    std::string& currentLine() {
        return lines[cursorY];
    }

    void insertChar(char c) {
        if (cursorY >= (int)lines.size()) {
            lines.push_back("");
        }
        lines[cursorY].insert(cursorX, 1, c);
        cursorX++;
        modified = true;
    }

    void backspace() {
        if (cursorX > 0) {
            lines[cursorY].erase(cursorX - 1, 1);
            cursorX--;
            modified = true;
        } else if (cursorY > 0) {
            int prevLen = (int)lines[cursorY - 1].length();
            lines[cursorY - 1] += lines[cursorY];
            lines.erase(lines.begin() + cursorY);
            cursorY--;
            cursorX = prevLen;
            modified = true;
        }
    }

    void deleteChar() {
        if (cursorX < (int)lines[cursorY].length()) {
            lines[cursorY].erase(cursorX, 1);
            modified = true;
        } else if (cursorY < (int)lines.size() - 1) {
            lines[cursorY] += lines[cursorY + 1];
            lines.erase(lines.begin() + cursorY + 1);
            modified = true;
        }
    }

    void insertNewline() {
        if (cursorY >= (int)lines.size()) {
            lines.push_back("");
        } else {
            std::string newLine = lines[cursorY].substr(cursorX);
            lines[cursorY] = lines[cursorY].substr(0, cursorX);
            lines.insert(lines.begin() + cursorY + 1, newLine);
        }
        cursorY++;
        cursorX = 0;
        modified = true;
    }

    void moveCursor(int dx, int dy) {
        cursorX += dx;
        cursorY += dy;

        if (cursorY < 0) cursorY = 0;
        if (cursorY >= (int)lines.size()) cursorY = (int)lines.size() - 1;

        int lineLen = (int)lines[cursorY].length();
        if (cursorX < 0) cursorX = 0;
        if (cursorX > lineLen) cursorX = lineLen;
    }

    void moveToLineStart() { cursorX = 0; }

    void moveToLineEnd() { cursorX = (int)lines[cursorY].length(); }

    void pageUp(int pageSize) {
        cursorY -= pageSize;
        if (cursorY < 0) cursorY = 0;
        adjustCursorX();
    }

    void pageDown(int pageSize) {
        cursorY += pageSize;
        if (cursorY >= (int)lines.size()) cursorY = (int)lines.size() - 1;
        adjustCursorX();
    }

    void adjustCursorX() {
        int lineLen = (int)lines[cursorY].length();
        if (cursorX > lineLen) cursorX = lineLen;
    }

    void clear() {
        lines.clear();
        lines.push_back("");
        cursorX = 0;
        cursorY = 0;
        scrollOffset = 0;
        horizontalScroll = 0;
        modified = false;
        filename = "";
    }

    bool loadFromFile(const std::string& fname) {
        std::ifstream file(fname);
        if (!file.is_open()) return false;

        lines.clear();
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }

        if (lines.empty()) lines.push_back("");

        cursorX = 0;
        cursorY = 0;
        scrollOffset = 0;
        horizontalScroll = 0;
        modified = false;
        file.close();
        return true;
    }

    bool saveToFile(const std::string& fname) {
        std::ofstream file(fname);
        if (!file.is_open()) return false;

        for (size_t i = 0; i < lines.size(); i++) {
            file << lines[i];
            if (i < lines.size() - 1) file << "\n";
        }

        modified = false;
        file.close();
        return true;
    }

    static bool fileExists(const std::string& fname) {
        struct stat buffer;
        return (stat(fname.c_str(), &buffer) == 0);
    }

    int getLineCount() const { return (int)lines.size(); }

    int getLineLength(int line) const {
        if (line >= 0 && line < (int)lines.size())
            return (int)lines[line].length();
        return 0;
    }
};

/**
 * TerminalEditor class - handles UI rendering and user input
 */
class TerminalEditor {
private:
    TextBuffer buffer;
    EditorMode mode = MODE_NORMAL;
    PromptType promptType = PROMPT_NONE;
    std::string promptInput;
    std::string statusMessage;
    int screenWidth = 0;
    int screenHeight = 0;
    int editAreaHeight = 0;
    bool running = true;
    std::string clipboard;

    std::string searchQuery;
    int searchX = -1;
    int searchY = -1;

    std::string pendingFilename;
    PromptType previousPromptType = PROMPT_NONE;
    int ctrlXPressCount = 0;

    std::chrono::steady_clock::time_point startTime;
    std::string actualFilePath;

    // ---- Syntax highlighting ----

    static const std::vector<std::string>& getCKeywords() {
        static const std::vector<std::string> keywords = {
            "auto","break","case","char","const","continue","default","do",
            "double","else","enum","extern","float","for","goto","if",
            "inline","int","long","register","restrict","return","short",
            "signed","sizeof","static","struct","switch","typedef","union",
            "unsigned","void","volatile","while","_Bool","_Complex",
            "_Imaginary","bool","true","false","nullptr","NULL",
            // C++ extras (common in .cpp)
            "class","public","private","protected","new","delete","this",
            "namespace","using","template","typename","virtual","override",
            "final","explicit","operator","friend","throw","try","catch",
            "noexcept","constexpr","auto","decltype","static_assert",
            "nullptr","std","string","vector","map","set"
        };
        return keywords;
    }

    enum TokenType {
        TOK_NORMAL,
        TOK_KEYWORD,
        TOK_STRING,
        TOK_CHAR_LIT,
        TOK_COMMENT_LINE,
        TOK_COMMENT_BLOCK,
        TOK_NUMBER,
        TOK_PREPROC
    };

    struct Token {
        TokenType type;
        std::string text;
    };

    // Tokenize a single line (simplified, handles line comments & strings)
    // blockComment: in/out - whether we're inside a block comment entering this line
    std::vector<Token> tokenizeLine(const std::string& line, bool& inBlockComment) {
        std::vector<Token> tokens;
        int n = (int)line.size();
        int i = 0;

        // Preprocessor directive
        if (!inBlockComment) {
            int start = 0;
            while (start < n && (line[start] == ' ' || line[start] == '\t')) start++;
            if (start < n && line[start] == '#') {
                // Whole line is a preprocessor directive (simplification)
                tokens.push_back({TOK_PREPROC, line});
                return tokens;
            }
        }

        while (i < n) {
            // Block comment continuation
            if (inBlockComment) {
                int start = i;
                while (i < n) {
                    if (i + 1 < n && line[i] == '*' && line[i+1] == '/') {
                        i += 2;
                        inBlockComment = false;
                        break;
                    }
                    i++;
                }
                tokens.push_back({TOK_COMMENT_BLOCK, line.substr(start, i - start)});
                continue;
            }

            // Line comment
            if (i + 1 < n && line[i] == '/' && line[i+1] == '/') {
                tokens.push_back({TOK_COMMENT_LINE, line.substr(i)});
                break;
            }

            // Block comment start
            if (i + 1 < n && line[i] == '/' && line[i+1] == '*') {
                int start = i;
                i += 2;
                inBlockComment = true;
                while (i < n) {
                    if (i + 1 < n && line[i] == '*' && line[i+1] == '/') {
                        i += 2;
                        inBlockComment = false;
                        break;
                    }
                    i++;
                }
                tokens.push_back({TOK_COMMENT_BLOCK, line.substr(start, i - start)});
                continue;
            }

            // String literal
            if (line[i] == '"') {
                int start = i++;
                while (i < n) {
                    if (line[i] == '\\') { i += 2; continue; }
                    if (line[i] == '"') { i++; break; }
                    i++;
                }
                tokens.push_back({TOK_STRING, line.substr(start, i - start)});
                continue;
            }

            // Char literal
            if (line[i] == '\'') {
                int start = i++;
                while (i < n) {
                    if (line[i] == '\\') { i += 2; continue; }
                    if (line[i] == '\'') { i++; break; }
                    i++;
                }
                tokens.push_back({TOK_CHAR_LIT, line.substr(start, i - start)});
                continue;
            }

            // Number
            if (isdigit((unsigned char)line[i]) ||
                (line[i] == '.' && i + 1 < n && isdigit((unsigned char)line[i+1]))) {
                int start = i;
                // Hex
                if (line[i] == '0' && i + 1 < n && (line[i+1] == 'x' || line[i+1] == 'X')) {
                    i += 2;
                    while (i < n && isxdigit((unsigned char)line[i])) i++;
                } else {
                    while (i < n && (isdigit((unsigned char)line[i]) || line[i] == '.' ||
                           line[i] == 'e' || line[i] == 'E' ||
                           line[i] == 'f' || line[i] == 'F' ||
                           line[i] == 'u' || line[i] == 'U' ||
                           line[i] == 'l' || line[i] == 'L')) i++;
                }
                tokens.push_back({TOK_NUMBER, line.substr(start, i - start)});
                continue;
            }

            // Identifier or keyword
            if (isalpha((unsigned char)line[i]) || line[i] == '_') {
                int start = i;
                while (i < n && (isalnum((unsigned char)line[i]) || line[i] == '_')) i++;
                std::string word = line.substr(start, i - start);
                const auto& kws = getCKeywords();
                bool isKw = std::find(kws.begin(), kws.end(), word) != kws.end();
                tokens.push_back({isKw ? TOK_KEYWORD : TOK_NORMAL, word});
                continue;
            }

            // Normal character
            // Merge consecutive normal chars
            if (!tokens.empty() && tokens.back().type == TOK_NORMAL) {
                tokens.back().text += line[i];
            } else {
                tokens.push_back({TOK_NORMAL, std::string(1, line[i])});
            }
            i++;
        }
        return tokens;
    }

    WORD tokenColor(TokenType t) {
        switch (t) {
            case TOK_KEYWORD:       return g_console->getColorPair(COLOR_PAIR_KEYWORD);
            case TOK_STRING:
            case TOK_CHAR_LIT:      return g_console->getColorPair(COLOR_PAIR_STRING);
            case TOK_COMMENT_LINE:
            case TOK_COMMENT_BLOCK: return g_console->getColorPair(COLOR_PAIR_COMMENT);
            case TOK_NUMBER:        return g_console->getColorPair(COLOR_PAIR_NUMBER);
            case TOK_PREPROC:       return g_console->getColorPair(COLOR_PAIR_PREPROC);
            default:                return g_console->getColorPair(0); // default
        }
    }

    bool isCStyleFile() {
        if (buffer.filename.empty()) return false;
        const std::string& fn = buffer.filename;
        static const std::vector<std::string> exts = {
            ".c", ".cpp", ".h", ".hpp", ".cc", ".cxx", ".hxx"
        };
        for (const auto& ext : exts) {
            if (fn.size() >= ext.size() &&
                fn.substr(fn.size() - ext.size()) == ext)
                return true;
        }
        return false;
    }

    // ---- End of syntax highlighting ----

public:
    /**
     * Initialize terminal
     */
    void init() {
        startTime = std::chrono::steady_clock::now();  // 开始计时
        g_console = new WindowsConsole();
        g_console->curs_set(1);

        g_console->getmaxyx(screenHeight, screenWidth);
        editAreaHeight = screenHeight - 3;

        showWelcome();
    }

    /**
     * Clean up terminal settings
     */
    void cleanup() {
        delete g_console;
        g_console = nullptr;
    }

    /**
     * Load a file from command line argument
     */
    void loadFile(const std::string& fname, const std::string& displayName = "") {
        actualFilePath = fname;
        if (TextBuffer::fileExists(fname)) {
            if (buffer.loadFromFile(fname)) {
                buffer.filename = displayName.empty() ? fname : displayName;
                statusMessage = "Opened: " + buffer.filename;
            } else {
                statusMessage = "Cannot open: " + fname;
            }
        } else {
            buffer.filename = displayName.empty() ? fname : displayName;
            statusMessage = "New file: " + buffer.filename;
        }
    }

    /**
     * Show welcome screen
     */
    void showWelcome() {
        // g_console->clear();
        // int row = screenHeight / 2 - 3;
        // int col = (screenWidth - 32) / 2;
        // if (col < 0) col = 0;

        // g_console->mvprintw(row,     col, "================================");
        // g_console->mvprintw(row + 1, col, "    %s v%s", EDITOR_NAME, EDITOR_VERSION);
        // g_console->mvprintw(row + 2, col, "================================");
        // g_console->mvprintw(row + 4, col, "  Ctrl+N  New File");
        // g_console->mvprintw(row + 5, col, "  Ctrl+O  Save File");
        // g_console->mvprintw(row + 6, col, "  Ctrl+R  Open File");
        // g_console->mvprintw(row + 7, col, "  Ctrl+X  Exit");
        // g_console->mvprintw(row + 8, col, "  Ctrl+G  Help");

        // Sleep(2000);

        g_console->refresh();
        //g_console->getch();
        g_console->clear();
    }

    /**
     * Main loop
     */
    void run() {
        while (running) {
            checkTerminalSize();
            render();
            processInput();
        }
    }

    /**
     * Detect terminal resize and update dimensions
     */
    void checkTerminalSize() {
        int newHeight, newWidth;
        g_console->getmaxyx(newHeight, newWidth);

        if (newHeight != screenHeight || newWidth != screenWidth) {
            screenHeight = newHeight;
            screenWidth = newWidth;
            editAreaHeight = screenHeight - 3;
            if (editAreaHeight < 1) editAreaHeight = 1;
        }
    }

    /**
     * Render the entire interface
     */
    void render() {
        g_console->curs_set(0);  // Hide cursor during rendering
        renderTitleBar();
        renderContent();
        renderStatusBar();
        renderPromptLine();
        g_console->curs_set(1);  // Show cursor
        positionCursor();
    }

    /**
     * Render title bar
     */
    void renderTitleBar() {
        std::string title;
        if (buffer.filename.empty()) {
            title = "[ Untitled ]";
        } else {
            title = "[ " + buffer.filename + " ]";
        }
        if (buffer.modified) title += " (modified)";

        int padding = screenWidth - (int)title.length();
        if (padding > 0) title += std::string(padding, ' ');

        g_console->attron(g_console->getColorPair(COLOR_PAIR_TITLE));
        g_console->mvprintw(0, 0, "%s", title.substr(0, screenWidth).c_str());
        g_console->attroff();
    }

    /**
     * Render text content (with horizontal scrolling)
     */
    void renderContent() {
        int visibleLines = editAreaHeight;
        adjustScrollOffset();
        bool highlight = isCStyleFile();

        // Compute block-comment state up to scrollOffset
        bool inBlockComment = false;
        if (highlight) {
            for (int i = 0; i < buffer.scrollOffset && i < buffer.getLineCount(); i++) {
                bool dummy = inBlockComment;
                tokenizeLine(buffer.lines[i], dummy);
                inBlockComment = dummy;
            }
        }

        for (int row = 0; row < visibleLines; row++) {
            int lineIndex = buffer.scrollOffset + row;
            int screenRow = row + 1;

            g_console->clearLine(screenRow);

            if (lineIndex >= buffer.getLineCount()) {
                g_console->attron(g_console->getColorPair(COLOR_PAIR_HIGHLIGHT));
                g_console->mvprintw(screenRow, 0, "~");
                g_console->attroff();
            } else {
                std::string& line = buffer.lines[lineIndex];

                // Line number
                char lineNum[8];
                snprintf(lineNum, sizeof(lineNum), "%4d ", lineIndex + 1);
                g_console->attron(g_console->getColorPair(COLOR_PAIR_HIGHLIGHT));
                g_console->mvprintw(screenRow, 0, "%s", lineNum);
                g_console->attroff();

                int contentStart = 5;
                int contentWidth = screenWidth - contentStart;
                if (contentWidth < 1) contentWidth = 1;
                int lineLength = (int)line.length();

                if (highlight) {
                    // Tokenize and render with colors
                    bool bcState = inBlockComment;
                    std::vector<Token> tokens = tokenizeLine(line, bcState);

                    int col = 0;
                    for (auto& tok : tokens) {
                        int tokLen = (int)tok.text.size();
                        int tokEnd = col + tokLen;

                        if (tokEnd <= buffer.horizontalScroll) {
                            col = tokEnd;
                            continue;
                        }
                        if (col >= buffer.horizontalScroll + contentWidth) break;

                        int clipStart = std::max(0, buffer.horizontalScroll - col);
                        int clipEnd   = std::min(tokLen, buffer.horizontalScroll + contentWidth - col);
                        std::string visible = tok.text.substr(clipStart, clipEnd - clipStart);

                        int screenCol = contentStart + (col + clipStart - buffer.horizontalScroll);

                        g_console->attron(tokenColor(tok.type));
                        g_console->mvprintw(screenRow, screenCol, "%s", visible.c_str());
                        g_console->attroff();

                        col = tokEnd;
                    }

                    if (buffer.horizontalScroll + contentWidth < lineLength) {
                        g_console->mvprintw(screenRow, screenWidth - 1, ">");
                    }

                    inBlockComment = bcState;
                } else {
                    // Plain rendering
                    std::string visible_part;
                    if (buffer.horizontalScroll < lineLength) {
                        visible_part = line.substr(buffer.horizontalScroll, contentWidth);
                    }
                    g_console->mvprintw(screenRow, contentStart, "%s", visible_part.c_str());

                    if (buffer.horizontalScroll + contentWidth < lineLength) {
                        g_console->mvprintw(screenRow, screenWidth - 1, ">");
                    }
                }
            }
        }
    }

    /**
     * Adjust scroll offsets to keep cursor visible
     */
    void adjustScrollOffset() {
        if (buffer.cursorY < buffer.scrollOffset)
            buffer.scrollOffset = buffer.cursorY;
        if (buffer.cursorY >= buffer.scrollOffset + editAreaHeight)
            buffer.scrollOffset = buffer.cursorY - editAreaHeight + 1;

        int content_width = screenWidth - 5;
        if (content_width < 1) content_width = 1;
        if (buffer.cursorX < buffer.horizontalScroll)
            buffer.horizontalScroll = buffer.cursorX;
        if (buffer.cursorX >= buffer.horizontalScroll + content_width)
            buffer.horizontalScroll = buffer.cursorX - content_width + 1;
    }

    /**
     * Render status bar
     */
    void renderStatusBar() {
        int statusRow = screenHeight - 2;

        std::string leftInfo = " Ln " + std::to_string(buffer.cursorY + 1) +
                               ", Col " + std::to_string(buffer.cursorX + 1);
        if (!buffer.filename.empty()) leftInfo += " | " + buffer.filename;

        std::string rightInfo = "^G Help  ^O Save  ^X Exit  ";

        int leftLen = (int)leftInfo.length();
        int rightLen = (int)rightInfo.length();
        int spaces = screenWidth - leftLen - rightLen;
        if (spaces < 1) spaces = 1;

        std::string statusBar = leftInfo + std::string(spaces, ' ') + rightInfo;

        g_console->attron(g_console->getColorPair(COLOR_PAIR_STATUS));
        g_console->mvprintw(statusRow, 0, "%s", statusBar.substr(0, screenWidth).c_str());
        g_console->attroff();
    }

    /**
     * Render prompt / command line
     */
    void renderPromptLine() {
        int promptRow = screenHeight - 1;
        g_console->clearLine(promptRow);

        if (mode == MODE_PROMPT || mode == MODE_COMMAND) {
            std::string prompt;
            switch (promptType) {
                case PROMPT_SAVE_AS:
                    prompt = "Save as: " + promptInput;
                    break;
                case PROMPT_OPEN_FILE:
                    prompt = "Open file: " + promptInput;
                    break;
                case PROMPT_NEW_FILE:
                    prompt = "New file (enter filename): " + promptInput;
                    break;
                case PROMPT_OVERWRITE:
                    prompt = "File exists, overwrite? (Y/N): " + promptInput;
                    break;
                case PROMPT_EXIT_UNSAVED:
                    prompt = "File modified, save? (Y/N/C): " + promptInput;
                    break;
                default:
                    prompt = promptInput;
            }
            g_console->mvprintw(promptRow, 0, "%s", prompt.c_str());
        } else if (mode == MODE_SEARCH) {
            g_console->mvprintw(promptRow, 0, "Search: %s", searchQuery.c_str());
        } else if (!statusMessage.empty()) {
            g_console->attron(g_console->getColorPair(COLOR_PAIR_SUCCESS));
            g_console->mvprintw(promptRow, 0, "%s", statusMessage.c_str());
            g_console->attroff();
        } else {
            g_console->mvprintw(promptRow, 0, "^N New  ^R Open  ^W Search  ^K Cut  ^U Paste");
        }
    }

    /**
     * Position cursor at the correct screen location
     */
    void positionCursor() {
        if (mode == MODE_NORMAL) {
            int screenY = buffer.cursorY - buffer.scrollOffset + 1;
            int content_start = 5;
            int screenX = buffer.cursorX - buffer.horizontalScroll + content_start;

            if (screenY >= 1 && screenY <= editAreaHeight &&
                screenX >= content_start && screenX < screenWidth) {
                g_console->move(screenY, screenX);
            }
        } else if (mode == MODE_SEARCH) {
            int promptRow = screenHeight - 1;
            g_console->move(promptRow, 8 + (int)searchQuery.length());
        } else {
            int promptRow = screenHeight - 1;
            int promptLen = 0;

            switch (promptType) {
                case PROMPT_SAVE_AS:        promptLen = 9;  break;
                case PROMPT_OPEN_FILE:      promptLen = 11; break;
                case PROMPT_NEW_FILE:       promptLen = 27; break;
                case PROMPT_OVERWRITE:      promptLen = 31; break;
                case PROMPT_EXIT_UNSAVED:   promptLen = 31; break;
                default:                    promptLen = 0;  break;
            }

            g_console->move(promptRow, promptLen + (int)promptInput.length());
        }
    }

    /**
     * Process user input
     */
    void processInput() {
        int ch = g_console->getch();

        // Ignore resize sentinel
        if (ch == -2) return;

        // Clear status message on any input in normal mode
        if (mode == MODE_NORMAL && !statusMessage.empty()) {
            statusMessage = "";
        }

        if (mode == MODE_NORMAL) {
            processNormalInput(ch);
        } else if (mode == MODE_PROMPT || mode == MODE_COMMAND) {
            processPromptInput(ch);
        } else if (mode == MODE_SEARCH) {
            processSearchInput(ch);
        }
    }

    /**
     * Process input in normal editing mode
     */
    void processNormalInput(int ch) {
        if (ch != KEY_CTRL_X) ctrlXPressCount = 0;
        switch (ch) {
            case KEY_CTRL_X:  promptExit();      break;
            case KEY_CTRL_O:  promptSave();      break;
            case KEY_CTRL_N:  promptNewFile();   break;
            case KEY_CTRL_R:  promptOpenFile();  break;
            case KEY_CTRL_G:  showHelp();        break;
            case KEY_CTRL_W:  startSearch();     break;
            case KEY_CTRL_K:  cutLine();         break;
            case KEY_CTRL_U:  pasteLine();       break;

            case KEY_UP_SPECIAL:      buffer.moveCursor(0, -1);       break;
            case KEY_DOWN_SPECIAL:    buffer.moveCursor(0, 1);        break;
            case KEY_LEFT_SPECIAL:    buffer.moveCursor(-1, 0);       break;
            case KEY_RIGHT_SPECIAL:   buffer.moveCursor(1, 0);        break;
            case KEY_HOME_SPECIAL:    buffer.moveToLineStart();       break;
            case KEY_END_SPECIAL:     buffer.moveToLineEnd();         break;
            case KEY_PPAGE_SPECIAL:   buffer.pageUp(editAreaHeight);  break;
            case KEY_NPAGE_SPECIAL:   buffer.pageDown(editAreaHeight);break;

            case KEY_DC_SPECIAL:
                buffer.deleteChar();
                break;

            case KEY_BACKSPACE_SPECIAL:
                buffer.backspace();
                break;

            case KEY_ENTER_SPECIAL:
                buffer.insertNewline();
                break;

            case '\t':
                for (int i = 0; i < 4; i++) buffer.insertChar(' ');
                break;

            default:
                if (ch >= 32 && ch < 127) buffer.insertChar((char)ch);
                break;
        }
    }

    /**
     * Process input in prompt mode
     */
    void processPromptInput(int ch) {
        switch (ch) {
            case KEY_ENTER_SPECIAL:
                executePrompt();
                break;

            case KEY_BACKSPACE_SPECIAL:
                if (!promptInput.empty()) promptInput.pop_back();
                break;

            case 27:  // ESC - cancel
                cancelPrompt();
                break;

            default:
                if (ch >= 32 && ch < 127) promptInput += (char)ch;
                break;
        }
    }

    /**
     * Process input in search mode
     */
    void processSearchInput(int ch) {
        if (ch == 27) {  // ESC - cancel
            mode = MODE_NORMAL;
            searchQuery = "";
        } else if (ch == KEY_ENTER_SPECIAL) {
            performSearch();
            mode = MODE_NORMAL;
        } else if (ch == KEY_BACKSPACE_SPECIAL) {
            if (!searchQuery.empty()) searchQuery.pop_back();
        } else if (ch >= 32 && ch < 127) {
            searchQuery += (char)ch;
        }
    }

    /**
     * Show help screen
     */
    void showHelp() {
        g_console->clear();
        int row = 2;
        int col = 4;

        g_console->mvprintw(row++, col, "==================================");
        g_console->mvprintw(row++, col, "      %s Help", EDITOR_NAME);
        g_console->mvprintw(row++, col, "==================================");
        row++;

        g_console->mvprintw(row++, col, "File Operations:");
        g_console->mvprintw(row++, col, "  Ctrl+N  New file");
        g_console->mvprintw(row++, col, "  Ctrl+O  Save file");
        g_console->mvprintw(row++, col, "  Ctrl+R  Open file");
        g_console->mvprintw(row++, col, "  Ctrl+X  Exit editor");
        row++;

        g_console->mvprintw(row++, col, "Edit Operations:");
        g_console->mvprintw(row++, col, "  Ctrl+K  Cut current line");
        g_console->mvprintw(row++, col, "  Ctrl+U  Paste cut content");
        g_console->mvprintw(row++, col, "  Ctrl+W  Search text");
        row++;

        g_console->mvprintw(row++, col, "Navigation:");
        g_console->mvprintw(row++, col, "  Arrow   Move cursor");
        g_console->mvprintw(row++, col, "  Home    Go to line start");
        g_console->mvprintw(row++, col, "  End     Go to line end");
        g_console->mvprintw(row++, col, "  PgUp    Page up");
        g_console->mvprintw(row++, col, "  PgDown  Page down");
        row++;

        g_console->mvprintw(row++, col, "Press any key to return...");

        g_console->refresh();
        g_console->getch();
        g_console->clear();
    }

    /**
     * Start save file flow
     */
    void promptSave() {
        if (buffer.filename.empty()) {
            mode = MODE_PROMPT;
            promptType = PROMPT_SAVE_AS;
            promptInput = "";
        } else {
            doSave(buffer.filename);
        }
    }

    /**
     * Start new file flow
     */
    void promptNewFile() {
        if (buffer.modified) {
            previousPromptType = PROMPT_NEW_FILE;
            mode = MODE_PROMPT;
            promptType = PROMPT_EXIT_UNSAVED;
            promptInput = "";
        } else {
            mode = MODE_PROMPT;
            promptType = PROMPT_NEW_FILE;
            promptInput = "";
        }
    }

    /**
     * Start open file flow
     */
    void promptOpenFile() {
        if (buffer.modified) {
            previousPromptType = PROMPT_OPEN_FILE;
            mode = MODE_PROMPT;
            promptType = PROMPT_EXIT_UNSAVED;
            promptInput = "";
        } else {
            mode = MODE_PROMPT;
            promptType = PROMPT_OPEN_FILE;
            promptInput = "";
        }
    }

    /**
     * Start exit flow
     */
    void promptExit() {
        if (buffer.modified) {
            ctrlXPressCount++;
            if (ctrlXPressCount >= 3) {
                running = false;
            } else {
                int remaining = 3 - ctrlXPressCount;
                statusMessage = "File not saved! Press Ctrl+X " + std::to_string(remaining) + " more times to exit without saving";
            }
        } else {
            running = false;
        }
    }

    /**
     * Start search
     */
    void startSearch() {
        mode = MODE_SEARCH;
        searchQuery = "";
        statusMessage = "";
    }

    /**
     * Perform text search
     */
    void performSearch() {
        if (searchQuery.empty()) return;

        for (int y = buffer.cursorY; y < buffer.getLineCount(); y++) {
            std::string& line = buffer.lines[y];
            size_t pos;

            if (y == buffer.cursorY) {
                pos = line.find(searchQuery, buffer.cursorX + 1);
            } else {
                pos = line.find(searchQuery);
            }

            if (pos != std::string::npos) {
                buffer.cursorY = y;
                buffer.cursorX = (int)pos;
                searchX = (int)pos;
                searchY = y;
                statusMessage = "Found: " + searchQuery;
                return;
            }
        }

        // Wrap around from the beginning
        for (int y = 0; y <= buffer.cursorY; y++) {
            std::string& line = buffer.lines[y];
            size_t pos = line.find(searchQuery);

            if (pos != std::string::npos) {
                if (y == buffer.cursorY && (int)pos <= buffer.cursorX) continue;
                buffer.cursorY = y;
                buffer.cursorX = (int)pos;
                searchX = (int)pos;
                searchY = y;
                statusMessage = "Found (wrapped): " + searchQuery;
                return;
            }
        }

        statusMessage = "Not found: " + searchQuery;
    }

    /**
     * Cut current line
     */
    void cutLine() {
        if (buffer.cursorY < buffer.getLineCount()) {
            clipboard = buffer.lines[buffer.cursorY];
            buffer.lines.erase(buffer.lines.begin() + buffer.cursorY);
            if (buffer.lines.empty()) buffer.lines.push_back("");
            if (buffer.cursorY >= buffer.getLineCount())
                buffer.cursorY = buffer.getLineCount() - 1;
            buffer.adjustCursorX();
            buffer.modified = true;
            statusMessage = "Line cut";
        }
    }

    /**
     * Paste line
     */
    void pasteLine() {
        if (!clipboard.empty()) {
            buffer.lines.insert(buffer.lines.begin() + buffer.cursorY, clipboard);
            buffer.cursorY++;
            buffer.cursorX = 0;
            buffer.modified = true;
            statusMessage = "Pasted";
        }
    }

    /**
     * Execute the current prompt action
     */
    void executePrompt() {
        switch (promptType) {
            case PROMPT_SAVE_AS:
                if (!promptInput.empty()) {
                    if (!isValidPath(promptInput)) {
                        statusMessage = "Invalid path: " + promptInput;
                        mode = MODE_NORMAL; promptType = PROMPT_NONE; promptInput = "";
                        return;
                    }
                    std::string np = normalizePath(promptInput);
                    if (TextBuffer::fileExists(np)) {
                        pendingFilename = np;
                        promptType = PROMPT_OVERWRITE;
                        promptInput = "";
                        return;
                    }
                    doSave(np);
                }
                break;

            case PROMPT_OPEN_FILE:
                if (!promptInput.empty()) doOpen(promptInput);
                break;

            case PROMPT_NEW_FILE:
                doNewFile(promptInput);
                break;

            case PROMPT_OVERWRITE:
                if (!promptInput.empty() &&
                    (promptInput[0] == 'Y' || promptInput[0] == 'y')) {
                    doSave(pendingFilename);
                } else {
                    statusMessage = "Cancelled";
                }
                break;

            case PROMPT_EXIT_UNSAVED:
                if (!promptInput.empty()) {
                    if (promptInput[0] == 'Y' || promptInput[0] == 'y') {
                        if (buffer.filename.empty()) {
                            promptType = PROMPT_SAVE_AS;
                            promptInput = "";
                            return;
                        } else {
                            doSave(buffer.filename);
                            if (previousPromptType == PROMPT_NONE) {
                                running = false;
                            }
                        }
                    } else if (promptInput[0] == 'N' || promptInput[0] == 'n') {
                        buffer.modified = false;
                        if (previousPromptType == PROMPT_NEW_FILE) {
                            mode = MODE_PROMPT;
                            promptType = PROMPT_NEW_FILE;
                            promptInput = "";
                            previousPromptType = PROMPT_NONE;
                            return;
                        } else if (previousPromptType == PROMPT_OPEN_FILE) {
                            mode = MODE_PROMPT;
                            promptType = PROMPT_OPEN_FILE;
                            promptInput = "";
                            previousPromptType = PROMPT_NONE;
                            return;
                        } else {
                            running = false;
                        }
                    } else if (promptInput[0] == 'C' || promptInput[0] == 'c') {
                        cancelPrompt();
                        return;
                    }
                }
                break;

            default:
                break;
        }

        mode = MODE_NORMAL;
        promptType = PROMPT_NONE;
        promptInput = "";
        previousPromptType = PROMPT_NONE;
    }

    /**
     * Cancel current prompt
     */
    void cancelPrompt() {
        mode = MODE_NORMAL;
        promptType = PROMPT_NONE;
        promptInput = "";
        previousPromptType = PROMPT_NONE;
        statusMessage = "Cancelled";
    }

    /**
     * Execute file save
     */
    void doSave(const std::string& fname) {
        std::string savePath;
        if (fname == buffer.filename && !actualFilePath.empty()) {
            // Saving current file — use actual path directly, no normalization
            savePath = actualFilePath;
        } else {
            if (!isValidPath(fname)) {
                statusMessage = "Invalid path: " + fname;
                mode = MODE_NORMAL;
                promptType = PROMPT_NONE;
                promptInput = "";
                return;
            }
            savePath = normalizePath(fname);
            std::string dir = savePath;
            size_t pos = dir.rfind('/');
            if (pos != std::string::npos) {
                dir = dir.substr(0, pos);
                if (!ensureDirectoryExists(dir)) {
                    statusMessage = "Failed to create directory: " + dir;
                    return;
                }
            }
            actualFilePath = savePath;
            buffer.filename = savePath;
        }
        if (buffer.saveToFile(savePath)) {
            statusMessage = "Saved: " + buffer.filename;
        } else {
            statusMessage = "Save failed: " + savePath;
        }
    }

    /**
     * Execute file open
     */
    void doOpen(const std::string& fname) {
        if (!isValidPath(fname)) {
            statusMessage = "Invalid path: " + fname;
            mode = MODE_NORMAL;
            promptType = PROMPT_NONE;
            promptInput = "";
            return;
        }
        std::string normalizedPath = normalizePath(fname);
        if (TextBuffer::fileExists(normalizedPath)) {
            if (buffer.loadFromFile(normalizedPath)) {
                buffer.filename = normalizedPath;
                actualFilePath = normalizedPath;
                statusMessage = "Opened: " + normalizedPath;
            } else {
                statusMessage = "Cannot open: " + normalizedPath;
            }
        } else {
            statusMessage = "File not found: " + normalizedPath;
        }
    }

    /**
     * Execute new file
     */
    void doNewFile(const std::string& fname) {
        buffer.clear();
        actualFilePath = "";
        if (!fname.empty()) {
            if (!isValidPath(fname)) {
                statusMessage = "Invalid path: " + fname;
                mode = MODE_NORMAL;
                promptType = PROMPT_NONE;
                promptInput = "";
                return;
            }
            buffer.filename = normalizePath(fname);
            actualFilePath = buffer.filename;
        }
        statusMessage = "New file" + (buffer.filename.empty() ? "" : ": " + buffer.filename);
    }
};

/**
 * Editor entry point. Opens filepath (if non-empty) and runs the editor.
 */
int run_editor(const std::string& filepath, const std::string& displayName) {
    TerminalEditor editor;

    editor.init();

    if (!filepath.empty()) {
        editor.loadFile(filepath, displayName);
    }

    editor.run();
    editor.cleanup();

    return 0;
}
