#include "editor_win.h"

#ifdef _WIN32

int run_editor_portable(const std::string& filepath, const std::string& displayName) {
    return run_editor_windows(filepath, displayName);
}

#else

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

#define EDITOR_NAME "Tundra Editor"

#define KEY_CTRL_X 24
#define KEY_CTRL_O 15
#define KEY_CTRL_N 14
#define KEY_CTRL_R 18
#define KEY_CTRL_W 23
#define KEY_CTRL_K 11
#define KEY_CTRL_U 21
#define KEY_CTRL_G 7

#define SPECIAL_KEY_BASE 0x100
#define KEY_UP_SPECIAL (SPECIAL_KEY_BASE + 1)
#define KEY_DOWN_SPECIAL (SPECIAL_KEY_BASE + 2)
#define KEY_LEFT_SPECIAL (SPECIAL_KEY_BASE + 3)
#define KEY_RIGHT_SPECIAL (SPECIAL_KEY_BASE + 4)
#define KEY_HOME_SPECIAL (SPECIAL_KEY_BASE + 5)
#define KEY_END_SPECIAL (SPECIAL_KEY_BASE + 6)
#define KEY_PPAGE_SPECIAL (SPECIAL_KEY_BASE + 7)
#define KEY_NPAGE_SPECIAL (SPECIAL_KEY_BASE + 8)
#define KEY_DC_SPECIAL (SPECIAL_KEY_BASE + 9)
#define KEY_ENTER_SPECIAL (SPECIAL_KEY_BASE + 10)
#define KEY_BACKSPACE_SPECIAL (SPECIAL_KEY_BASE + 11)

#define COLOR_PAIR_TITLE 1
#define COLOR_PAIR_STATUS 2
#define COLOR_PAIR_HIGHLIGHT 3
#define COLOR_PAIR_SUCCESS 4
#define COLOR_PAIR_ERROR 5
#define COLOR_PAIR_KEYWORD 6
#define COLOR_PAIR_STRING 7
#define COLOR_PAIR_COMMENT 8
#define COLOR_PAIR_NUMBER 9
#define COLOR_PAIR_PREPROC 10

enum EditorMode {
    MODE_NORMAL,
    MODE_COMMAND,
    MODE_PROMPT,
    MODE_SEARCH
};

enum PromptType {
    PROMPT_NONE,
    PROMPT_SAVE_AS,
    PROMPT_OPEN_FILE,
    PROMPT_NEW_FILE,
    PROMPT_OVERWRITE,
    PROMPT_EXIT_UNSAVED
};

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
    if (dirPath.empty()) return true;
    std::error_code ec;
    std::filesystem::create_directories(dirPath, ec);
    return !ec;
}

static std::string normalizePath(const std::string& input) {
    std::string path = unifySlashes(input);
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    if (path.size() >= 6 && path.substr(0, 6) == "Files/") {
        return path;
    }
    return "Files/" + path;
}

class AnsiConsole {
private:
    termios originalTermios{};
    bool termiosSaved = false;
    int cachedWidth = 80;
    int cachedHeight = 24;

    bool hasPendingInput(int timeoutMs) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        timeval timeout{};
        timeout.tv_sec = timeoutMs / 1000;
        timeout.tv_usec = (timeoutMs % 1000) * 1000;
        return select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout) > 0;
    }

    int readByte() {
        unsigned char ch = 0;
        const ssize_t count = read(STDIN_FILENO, &ch, 1);
        if (count <= 0) return -1;
        return static_cast<int>(ch);
    }

    void writeAnsi(const std::string& text) {
        std::cout << text;
    }

