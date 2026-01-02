#include "TUXfile.h"
#include <fstream>
#include <stdexcept>
#include <string>
#include <iostream>
#include "color.h"
#include <vector>
#include <windows.h>
#include <ctime>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_map>
#include "udata.h"
#include <conio.h>
// Simple XOR encryption/decryption 
void writeEncryptedString(std::ofstream& out, const std::string& data) {
    std::string enc = encryptDecrypt(data);
    size_t len = enc.size();
    out.write(reinterpret_cast<const char*>(&len), sizeof(len));
    out.write(enc.data(), len);
}
bool readEncryptedString(std::ifstream& in, std::string& data) {
    size_t len = 0;
    in.read(reinterpret_cast<char*>(&len), sizeof(len));
    if (!in) return false;
    std::vector<char> buf(len);
    in.read(buf.data(), len);
    if (!in) return false;
    data = encryptDecrypt(std::string(buf.data(), len));
    return true;
}

// ---------- Metadata & State ----------
struct FileMetadata {
    std::string creator;
    std::string lastEditor;
    std::time_t createTime{};
    std::time_t modifyTime{};
};

USER currentUser;
static bool g_lastTuxReadOk = true;
const size_t MAX_TUX_STRING_LEN  = 1024;
const size_t MAX_TUX_CONTENT_LEN = 16 * 1024 * 1024;

static bool hasPrivilege() {
    std::string t = currentUser.type;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    return t == "debug" || t == "admin";
}

// Verify filename validity
static bool isValidFilename(const std::string& name) {
    return !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    });
}

// ---------- Paths & Directories ----------
std::string getTuxPath(const std::string& filename) {
    std::string full = filename;
    if (full.find(".TUX") == std::string::npos) full += ".TUX";
    return "Files\\" + full;
}
void initFilesDir() {
    const std::string dir = "Files";
    DWORD attrs = GetFileAttributesA(dir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) CreateDirectoryA(dir.c_str(), NULL);
}

// ---------- Reader Helper ----------
class TuxReader {
    std::ifstream& in;
    uintmax_t remaining;
    std::string path;
public:
    TuxReader(std::ifstream& s, uintmax_t size, const std::string& p)
        : in(s), remaining(size), path(p) {}
    bool readExact(void* dst, size_t n) {
        if (remaining < n) return false;
        in.read(reinterpret_cast<char*>(dst), n);
        if (!in) return false;
        remaining -= n; return true;
    }
    bool readEncryptedString(std::string& out, size_t maxLen, const char* label) {
        (void)label; //unused
        size_t len = 0;
        if (!readExact(&len, sizeof(len))) return false;
        if (len > maxLen || len > remaining) return false;
        std::string enc(len, '\0');
        if (!readExact(enc.data(), len)) return false;
        out = encryptDecrypt(enc);
        return true;
    }
};

// ---------- Read Metadata ----------
FileMetadata readMetadata(const std::string& path) {
    FileMetadata meta{};
    std::ifstream in(path, std::ios::binary);
    if (!in) return meta;
    uintmax_t fsize = 0;
    try { fsize = std::filesystem::file_size(path); }
    catch (...) { return {}; }
    TuxReader r(in, fsize, path);
    unsigned int ver = 0;
    if (!r.readExact(&ver, sizeof(ver)) || ver != 1) return {};
    if (!r.readEncryptedString(meta.creator, MAX_TUX_STRING_LEN, "creator")) return {};
    if (!r.readEncryptedString(meta.lastEditor, MAX_TUX_STRING_LEN, "lastEditor")) return {};
    if (!r.readExact(&meta.createTime, sizeof(meta.createTime))) return {};
    if (!r.readExact(&meta.modifyTime, sizeof(meta.modifyTime))) return {};
    return meta;
}

// ---------- Full Read ----------
std::pair<std::string, FileMetadata> readFullTuxFile(const std::string& path) {
    g_lastTuxReadOk = false;
    FileMetadata meta{};
    std::ifstream in(path, std::ios::binary);
    if (!in) return {"", meta};
    uintmax_t fsize = 0;
    try { fsize = std::filesystem::file_size(path); }
    catch (...) { return {"", meta}; }
    TuxReader r(in, fsize, path);
    unsigned int ver = 0;
    if (!r.readExact(&ver, sizeof(ver)) || ver != 1) return {"", meta};
    if (!r.readEncryptedString(meta.creator, MAX_TUX_STRING_LEN, "creator")) return {"", meta};
    if (!r.readEncryptedString(meta.lastEditor, MAX_TUX_STRING_LEN, "lastEditor")) return {"", meta};
    if (!r.readExact(&meta.createTime, sizeof(meta.createTime))) return {"", meta};
    if (!r.readExact(&meta.modifyTime, sizeof(meta.modifyTime))) return {"", meta};
    std::string content;
    if (!r.readEncryptedString(content, MAX_TUX_CONTENT_LEN, "content")) return {"", meta};
    g_lastTuxReadOk = true;
    return {content, meta};
}

// ---------- Write ----------
void writeTuxFile(const std::string& path, const std::string& content, const FileMetadata& meta) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { colorcout("RED", "Write failed: " + path + "\n"); return; }
    unsigned int ver = 1;
    out.write(reinterpret_cast<const char*>(&ver), sizeof(ver));
    writeEncryptedString(out, meta.creator);
    writeEncryptedString(out, meta.lastEditor);
    out.write(reinterpret_cast<const char*>(&meta.createTime), sizeof(meta.createTime));
    out.write(reinterpret_cast<const char*>(&meta.modifyTime), sizeof(meta.modifyTime));
    writeEncryptedString(out, content);
}

// ---------- List ----------
void listTuxFiles() {
    colorcout("CYAN", "=== TUX File List ===\n");
    const std::string root = "Files";
    if (!std::filesystem::exists(root)) {
        colorcout("YELLOW", "(Files directory does not exist)\n\n"); return;
    }
    size_t count = 0;
    std::function<void(const std::filesystem::path&, const std::string&)> walk;
    walk = [&](const std::filesystem::path& dir, const std::string& pre) {
        std::vector<std::filesystem::directory_entry> es;
        for (auto &e: std::filesystem::directory_iterator(dir)) {
            if (e.is_directory() || (e.is_regular_file() && e.path().extension()==".TUX")) es.push_back(e);
        }
        std::sort(es.begin(), es.end(), [](auto&a, auto&b){return a.path().filename().string()<b.path().filename().string();});
        for (size_t i=0;i<es.size();++i){
            bool last = (i+1==es.size());
            std::string conn = last?"`- ":"|- ";
            std::string next = pre + (last?"   ":"|  ");
            if (es[i].is_directory()) {
                colorcout("white", pre+conn+es[i].path().filename().string()+"\n");
                walk(es[i].path(), next);
            } else {
                ++count;
                colorcout("white", pre+conn+es[i].path().filename().string()+"\n");
            }
        }
    };
    colorcout("CYAN","Files\n");
    walk(root,"");
    if (count==0) colorcout("YELLOW","\n(Empty directory)\n");
    std::cout << "\n";
}

// ---------- Create ----------
void createTuxFile(const std::string& filename) {
    if (filename.empty()) { colorcout("RED","Usage: create <filename>\n"); return; }
    std::string path = getTuxPath(filename);
    if (std::filesystem::exists(path)) {
        if (!getYN("File already exists, overwrite?")) {
            colorcout("YELLOW","Cancelled\n");
            return;
        }
    }
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    FileMetadata meta{currentUser.name,currentUser.name,now,now};
    writeTuxFile(path,"",meta);
    colorcout("GREEN","Created empty file: "+filename+"\n\n");
}

// ---------- View Content ----------
void viewTuxFile(const std::string& filename) {
    if (filename.empty()) { colorcout("RED","Usage: view <filename>\n"); return; }
    std::string path = getTuxPath(filename);
    if (!std::filesystem::exists(path)) { colorcout("RED","Not found: "+filename+"\n"); return; }
    auto [content, _] = readFullTuxFile(path);
    if (!g_lastTuxReadOk) { colorcout("RED","File corrupted or invalid format\n\n"); return; }
    colorcout("CYAN","=== "+filename+" ===\n");
    std::cout << content << "\n\n";
}

// ---------- Edit ----------
static std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines; std::stringstream ss(text); std::string l;
    while (std::getline(ss,l)) lines.push_back(l);
    if (text.empty() || text.back()=='\n') lines.push_back("");
    if (lines.empty()) lines.push_back("");
    return lines;
}