public:
    AnsiConsole() {
        termiosSaved = tcgetattr(STDIN_FILENO, &originalTermios) == 0;
        if (termiosSaved) {
            termios raw = originalTermios;
            raw.c_iflag &= static_cast<unsigned int>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
            raw.c_oflag &= static_cast<unsigned int>(~(OPOST));
            raw.c_cflag |= CS8;
            raw.c_lflag &= static_cast<unsigned int>(~(ECHO | ICANON | IEXTEN | ISIG));
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        }

        writeAnsi("\x1b[?1049h\x1b[H\x1b[2J\x1b[?25h");
        std::cout.flush();
        getmaxyx(cachedHeight, cachedWidth);
    }

    ~AnsiConsole() {
        writeAnsi("\x1b[0m\x1b[2J\x1b[H\x1b[?25h\x1b[?1049l");
        std::cout.flush();
        if (termiosSaved) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermios);
        }
    }

    void clear() { writeAnsi("\x1b[2J\x1b[H"); }
    void refresh() { std::cout.flush(); }

    void getmaxyx(int& height, int& width) {
        winsize ws{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
            cachedWidth = ws.ws_col;
            cachedHeight = ws.ws_row;
        }
        height = cachedHeight;
        width = cachedWidth;
    }

    void move(int y, int x) { std::cout << "\x1b[" << (y + 1) << ";" << (x + 1) << "H"; }

    void mvprintw(int y, int x, const char* fmt, ...) {
        move(y, x);
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        std::cout << buf;
    }

    void clearLine(int y) {
        move(y, 0);
        writeAnsi("\x1b[2K");
    }

    void curs_set(int visible) { writeAnsi(visible ? "\x1b[?25h" : "\x1b[?25l"); }

    void attron(int attributes) {
        switch (attributes) {
            case COLOR_PAIR_TITLE: writeAnsi("\x1b[37;44;1m"); break;
            case COLOR_PAIR_STATUS: writeAnsi("\x1b[30;47m"); break;
            case COLOR_PAIR_HIGHLIGHT: writeAnsi("\x1b[33m"); break;
            case COLOR_PAIR_SUCCESS: writeAnsi("\x1b[32m"); break;
            case COLOR_PAIR_ERROR: writeAnsi("\x1b[31m"); break;
            case COLOR_PAIR_KEYWORD: writeAnsi("\x1b[94m"); break;
            case COLOR_PAIR_STRING: writeAnsi("\x1b[32m"); break;
            case COLOR_PAIR_COMMENT: writeAnsi("\x1b[36m"); break;
            case COLOR_PAIR_NUMBER: writeAnsi("\x1b[35m"); break;
            case COLOR_PAIR_PREPROC: writeAnsi("\x1b[33;1m"); break;
            default: writeAnsi("\x1b[0m"); break;
        }
    }

    void attroff() { writeAnsi("\x1b[0m"); }
    int getColorPair(int pair) { return pair; }

    int getch() {
        const int ch = readByte();
        if (ch < 0) return ch;
        if (ch == '\r' || ch == '\n') return KEY_ENTER_SPECIAL;
        if (ch == 127 || ch == 8) return KEY_BACKSPACE_SPECIAL;
        if (ch != 27) return ch;
        if (!hasPendingInput(20)) return 27;
        const int next = readByte();
        if (next == '[') {
            const int third = readByte();
            switch (third) {
                case 'A': return KEY_UP_SPECIAL;
                case 'B': return KEY_DOWN_SPECIAL;
                case 'C': return KEY_RIGHT_SPECIAL;
                case 'D': return KEY_LEFT_SPECIAL;
                case 'H': return KEY_HOME_SPECIAL;
                case 'F': return KEY_END_SPECIAL;
                case '3': if (hasPendingInput(20) && readByte() == '~') return KEY_DC_SPECIAL; break;
                case '5': if (hasPendingInput(20) && readByte() == '~') return KEY_PPAGE_SPECIAL; break;
                case '6': if (hasPendingInput(20) && readByte() == '~') return KEY_NPAGE_SPECIAL; break;
            }
        } else if (next == 'O') {
            const int third = readByte();
            if (third == 'H') return KEY_HOME_SPECIAL;
            if (third == 'F') return KEY_END_SPECIAL;
        }
        return 27;
    }
};

static AnsiConsole* g_console = nullptr;

class TextBuffer {
public:
    std::vector<std::string> lines;
    int cursorX = 0;
    int cursorY = 0;
    int scrollOffset = 0;
    int horizontalScroll = 0;
    bool modified = false;
    std::string filename;

    TextBuffer() { lines.push_back(""); }