static void renderEditorInit(const std::vector<std::string>& lines) {
    system("cls");
    colorcout("CYAN","=== TUX Inline Editor ===\n");
    colorcout("YELLOW","[press tab to enter commands]\n\n");
    for (size_t i=0;i<lines.size();++i) {
        std::cout << lines[i] << "\n";
    }
    std::cout << "\nCommand: (/s to save and quit, /q to quit without saving)" << std::flush;
}

static int getWrapWidth(HANDLE h) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h, &csbi);
    // Reserve 1 column to avoid automatic console line wrapping
    return std::max<SHORT>(1, csbi.dwSize.X - 1);
}

static int rowCount(const std::string& line, int wrapWidth) {
    return std::max<int>(1, static_cast<int>((line.size() + wrapWidth - 1) / wrapWidth));
}

static void clearRow(HANDLE h, SHORT y) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h, &csbi);
    COORD pos = {0, y};
    DWORD written;
    FillConsoleOutputCharacter(h, ' ', csbi.dwSize.X, pos, &written);
}

// Unified handling of bottom area rendering (blank lines + command line + clearing old remnants)
static void renderBottomArea(HANDLE h, int displayRows, const std::string& text, int wrapWidth, int& prevBottom) {
    const SHORT headerRows = 3;
    SHORT blankY = headerRows + static_cast<SHORT>(displayRows);
    
    // 1. Clear blank lines between content and command line (to prevent old command line remnants when content moves down)
    clearRow(h, blankY);

    // 2. Calculate new command line position
    SHORT cmdStartY = blankY + 1;
    std::string full = "Command: " + text;
    int totalLen = static_cast<int>(full.size());
    int needRows = std::max(1, (totalLen + wrapWidth - 1) / wrapWidth);
    SHORT cmdEndY = cmdStartY + static_cast<SHORT>(needRows) - 1;

    // 3. render command line with wrapping
    SHORT yPos = cmdStartY;
    for (size_t i = 0; i < full.size(); i += wrapWidth) {
        clearRow(h, yPos); // Clear the line before rendering
        SetConsoleCursorPosition(h, {0, yPos});
        std::cout << full.substr(i, wrapWidth) << std::flush;
        yPos++;
    }

    // 4. Clear remnants below (if content moves up or command line shortens)
    if (prevBottom > cmdEndY) {
        for (int y = cmdEndY + 1; y <= prevBottom; ++y) {
            clearRow(h, static_cast<SHORT>(y));
        }
    }

    // 5. Update bottom area record
    prevBottom = cmdEndY;
}

static int renderAllLines(HANDLE h, const std::vector<std::string>& lines, int wrapWidth, int& prevRows) {
    const SHORT headerRows = 3; // title + hint + blank
    SHORT y = headerRows;
    int usedRows = 0;
    for (const auto& line : lines) {
        if (line.empty()) {
            clearRow(h, y);
            SetConsoleCursorPosition(h, {0, y});
            std::cout << std::flush;
            ++y; ++usedRows;
            continue;
        }
        for (size_t start = 0; start < line.size(); start += wrapWidth) {
            clearRow(h, y);
            SetConsoleCursorPosition(h, {0, y});
            std::cout << line.substr(start, wrapWidth) << std::flush;
            ++y; ++usedRows;
        }
    }
    // clean up remaining old lines
    for (int r = usedRows; r < prevRows; ++r, ++y) {
        clearRow(h, y);
    }
    prevRows = usedRows;
    return usedRows;
}

static void setCursorPosition(HANDLE h, int cx, int cy, int displayRows, bool cmdMode, const std::string& cmd, const std::vector<std::string>& lines) {
    const SHORT headerRows = 3;
    int wrapWidth = getWrapWidth(h);
    COORD pos;
    if (cmdMode) {
        pos.X = 9 + static_cast<SHORT>(cmd.size()); // "Command: "
        pos.Y = headerRows + static_cast<SHORT>(displayRows) + 1;
    } else {
        int rowsBefore = 0;
        for (int i = 0; i < cy; ++i) rowsBefore += rowCount(lines[i], wrapWidth);
        pos.X = static_cast<SHORT>(cx % wrapWidth);
        pos.Y = headerRows + static_cast<SHORT>(rowsBefore + (cx / wrapWidth));
    }
    SetConsoleCursorPosition(h, pos);
}

// ---------- Edit ----------
void editTuxFile(const std::string& filename) {
    if (filename.empty()) { colorcout("RED","Usage: edit <filename>\n"); return; }
    std::string path = getTuxPath(filename);
    if (!std::filesystem::exists(path)) { colorcout("RED","Not found: "+filename+"\n"); return; }
    auto [oldContent, meta] = readFullTuxFile(path);
    if (!g_lastTuxReadOk) { colorcout("RED","File corrupted, abort editing\n\n"); return; }
    // Security check: only creator, admin, or debug can edit
    if (meta.creator != currentUser.name && !hasPrivilege()) {
        colorcout("RED", "Access denied: You are not the creator of this file\n");
        return;
    }

    std::vector<std::string> lines = splitLines(oldContent);
    int cx=0, cy=0;
    bool cmdMode=false; std::string cmd;
    bool editing=true; bool saved=false;
    
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    int wrapWidth = getWrapWidth(h);
    int displayRows = 0;
    int prevDisplayRows = 0;
    int prevBottom = 0; // Record the bottom line of the command area

    // Initial render
    renderEditorInit(lines);
    displayRows = renderAllLines(h, lines, wrapWidth, prevDisplayRows);
    renderBottomArea(h, displayRows, "(/s to save and quit, /q to quit without saving)", wrapWidth, prevBottom);
    setCursorPosition(h, cx, cy, displayRows, cmdMode, cmd, lines);

    while (editing) {
        int ch=_getch();
        if (cmdMode) {
            if (ch==13) { // Enter
                if (cmd=="/s") {
                    std::string newContent;
                    for (size_t i=0;i<lines.size();++i) {
                        newContent += lines[i];
                        if (i+1<lines.size()) newContent += "\n";
                    }
                    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    meta.lastEditor = currentUser.name;
                    meta.modifyTime = now;
                    writeTuxFile(path,newContent,meta);
                    
                    // Clear screen for message
                    system("cls");
                    colorcout("GREEN","Saved\n\n");
                    saved=true; editing=false;
                } else if (cmd=="/q") {
                    system("cls");
                    colorcout("YELLOW","Edit cancelled\n\n");
                    editing=false;
                }
                cmdMode=false; cmd.clear();
                if (editing) {
                    displayRows = renderAllLines(h, lines, wrapWidth, prevDisplayRows);
                    renderBottomArea(h, displayRows, "(/s to save and quit, /q to quit without saving)", wrapWidth, prevBottom);
                    setCursorPosition(h, cx, cy, displayRows, cmdMode, cmd, lines);
                }
            } else if (ch==27) { // ESC
                cmdMode=false; cmd.clear();
                renderBottomArea(h, displayRows, "(/s to save and quit, /q to quit without saving)", wrapWidth, prevBottom);
                setCursorPosition(h, cx, cy, displayRows, cmdMode, cmd, lines);
            }
            else if (ch==8) { // Backspace in command mode
                if(!cmd.empty()) {
                    cmd.pop_back();
                    renderBottomArea(h, displayRows, cmd, wrapWidth, prevBottom);
                    setCursorPosition(h, cx, cy, displayRows, cmdMode, cmd, lines);
                }
            }
            else if (isprint(ch)) {
                cmd.push_back((char)ch);
                renderBottomArea(h, displayRows, cmd, wrapWidth, prevBottom);
                setCursorPosition(h, cx, cy, displayRows, cmdMode, cmd, lines);
            }
            continue;
        }

        if (ch==9) { // Tab
            cmdMode=true; cmd.clear();
            renderBottomArea(h, displayRows, cmd, wrapWidth, prevBottom);
            setCursorPosition(h, cx, cy, displayRows, cmdMode, cmd, lines);
            continue;
        }
        
        if (ch==0 || ch==224) {
            int k=_getch();
            switch (k) {
                case 75: if (cx>0) --cx; break; // left
                case 77: if (cx<(int)lines[cy].size()) ++cx; break; // right
                case 72: if (cy>0){ --cy; cx=std::min<int>(cx, lines[cy].size()); } break; // up
                case 80: if (cy+1<(int)lines.size()){ ++cy; cx=std::min<int>(cx, lines[cy].size()); } break; // down
            }
            setCursorPosition(h, cx, cy, displayRows, cmdMode, cmd, lines);
        } else if (ch==8) { // backspace
            if (cx>0) {
                lines[cy].erase(cx-1,1);
                --cx;
            }
            else if (cy>0) {
                cx = (int)lines[cy-1].size();
                lines[cy-1] += lines[cy];
                lines.erase(lines.begin()+cy);
                --cy;
            }
            displayRows = renderAllLines(h, lines, wrapWidth, prevDisplayRows);
            renderBottomArea(h, displayRows, "(/s to save and quit, /q to quit without saving)", wrapWidth, prevBottom);
            setCursorPosition(h, cx, cy, displayRows, cmdMode, cmd, lines);
        } else if (ch==13) { // enter
            std::string rest = lines[cy].substr(cx);
            lines[cy].erase(cx);
            lines.insert(lines.begin()+cy+1, rest);
            ++cy; cx=0;
            displayRows = renderAllLines(h, lines, wrapWidth, prevDisplayRows);
            renderBottomArea(h, displayRows, "(/s to save and quit, /q to quit without saving)", wrapWidth, prevBottom);
            setCursorPosition(h, cx, cy, displayRows, cmdMode, cmd, lines);
        } else if (isprint(ch)) {
            lines[cy].insert(lines[cy].begin()+cx,(char)ch);
            ++cx;
            displayRows = renderAllLines(h, lines, wrapWidth, prevDisplayRows);
            renderBottomArea(h, displayRows, "(/s to save and quit, /q to quit without saving)", wrapWidth, prevBottom);
            setCursorPosition(h, cx, cy, displayRows, cmdMode, cmd, lines);
        }
    }

    if (!saved) colorcout("YELLOW","No changes saved\n\n");
}