    void insertChar(char c) {
        if (cursorY >= static_cast<int>(lines.size())) lines.push_back("");
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
            const int prevLen = static_cast<int>(lines[cursorY - 1].length());
            lines[cursorY - 1] += lines[cursorY];
            lines.erase(lines.begin() + cursorY);
            cursorY--;
            cursorX = prevLen;
            modified = true;
        }
    }

    void deleteChar() {
        if (cursorX < static_cast<int>(lines[cursorY].length())) {
            lines[cursorY].erase(cursorX, 1);
            modified = true;
        } else if (cursorY < static_cast<int>(lines.size()) - 1) {
            lines[cursorY] += lines[cursorY + 1];
            lines.erase(lines.begin() + cursorY + 1);
            modified = true;
        }
    }

    void insertNewline() {
        if (cursorY >= static_cast<int>(lines.size())) {
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
        if (cursorY >= static_cast<int>(lines.size())) cursorY = static_cast<int>(lines.size()) - 1;
        const int lineLen = static_cast<int>(lines[cursorY].length());
        if (cursorX < 0) cursorX = 0;
        if (cursorX > lineLen) cursorX = lineLen;
    }

    void moveToLineStart() { cursorX = 0; }
    void moveToLineEnd() { cursorX = static_cast<int>(lines[cursorY].length()); }

    void pageUp(int pageSize) {
        cursorY -= pageSize;
        if (cursorY < 0) cursorY = 0;
        adjustCursorX();
    }

    void pageDown(int pageSize) {
        cursorY += pageSize;
        if (cursorY >= static_cast<int>(lines.size())) cursorY = static_cast<int>(lines.size()) - 1;
        adjustCursorX();
    }

    void adjustCursorX() {
        const int lineLen = static_cast<int>(lines[cursorY].length());
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
        filename.clear();
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
        return true;
    }

    bool saveToFile(const std::string& fname) {
        std::ofstream file(fname);
        if (!file.is_open()) return false;

        for (size_t i = 0; i < lines.size(); ++i) {
            file << lines[i];
            if (i + 1 < lines.size()) file << "\n";
        }

        modified = false;
        return true;
    }

    static bool fileExists(const std::string& fname) {
        return std::filesystem::exists(fname);
    }

    int getLineCount() const { return static_cast<int>(lines.size()); }
};

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
    std::string pendingFilename;
    PromptType previousPromptType = PROMPT_NONE;
    int ctrlXPressCount = 0;
    std::string actualFilePath;

    static const std::vector<std::string>& getCKeywords() {
        static const std::vector<std::string> keywords = {
            "auto","break","case","char","const","continue","default","do",
            "double","else","enum","extern","float","for","goto","if",
            "inline","int","long","register","restrict","return","short",
            "signed","sizeof","static","struct","switch","typedef","union",
            "unsigned","void","volatile","while","_Bool","_Complex",
            "_Imaginary","bool","true","false","nullptr","NULL",
            "class","public","private","protected","new","delete","this",
            "namespace","using","template","typename","virtual","override",
            "final","explicit","operator","friend","throw","try","catch",
            "noexcept","constexpr","auto","decltype","static_assert",
            "std","string","vector","map","set"
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

    std::vector<Token> tokenizeLine(const std::string& line, bool& inBlockComment) {
        std::vector<Token> tokens;
        const int n = static_cast<int>(line.size());
        int i = 0;

        if (!inBlockComment) {
            int start = 0;
            while (start < n && (line[start] == ' ' || line[start] == '\t')) start++;
            if (start < n && line[start] == '#') {
                tokens.push_back({TOK_PREPROC, line});
                return tokens;
            }
        }

        while (i < n) {
            if (inBlockComment) {
                const int start = i;
                while (i < n) {
                    if (i + 1 < n && line[i] == '*' && line[i + 1] == '/') {
                        i += 2;
                        inBlockComment = false;
                        break;
                    }
                    i++;
                }
                tokens.push_back({TOK_COMMENT_BLOCK, line.substr(start, i - start)});
                continue;
            }

            if (line[i] == '"') {
                const int start = i++;
                while (i < n) {
                    if (line[i] == '\\') { i += 2; continue; }
                    if (line[i] == '"') { i++; break; }
                    i++;
                }
                tokens.push_back({TOK_STRING, line.substr(start, i - start)});
                continue;
            }

            if (line[i] == '\'') {
                const int start = i++;
                while (i < n) {
                    if (line[i] == '\\') { i += 2; continue; }
                    if (line[i] == '\'') { i++; break; }
                    i++;
                }
                tokens.push_back({TOK_CHAR_LIT, line.substr(start, i - start)});
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(line[i])) ||
                (line[i] == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
                const int start = i;
                if (line[i] == '0' && i + 1 < n && (line[i + 1] == 'x' || line[i + 1] == 'X')) {
                    i += 2;
                    while (i < n && std::isxdigit(static_cast<unsigned char>(line[i]))) i++;
                } else {
                    while (i < n && (std::isdigit(static_cast<unsigned char>(line[i])) || line[i] == '.' ||
                           line[i] == 'e' || line[i] == 'E' ||
                           line[i] == 'f' || line[i] == 'F' ||
                           line[i] == 'u' || line[i] == 'U' ||
                           line[i] == 'l' || line[i] == 'L')) {
                        i++;
                    }
                }
                tokens.push_back({TOK_NUMBER, line.substr(start, i - start)});
                continue;
            }

            if (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_') {
                const int start = i;
                while (i < n && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) i++;
                const std::string word = line.substr(start, i - start);
                const auto& kws = getCKeywords();
                const bool isKw = std::find(kws.begin(), kws.end(), word) != kws.end();
                tokens.push_back({isKw ? TOK_KEYWORD : TOK_NORMAL, word});
                continue;
            }

            if (!tokens.empty() && tokens.back().type == TOK_NORMAL) {
                tokens.back().text += line[i];
            } else {
                tokens.push_back({TOK_NORMAL, std::string(1, line[i])});
            }
            i++;
        }

        return tokens;
    }

    int tokenColor(TokenType t) {
        switch (t) {
            case TOK_KEYWORD: return g_console->getColorPair(COLOR_PAIR_KEYWORD);
            case TOK_STRING:
            case TOK_CHAR_LIT: return g_console->getColorPair(COLOR_PAIR_STRING);
            case TOK_COMMENT_LINE:
            case TOK_COMMENT_BLOCK: return g_console->getColorPair(COLOR_PAIR_COMMENT);
            case TOK_NUMBER: return g_console->getColorPair(COLOR_PAIR_NUMBER);
            case TOK_PREPROC: return g_console->getColorPair(COLOR_PAIR_PREPROC);
            default: return g_console->getColorPair(0);
        }
    }

    bool isCStyleFile() {
        if (buffer.filename.empty()) return false;
        const std::string& fn = buffer.filename;
        static const std::vector<std::string> exts = {".c", ".cpp", ".h", ".hpp", ".cc", ".cxx", ".hxx"};
        for (const auto& ext : exts) {
            if (fn.size() >= ext.size() && fn.substr(fn.size() - ext.size()) == ext) return true;
        }
        return false;
    }

public:
    void init() {
        g_console = new AnsiConsole();
        g_console->curs_set(1);
        g_console->getmaxyx(screenHeight, screenWidth);
        editAreaHeight = std::max(1, screenHeight - 3);
        g_console->refresh();
        g_console->clear();
    }

    void cleanup() {
        delete g_console;
        g_console = nullptr;
    }

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

    void run() {
        while (running) {
            checkTerminalSize();
            render();
            processInput();
        }
    }

    void checkTerminalSize() {
        int newHeight = 0;
        int newWidth = 0;
        g_console->getmaxyx(newHeight, newWidth);
        if (newHeight != screenHeight || newWidth != screenWidth) {
            screenHeight = newHeight;
            screenWidth = newWidth;
            editAreaHeight = std::max(1, screenHeight - 3);
        }
    }

    void render() {
        g_console->curs_set(0);
        renderTitleBar();
        renderContent();
        renderStatusBar();
        renderPromptLine();
        g_console->curs_set(1);
        positionCursor();
        g_console->refresh();
    }

    void renderTitleBar() {
        std::string title = buffer.filename.empty() ? "[ Untitled ]" : "[ " + buffer.filename + " ]";
        if (buffer.modified) title += " (modified)";
        if (static_cast<int>(title.length()) < screenWidth) {
            title += std::string(screenWidth - title.length(), ' ');
        }
        g_console->attron(g_console->getColorPair(COLOR_PAIR_TITLE));
        g_console->mvprintw(0, 0, "%s", title.substr(0, screenWidth).c_str());
        g_console->attroff();
    }

    void adjustScrollOffset() {
        if (buffer.cursorY < buffer.scrollOffset) buffer.scrollOffset = buffer.cursorY;
        if (buffer.cursorY >= buffer.scrollOffset + editAreaHeight) {
            buffer.scrollOffset = buffer.cursorY - editAreaHeight + 1;
        }

        int contentWidth = screenWidth - 5;
        if (contentWidth < 1) contentWidth = 1;
        if (buffer.cursorX < buffer.horizontalScroll) buffer.horizontalScroll = buffer.cursorX;
        if (buffer.cursorX >= buffer.horizontalScroll + contentWidth) {
            buffer.horizontalScroll = buffer.cursorX - contentWidth + 1;
        }
    }

    void renderContent() {
        adjustScrollOffset();
        const bool highlight = isCStyleFile();
        bool inBlockComment = false;

        if (highlight) {
            for (int i = 0; i < buffer.scrollOffset && i < buffer.getLineCount(); ++i) {
                bool dummy = inBlockComment;
                tokenizeLine(buffer.lines[i], dummy);
                inBlockComment = dummy;
            }
        }

        for (int row = 0; row < editAreaHeight; ++row) {
            const int lineIndex = buffer.scrollOffset + row;
            const int screenRow = row + 1;
            g_console->clearLine(screenRow);

            if (lineIndex >= buffer.getLineCount()) {
                g_console->attron(g_console->getColorPair(COLOR_PAIR_HIGHLIGHT));
                g_console->mvprintw(screenRow, 0, "~");
                g_console->attroff();
                continue;
            }

            std::string& line = buffer.lines[lineIndex];
            char lineNum[16];
            snprintf(lineNum, sizeof(lineNum), "%4d ", lineIndex + 1);
            g_console->attron(g_console->getColorPair(COLOR_PAIR_HIGHLIGHT));
            g_console->mvprintw(screenRow, 0, "%s", lineNum);
            g_console->attroff();

            const int contentStart = 5;
            int contentWidth = screenWidth - contentStart;
            if (contentWidth < 1) contentWidth = 1;
            const int lineLength = static_cast<int>(line.length());

            if (highlight) {
                bool bcState = inBlockComment;
                const std::vector<Token> tokens = tokenizeLine(line, bcState);
                int col = 0;
                for (const auto& tok : tokens) {
                    const int tokLen = static_cast<int>(tok.text.size());
                    const int tokEnd = col + tokLen;
                    if (tokEnd <= buffer.horizontalScroll) {
                        col = tokEnd;
                        continue;
                    }
                    if (col >= buffer.horizontalScroll + contentWidth) break;

                    const int clipStart = std::max(0, buffer.horizontalScroll - col);
                    const int clipEnd = std::min(tokLen, buffer.horizontalScroll + contentWidth - col);
                    const std::string visible = tok.text.substr(clipStart, clipEnd - clipStart);
                    const int screenCol = contentStart + (col + clipStart - buffer.horizontalScroll);
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
                std::string visiblePart;
                if (buffer.horizontalScroll < lineLength) {
                    visiblePart = line.substr(buffer.horizontalScroll, contentWidth);
                }
                g_console->mvprintw(screenRow, contentStart, "%s", visiblePart.c_str());
                if (buffer.horizontalScroll + contentWidth < lineLength) {
                    g_console->mvprintw(screenRow, screenWidth - 1, ">");
                }
            }
        }
    }

    void renderStatusBar() {
        const int statusRow = screenHeight - 2;
        std::string leftInfo = " Ln " + std::to_string(buffer.cursorY + 1) +
                               ", Col " + std::to_string(buffer.cursorX + 1);
        if (!buffer.filename.empty()) leftInfo += " | " + buffer.filename;
        const std::string rightInfo = "^G Help  ^O Save  ^X Exit  ";
        int spaces = screenWidth - static_cast<int>(leftInfo.length()) - static_cast<int>(rightInfo.length());
        if (spaces < 1) spaces = 1;
        const std::string statusBar = leftInfo + std::string(spaces, ' ') + rightInfo;

        g_console->attron(g_console->getColorPair(COLOR_PAIR_STATUS));
        g_console->mvprintw(statusRow, 0, "%s", statusBar.substr(0, screenWidth).c_str());
        g_console->attroff();
    }

    void renderPromptLine() {
        const int promptRow = screenHeight - 1;
        g_console->clearLine(promptRow);

        if (mode == MODE_PROMPT || mode == MODE_COMMAND) {
            std::string prompt;
            switch (promptType) {
                case PROMPT_SAVE_AS: prompt = "Save as: " + promptInput; break;
                case PROMPT_OPEN_FILE: prompt = "Open file: " + promptInput; break;
                case PROMPT_NEW_FILE: prompt = "New file (enter filename): " + promptInput; break;
                case PROMPT_OVERWRITE: prompt = "File exists, overwrite? (Y/N): " + promptInput; break;
                case PROMPT_EXIT_UNSAVED: prompt = "File modified, save? (Y/N/C): " + promptInput; break;
                default: prompt = promptInput; break;
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

    void positionCursor() {
        if (mode == MODE_NORMAL) {
            const int screenY = buffer.cursorY - buffer.scrollOffset + 1;
            const int contentStart = 5;
            const int screenX = buffer.cursorX - buffer.horizontalScroll + contentStart;
            if (screenY >= 1 && screenY <= editAreaHeight && screenX >= contentStart && screenX < screenWidth) {
                g_console->move(screenY, screenX);
            }
        } else if (mode == MODE_SEARCH) {
            g_console->move(screenHeight - 1, 8 + static_cast<int>(searchQuery.length()));
        } else {
            int promptLen = 0;
            switch (promptType) {
                case PROMPT_SAVE_AS: promptLen = 9; break;
                case PROMPT_OPEN_FILE: promptLen = 11; break;
                case PROMPT_NEW_FILE: promptLen = 27; break;
                case PROMPT_OVERWRITE: promptLen = 31; break;
                case PROMPT_EXIT_UNSAVED: promptLen = 31; break;
                default: break;
            }
            g_console->move(screenHeight - 1, promptLen + static_cast<int>(promptInput.length()));
        }
    }

    void processInput() {
        const int ch = g_console->getch();
        if (mode == MODE_NORMAL && !statusMessage.empty()) statusMessage.clear();
        if (mode == MODE_NORMAL) processNormalInput(ch);
        else if (mode == MODE_PROMPT || mode == MODE_COMMAND) processPromptInput(ch);
        else if (mode == MODE_SEARCH) processSearchInput(ch);
    }

    void processNormalInput(int ch) {
        if (ch != KEY_CTRL_X) ctrlXPressCount = 0;
        switch (ch) {
            case KEY_CTRL_X: promptExit(); break;
            case KEY_CTRL_O: promptSave(); break;
            case KEY_CTRL_N: promptNewFile(); break;
            case KEY_CTRL_R: promptOpenFile(); break;
            case KEY_CTRL_G: showHelp(); break;
            case KEY_CTRL_W: startSearch(); break;
            case KEY_CTRL_K: cutLine(); break;
            case KEY_CTRL_U: pasteLine(); break;
            case KEY_UP_SPECIAL: buffer.moveCursor(0, -1); break;
            case KEY_DOWN_SPECIAL: buffer.moveCursor(0, 1); break;
            case KEY_LEFT_SPECIAL: buffer.moveCursor(-1, 0); break;
            case KEY_RIGHT_SPECIAL: buffer.moveCursor(1, 0); break;
            case KEY_HOME_SPECIAL: buffer.moveToLineStart(); break;
            case KEY_END_SPECIAL: buffer.moveToLineEnd(); break;
            case KEY_PPAGE_SPECIAL: buffer.pageUp(editAreaHeight); break;
            case KEY_NPAGE_SPECIAL: buffer.pageDown(editAreaHeight); break;
            case KEY_DC_SPECIAL: buffer.deleteChar(); break;
            case KEY_BACKSPACE_SPECIAL: buffer.backspace(); break;
            case KEY_ENTER_SPECIAL: buffer.insertNewline(); break;
            case '\t':
                for (int i = 0; i < 4; ++i) buffer.insertChar(' ');
                break;
            default:
                if (ch >= 32 && ch < 127) buffer.insertChar(static_cast<char>(ch));
                break;
        }
    }

    void processPromptInput(int ch) {
        switch (ch) {
            case KEY_ENTER_SPECIAL: executePrompt(); break;
            case KEY_BACKSPACE_SPECIAL: if (!promptInput.empty()) promptInput.pop_back(); break;
            case 27: cancelPrompt(); break;
            default: if (ch >= 32 && ch < 127) promptInput += static_cast<char>(ch); break;
        }
    }

    void processSearchInput(int ch) {
        if (ch == 27) {
            mode = MODE_NORMAL;
            searchQuery.clear();
        } else if (ch == KEY_ENTER_SPECIAL) {
            performSearch();
            mode = MODE_NORMAL;
        } else if (ch == KEY_BACKSPACE_SPECIAL) {
            if (!searchQuery.empty()) searchQuery.pop_back();
        } else if (ch >= 32 && ch < 127) {
            searchQuery += static_cast<char>(ch);
        }
    }

    void showHelp() {
        g_console->clear();
        int row = 2;
        const int col = 4;
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

    void promptSave() {
        if (buffer.filename.empty()) {
            mode = MODE_PROMPT;
            promptType = PROMPT_SAVE_AS;
            promptInput.clear();
        } else {
            doSave(buffer.filename);
        }
    }

    void promptNewFile() {
        if (buffer.modified) {
            previousPromptType = PROMPT_NEW_FILE;
            mode = MODE_PROMPT;
            promptType = PROMPT_EXIT_UNSAVED;
            promptInput.clear();
        } else {
            mode = MODE_PROMPT;
            promptType = PROMPT_NEW_FILE;
            promptInput.clear();
        }
    }

    void promptOpenFile() {
        if (buffer.modified) {
            previousPromptType = PROMPT_OPEN_FILE;
            mode = MODE_PROMPT;
            promptType = PROMPT_EXIT_UNSAVED;
            promptInput.clear();
        } else {
            mode = MODE_PROMPT;
            promptType = PROMPT_OPEN_FILE;
            promptInput.clear();
        }
    }

    void promptExit() {
        if (buffer.modified) {
            ctrlXPressCount++;
            if (ctrlXPressCount >= 3) {
                running = false;
            } else {
                const int remaining = 3 - ctrlXPressCount;
                statusMessage = "File not saved! Press Ctrl+X " + std::to_string(remaining) + " more times to exit without saving";
            }
        } else {
            running = false;
        }
    }

    void startSearch() {
        mode = MODE_SEARCH;
        searchQuery.clear();
        statusMessage.clear();
    }

    void performSearch() {
        if (searchQuery.empty()) return;

        for (int y = buffer.cursorY; y < buffer.getLineCount(); ++y) {
            std::string& line = buffer.lines[y];
            size_t pos = (y == buffer.cursorY) ? line.find(searchQuery, buffer.cursorX + 1) : line.find(searchQuery);
            if (pos != std::string::npos) {
                buffer.cursorY = y;
                buffer.cursorX = static_cast<int>(pos);
                statusMessage = "Found: " + searchQuery;
                return;
            }
        }

        for (int y = 0; y <= buffer.cursorY; ++y) {
            std::string& line = buffer.lines[y];
            const size_t pos = line.find(searchQuery);
            if (pos != std::string::npos) {
                if (y == buffer.cursorY && static_cast<int>(pos) <= buffer.cursorX) continue;
                buffer.cursorY = y;
                buffer.cursorX = static_cast<int>(pos);
                statusMessage = "Found (wrapped): " + searchQuery;
                return;
            }
        }

        statusMessage = "Not found: " + searchQuery;
    }

    void cutLine() {
        if (buffer.cursorY < buffer.getLineCount()) {
            clipboard = buffer.lines[buffer.cursorY];
            buffer.lines.erase(buffer.lines.begin() + buffer.cursorY);
            if (buffer.lines.empty()) buffer.lines.push_back("");
            if (buffer.cursorY >= buffer.getLineCount()) buffer.cursorY = buffer.getLineCount() - 1;
            buffer.adjustCursorX();
            buffer.modified = true;
            statusMessage = "Line cut";
        }
    }

    void pasteLine() {
        if (!clipboard.empty()) {
            buffer.lines.insert(buffer.lines.begin() + buffer.cursorY, clipboard);
            buffer.cursorY++;
            buffer.cursorX = 0;
            buffer.modified = true;
            statusMessage = "Pasted";
        }
    }

    void cancelPrompt() {
        mode = MODE_NORMAL;
        promptType = PROMPT_NONE;
        promptInput.clear();
        previousPromptType = PROMPT_NONE;
        statusMessage = "Cancelled";
    }

    void doSave(const std::string& fname) {
        std::string savePath;
        if (fname == buffer.filename && !actualFilePath.empty()) {
            savePath = actualFilePath;
        } else {
            if (!isValidPath(fname)) {
                statusMessage = "Invalid path: " + fname;
                mode = MODE_NORMAL;
                promptType = PROMPT_NONE;
                promptInput.clear();
                return;
            }
            savePath = normalizePath(fname);
            const std::filesystem::path dir = std::filesystem::path(savePath).parent_path();
            if (!dir.empty() && !ensureDirectoryExists(dir.string())) {
                statusMessage = "Failed to create directory: " + dir.string();
                return;
            }
            actualFilePath = savePath;
            buffer.filename = savePath;
        }

        if (buffer.saveToFile(savePath)) statusMessage = "Saved: " + buffer.filename;
        else statusMessage = "Save failed: " + savePath;
    }

    void doOpen(const std::string& fname) {
        if (!isValidPath(fname)) {
            statusMessage = "Invalid path: " + fname;
            mode = MODE_NORMAL;
            promptType = PROMPT_NONE;
            promptInput.clear();
            return;
        }
        const std::string normalizedPath = normalizePath(fname);
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

    void doNewFile(const std::string& fname) {
        buffer.clear();
        actualFilePath.clear();
        if (!fname.empty()) {
            if (!isValidPath(fname)) {
                statusMessage = "Invalid path: " + fname;
                mode = MODE_NORMAL;
                promptType = PROMPT_NONE;
                promptInput.clear();
                return;
            }
            buffer.filename = normalizePath(fname);
            actualFilePath = buffer.filename;
        }
        statusMessage = "New file" + (buffer.filename.empty() ? "" : ": " + buffer.filename);
    }

    void executePrompt() {
        switch (promptType) {
            case PROMPT_SAVE_AS:
                if (!promptInput.empty()) {
                    if (!isValidPath(promptInput)) {
                        statusMessage = "Invalid path: " + promptInput;
                        mode = MODE_NORMAL;
                        promptType = PROMPT_NONE;
                        promptInput.clear();
                        return;
                    }
                    const std::string np = normalizePath(promptInput);
                    if (TextBuffer::fileExists(np)) {
                        pendingFilename = np;
                        promptType = PROMPT_OVERWRITE;
                        promptInput.clear();
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
                if (!promptInput.empty() && (promptInput[0] == 'Y' || promptInput[0] == 'y')) doSave(pendingFilename);
                else statusMessage = "Cancelled";
                break;
            case PROMPT_EXIT_UNSAVED:
                if (!promptInput.empty()) {
                    if (promptInput[0] == 'Y' || promptInput[0] == 'y') {
                        if (buffer.filename.empty()) {
                            promptType = PROMPT_SAVE_AS;
                            promptInput.clear();
                            return;
                        }
                        doSave(buffer.filename);
                        if (previousPromptType == PROMPT_NONE) running = false;
                    } else if (promptInput[0] == 'N' || promptInput[0] == 'n') {
                        buffer.modified = false;
                        if (previousPromptType == PROMPT_NEW_FILE) {
                            mode = MODE_PROMPT;
                            promptType = PROMPT_NEW_FILE;
                            promptInput.clear();
                            previousPromptType = PROMPT_NONE;
                            return;
                        }
                        if (previousPromptType == PROMPT_OPEN_FILE) {
                            mode = MODE_PROMPT;
                            promptType = PROMPT_OPEN_FILE;
                            promptInput.clear();
                            previousPromptType = PROMPT_NONE;
                            return;
                        }
                        running = false;
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
        promptInput.clear();
        previousPromptType = PROMPT_NONE;
    }
};

int run_editor_portable(const std::string& filepath, const std::string& displayName) {
    TerminalEditor editor;
    editor.init();
    if (!filepath.empty()) editor.loadFile(filepath, displayName);
    editor.run();
    editor.cleanup();
    return 0;
}

#endif