// ---------- Delete ----------
void deleteTuxFile(const std::string& filename) {
    if (filename.empty()) { colorcout("RED","Usage: delete <filename>\n"); return; }
    std::string path = getTuxPath(filename);
    if (!std::filesystem::exists(path)) { colorcout("RED","Not found: "+filename+"\n"); return; }
    if (getYN("Confirm delete "+filename)) {
        if (std::filesystem::remove(path)) colorcout("GREEN","Deleted\n\n");
        else colorcout("RED","Delete failed\n\n");
    } else colorcout("YELLOW","Cancelled\n\n");
}

// ---------- Rename ----------
void renameTuxFile(const std::string& oldname, const std::string& newname) {
    if (oldname.empty()||newname.empty()) { colorcout("RED","Usage: rename <old> <new>\n"); return; }
    if (!isValidFilename(oldname) || !isValidFilename(newname)) {
        colorcout("YELLOW","Invalid filename. Only letters, digits, and '-' are allowed.\n");
        return;
    }
    std::string op = getTuxPath(oldname), np = getTuxPath(newname);
    if (!std::filesystem::exists(op)) { colorcout("RED","Not found: "+oldname+"\n"); return; }
    if (std::filesystem::exists(np)) { colorcout("RED","Target already exists: "+newname+"\n"); return; }
    std::filesystem::rename(op,np);
    colorcout("GREEN","Renamed: "+oldname+" -> "+newname+"\n\n");
}

// ---------- Export ----------
void exportTuxFile(const std::string& filename) {
    if (filename.empty()) { colorcout("RED","Usage: export <filename>\n"); return; }
    std::string tuxPath = getTuxPath(filename);
    if (!std::filesystem::exists(tuxPath)) { colorcout("RED","Not found: "+filename+"\n"); return; }
    auto [content, meta] = readFullTuxFile(tuxPath);
    if (!g_lastTuxReadOk) { colorcout("RED","File corrupted, export aborted\n\n"); return; }
    std::string txtName = filename;
    if (txtName.find(".TUX")!=std::string::npos) txtName = txtName.substr(0, txtName.find(".TUX"));
    txtName += ".txt";
    std::string txtPath = "Files\\" + txtName;
    if (std::filesystem::exists(txtPath)) {
        if (!getYN("TXT already exists, overwrite?")) {
            colorcout("YELLOW","Cancelled\n\n");
            return;
        }
    }
    std::ofstream out(txtPath);
    if (!out) { colorcout("RED","Failed to create TXT\n\n"); return; }
    std::tm ct{}, mt{};
    localtime_s(&ct, &meta.createTime);
    localtime_s(&mt, &meta.modifyTime);
    char cbuf[64], mbuf[64];
    std::strftime(cbuf,sizeof(cbuf),"%Y-%m-%d %H:%M:%S",&ct);
    std::strftime(mbuf,sizeof(mbuf),"%Y-%m-%d %H:%M:%S",&mt);
    out << "=== TundraUX File Metadata ===\n";
    out << "Creator: " << meta.creator << "\n";
    out << "Last Editor: " << meta.lastEditor << "\n";
    out << "Create Time: " << cbuf << "\n";
    out << "Modify Time: " << mbuf << "\n";
    out << "=== End of Metadata ===\n\n";
    out << content;
    out.close();
    colorcout("GREEN","Exported as: "+txtName+"\n\n");
}

// ---------- Import ----------
void importTxtFile(const std::string& filename) {
    if (filename.empty()) { colorcout("RED","Usage: import <filename>\n"); return; }
    std::string txt = filename;
    if (txt.find(".txt")==std::string::npos) txt += ".txt";
    std::string txtPath = "Files\\" + txt;
    if (!std::filesystem::exists(txtPath)) { colorcout("RED","TXT not found: "+txt+"\n"); return; }
    std::ifstream in(txtPath);
    if (!in) { colorcout("RED","Failed to read TXT\n\n"); return; }
    FileMetadata meta{};
    std::string line, content; int lineNum=0; bool header=false, ended=false;
    while (std::getline(in,line)) {
        ++lineNum;
        if (lineNum==1) { if (line!="=== TundraUX File Metadata ===") { colorcout("RED","Missing metadata header\n"); return; } header=true; continue; }
        if (!ended) {
            if (line=="=== End of Metadata ===") { ended=true; continue; }
            auto pos = line.find(": ");
            if (pos==std::string::npos) { colorcout("RED","Invalid metadata format\n"); return; }
            std::string field=line.substr(0,pos), val=line.substr(pos+2);
            if (field=="Creator") meta.creator=val;
            else if (field=="Last Editor") meta.lastEditor=val;
            else if (field=="Create Time") {
                std::tm tm{}; std::istringstream ss(val); ss>>std::get_time(&tm,"%Y-%m-%d %H:%M:%S");
                if (ss.fail()) { colorcout("RED","Invalid time format\n"); return; }
                meta.createTime = std::mktime(&tm);
            } else if (field=="Modify Time") {
                std::tm tm{}; std::istringstream ss(val); ss>>std::get_time(&tm,"%Y-%m-%d %H:%M:%S");
                if (ss.fail()) { colorcout("RED","Invalid time format\n"); return; }
                meta.modifyTime = std::mktime(&tm);
            }
        } else {
            if (!content.empty()) content+="\n";
            content += line;
        }
    }
    if (!header || !ended || meta.creator.empty() || meta.lastEditor.empty() || meta.createTime==0 || meta.modifyTime==0) {
        colorcout("RED","Metadata missing or incomplete\n\n"); return;
    }
    // Build target name
    std::string tuxName = txt;
    if (tuxName.find(".txt")!=std::string::npos) tuxName = tuxName.substr(0,tuxName.find(".txt"));
    tuxName += ".TUX";
    std::string tuxPath = getTuxPath(tuxName);
    if (std::filesystem::exists(tuxPath)) {
        if (!getYN("TUX already exists, overwrite?")) {
            colorcout("YELLOW","Cancelled\n\n");
            return;
        }
    }
    meta.lastEditor = currentUser.name;
    meta.modifyTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    writeTuxFile(tuxPath, content, meta);
    colorcout("GREEN","Imported as: "+tuxName+"\n\n");
}

// ---------- View Metadata ----------
void viewMetadata(const std::string& filename) {
    if (filename.empty()) { colorcout("RED","Usage: metadata <filename>\n"); return; }
    std::string path = getTuxPath(filename);
    if (!std::filesystem::exists(path)) { colorcout("RED","Not found: "+filename+"\n"); return; }
    FileMetadata meta = readMetadata(path);
    if (meta.creator.empty() && meta.lastEditor.empty()) { colorcout("RED","Failed to read, file may be corrupted\n\n"); return; }
    colorcout("CYAN","=== Metadata for: "+filename+" ===\n\n");
    colorcout("white","Creator: "+meta.creator+"\n");
    colorcout("white","Last Editor: "+meta.lastEditor+"\n");
    auto fmt = [](std::time_t t){ std::tm tm{}; localtime_s(&tm,&t); char buf[64]; std::strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",&tm); return std::string(buf); };
    colorcout("white","Create Time: "+fmt(meta.createTime)+"\n");
    colorcout("white","Modify Time: "+fmt(meta.modifyTime)+"\n\n");
}

// ---------- Simple CLI Entry ----------
void showHelp() {
    colorcout("CYAN","=== Help List ===\n\n");
    colorcout("white","ls             - list files\n");
    colorcout("white","c  <f>         - create file\n");
    colorcout("white","e  <f>         - edit file\n");
    colorcout("white","v  <f>         - view file\n");
    colorcout("white","d  <f>         - delete file\n");
    colorcout("white","rn <o> <n>     - rename file\n");
    colorcout("white","m  <f>         - view metadata\n");
    colorcout("white","ex <f>         - export file\n");
    colorcout("white","im <f>         - import file\n");
    colorcout("white","h              - show help\n");
    colorcout("white","q              - quit\n\n");
}

void file_editor(const std::string& currentUsername, const std::string& currentUsertype) {
    set_title("TUX File Manager");
    currentUser.name = currentUsername;
    currentUser.type = currentUsertype;
    initFilesDir();
    colorcout("CYAN","=== TUX File Manager ===\n");
    colorcout("white","Current user: "+currentUser.name+"\n\n");
    //showHelp();
    listTuxFiles();
    std::vector<std::string> commandHistory;
    int historyIndex = -1;
    const int MAX_HISTORY = 100;
    std::string input;
    while (true) {
        colorcout("white", "> ");
        input = readLineWithHistory(commandHistory, historyIndex);
        if (input.empty()) continue;
        if (commandHistory.empty() || commandHistory.back() != input) {
            if (static_cast<int>(commandHistory.size()) >= MAX_HISTORY) {
                commandHistory.erase(commandHistory.begin());
            }
            commandHistory.push_back(input);
        }
        historyIndex = -1;
        std::istringstream iss(input);
        std::string cmd; iss >> cmd;
        if (cmd=="q" || cmd=="exit") break;
        else if (cmd=="h" || cmd=="help") showHelp();
        else if (cmd=="ls" || cmd=="list") listTuxFiles();
        else if (cmd=="c" || cmd=="create") { std::string f; std::getline(iss>>std::ws, f); createTuxFile(f); }
        else if (cmd=="e" || cmd=="edit") { std::string f; std::getline(iss>>std::ws, f); editTuxFile(f); }
        else if (cmd=="v" || cmd=="view") { std::string f; std::getline(iss>>std::ws, f); viewTuxFile(f); }
        else if (cmd=="d" || cmd=="delete") { std::string f; std::getline(iss>>std::ws, f); deleteTuxFile(f); }
        else if (cmd=="rn" || cmd=="rename") { std::string a,b; iss>>a>>b; renameTuxFile(a,b); }
        else if (cmd=="ex" || cmd=="export") {
            if (!hasPrivilege()) { colorcout("RED","Access denied: You don't have the required privileges\n"); continue; }
            std::string f; std::getline(iss>>std::ws, f); exportTuxFile(f);
        }
        else if (cmd=="im" || cmd=="import") {
            if (!hasPrivilege()) { colorcout("RED","Access denied: You don't have the required privileges\n"); continue; }
            std::string f; std::getline(iss>>std::ws, f); importTxtFile(f);
        }
        else if (cmd=="m" || cmd=="metadata") {
            if (!hasPrivilege()) { colorcout("RED","Access denied: You don't have the required privileges\n"); continue; }
            std::string f; std::getline(iss>>std::ws, f); viewMetadata(f);
        }
        else { colorcout("RED","Unknown command\n"); }
    }
    colorcout("green", "Program exited\n");
}