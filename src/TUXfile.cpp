//Attention: windows only code.
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
#include <random>
#include "editor_win.h"
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

// Verify path validity (components separated by '/')
static bool isValidPath(const std::string& p) {
    if (p.empty()) return false;
    std::string comp;
    for (char c : p) {
        if (c == '/') {
            if (!isValidFilename(comp)) return false;
            comp.clear();
        } else {
            comp += c;
        }
    }
    return isValidFilename(comp);
}

// ---------- Paths & Directories ----------
std::string getTuxPath(const std::string& filename) {
    std::string full = filename;
    if (full.find(".TUX") == std::string::npos) full += ".TUX";
    for (char& c : full) if (c == '/') c = '\\';
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
    const std::string root = "Files";
    if (!std::filesystem::exists(root)) {
        colorcout("YELLOW", "(Files directory does not exist)\n\n"); return;
    }
    size_t count = 0;
    std::function<void(const std::filesystem::path&, const std::string&)> walk;
    walk = [&](const std::filesystem::path& dir, const std::string& pre) {
        std::vector<std::filesystem::directory_entry> es;
        for (auto& e : std::filesystem::directory_iterator(dir)) {
            // Skip the temp directory
            if (e.is_directory() && e.path().filename() == "temp") continue;
            if (e.is_directory() || (e.is_regular_file() && e.path().extension() == ".TUX")) es.push_back(e);
        }
        std::sort(es.begin(), es.end(), [](auto& a, auto& b) {
            // Directories first, then files; within each group sort alphabetically
            if (a.is_directory() != b.is_directory()) return a.is_directory() > b.is_directory();
            return a.path().filename().string() < b.path().filename().string();
        });
        for (size_t i = 0; i < es.size(); ++i) {
            bool last = (i + 1 == es.size());
            std::string conn = last ? "`- " : "|- ";
            std::string next = pre + (last ? "   " : "|  ");
            if (es[i].is_directory()) {
                colorcout("CYAN", pre + conn + es[i].path().filename().string() + "/\n");
                walk(es[i].path(), next);
            } else {
                ++count;
                // Strip .TUX extension for display
                std::string name = es[i].path().stem().string();
                colorcout("white", pre + conn + name + "\n");
            }
        }
    };
    colorcout("CYAN", "Files/\n");
    walk(root, "");
    if (count == 0) colorcout("YELLOW", "\n(No files)\n");
    std::cout << "\n";
}

// ---------- Create ----------
void createTuxFile(const std::string& filename) {
    if (filename.empty()) { colorcout("RED","Usage: create <filename>\n"); return; }
    if (!isValidPath(filename)) {
        colorcout("YELLOW","Invalid filename. Use alphanumeric, '-', '_', and '/' for subfolders.\n"); return;
    }
    std::string path = getTuxPath(filename);
    if (std::filesystem::exists(path)) {
        if (!getYN("File already exists, overwrite?")) {
            colorcout("YELLOW","Cancelled\n");
            return;
        }
    }
    // Create parent directories if needed
    std::filesystem::path fp(path);
    if (fp.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(fp.parent_path(), ec);
        if (ec) { colorcout("RED","Failed to create directory: "+ec.message()+"\n"); return; }
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
void editTuxFile(const std::string& filename) {
    if (filename.empty()) { colorcout("RED","Usage: edit <filename>\n"); return; }
    std::string path = getTuxPath(filename);
    if (!std::filesystem::exists(path)) { colorcout("RED","Not found: "+filename+"\n"); return; }
    auto [oldContent, meta] = readFullTuxFile(path);
    if (!g_lastTuxReadOk) { colorcout("RED","File corrupted, abort editing\n\n"); return; }
    if (meta.creator != currentUser.name && !hasPrivilege()) {
        colorcout("RED","Access denied: You are not the creator of this file\n");
        return;
    }

    // Use Files\temp directory
    std::string tempDir = "Files\\temp";
    { std::error_code ec; std::filesystem::create_directories(tempDir, ec); }

    // Seeded RNG
    static std::mt19937 rng(
        static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> hexDist(0, 15);
    std::uniform_int_distribution<int> realIdxDist(0, 4);
    std::uniform_int_distribution<int> charDist(32, 126);
    std::uniform_int_distribution<int> lenDist(80, 320);

    auto genHexName = [&]() -> std::string {
        std::string name(16, '0');
        for (auto& c : name) { int v = hexDist(rng); c = v < 10 ? ('0'+v) : ('a'+v-10); }
        return tempDir + "\\" + name;
    };

    // Generate 5 unique random temp paths (no extension)
    std::vector<std::string> tempPaths;
    for (int i = 0; i < 5; ++i) {
        std::string p;
        do { p = genHexName(); } while (std::filesystem::exists(p));
        tempPaths.push_back(p);
    }

    // Choose which file holds the real content
    int realIdx = realIdxDist(rng);

    // Write all 5 files: one real, four decoys with random printable text
    for (int i = 0; i < 5; ++i) {
        std::ofstream tf(tempPaths[i]);
        if (i == realIdx) {
            tf << oldContent;
        } else {
            int len = lenDist(rng);
            for (int j = 0; j < len; ++j) {
                tf << static_cast<char>(charDist(rng));
                if (j % 40 == 39) tf << '\n';
            }
        }
    }

    // Open the real temp file in the full editor, showing the original filename
    run_editor(tempPaths[realIdx], filename);

    // Read back the (possibly edited) content
    std::string newContent;
    {
        std::ifstream tf(tempPaths[realIdx]);
        if (tf) {
            std::ostringstream oss;
            oss << tf.rdbuf();
            newContent = oss.str();
        } else {
            colorcout("RED","Failed to read temp file\n\n");
            for (auto& p : tempPaths) { std::error_code ec; std::filesystem::remove(p, ec); }
            return;
        }
    }

    // Write back to TUX only if content changed
    if (newContent != oldContent) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        meta.lastEditor = currentUser.name;
        meta.modifyTime = now;
        writeTuxFile(path, newContent, meta);
    }

    // Delete all temp files
    for (auto& p : tempPaths) { std::error_code ec; std::filesystem::remove(p, ec); }
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
    if (!isValidPath(oldname) || !isValidPath(newname)) {
        colorcout("YELLOW","Invalid filename. Use alphanumeric, '-', '_', and '/' for subfolders.\n");
        return;
    }
    std::string op = getTuxPath(oldname), np = getTuxPath(newname);
    if (!std::filesystem::exists(op)) { colorcout("RED","Not found: "+oldname+"\n"); return; }
    if (std::filesystem::exists(np)) { colorcout("RED","Target already exists: "+newname+"\n"); return; }
    // Create target parent directories if needed
    std::filesystem::path nfp(np);
    if (nfp.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(nfp.parent_path(), ec);
        if (ec) { colorcout("RED","Failed to create directory: "+ec.message()+"\n"); return; }
    }
    std::filesystem::rename(op,np);
    colorcout("GREEN","Renamed: "+oldname+" -> "+newname+"\n\n");
}

// ---------- Make Directory ----------
void makeTuxDir(const std::string& dirname) {
    if (dirname.empty()) { colorcout("RED","Usage: mkdir <dirname>\n"); return; }
    if (!isValidPath(dirname)) {
        colorcout("YELLOW","Invalid name. Use alphanumeric, '-', '_', and '/' for nested dirs.\n"); return;
    }
    std::string normalized = dirname;
    for (char& c : normalized) if (c == '/') c = '\\';
    std::string path = "Files\\" + normalized;
    if (std::filesystem::exists(path)) { colorcout("YELLOW","Already exists: "+dirname+"\n"); return; }
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) colorcout("RED","Failed to create directory: "+ec.message()+"\n");
    else colorcout("GREEN","Created directory: "+dirname+"\n\n");
}

// ---------- Remove Directory ----------
void removeTuxDir(const std::string& dirname) {
    if (dirname.empty()) { colorcout("RED","Usage: rmdir <dirname>\n"); return; }
    if (!isValidPath(dirname)) {
        colorcout("YELLOW","Invalid name. Use alphanumeric, '-', '_', and '/' for nested dirs.\n"); return;
    }
    std::string normalized = dirname;
    for (char& c : normalized) if (c == '/') c = '\\';
    std::string path = "Files\\" + normalized;
    if (!std::filesystem::exists(path)) { colorcout("RED","Not found: "+dirname+"\n"); return; }
    if (!std::filesystem::is_directory(path)) { colorcout("RED","Not a directory: "+dirname+"\n"); return; }
    if (!std::filesystem::is_empty(path)) {
        if (!getYN("Directory is not empty, remove all contents?")) {
            colorcout("YELLOW","Cancelled\n\n"); return;
        }
    }
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec) colorcout("RED","Failed to remove: "+ec.message()+"\n");
    else colorcout("GREEN","Removed directory: "+dirname+"\n\n");
}

// ---------- Copy ----------
void copyTuxFile(const std::string& src, const std::string& dst) {
    if (src.empty() || dst.empty()) { colorcout("RED","Usage: cp <src> <dst>\n"); return; }
    if (!isValidPath(src)) { colorcout("YELLOW","Invalid source path.\n"); return; }
    std::string srcPath = getTuxPath(src);
    if (!std::filesystem::exists(srcPath)) { colorcout("RED","Not found: "+src+"\n"); return; }
    auto [content, meta] = readFullTuxFile(srcPath);
    if (!g_lastTuxReadOk) { colorcout("RED","File corrupted, copy aborted\n\n"); return; }

    // If dst is an existing directory, copy into it with same filename
    std::string dstNorm = dst;
    for (char& c : dstNorm) if (c == '/') c = '\\';
    std::string dstAsDir = "Files\\" + dstNorm;
    std::string dstPath, displayDst;
    if (std::filesystem::is_directory(dstAsDir)) {
        std::string stem = std::filesystem::path(srcPath).stem().string();
        dstPath = dstAsDir + "\\" + stem + ".TUX";
        displayDst = dst + "/" + stem;
    } else {
        if (!isValidPath(dst)) { colorcout("YELLOW","Invalid destination path.\n"); return; }
        dstPath = getTuxPath(dst);
        displayDst = dst;
    }

    if (std::filesystem::exists(dstPath)) {
        if (!getYN("File already exists, overwrite?")) { colorcout("YELLOW","Cancelled\n\n"); return; }
    }
    std::filesystem::path fp(dstPath);
    if (fp.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(fp.parent_path(), ec);
        if (ec) { colorcout("RED","Failed to create directory: "+ec.message()+"\n"); return; }
    }
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    meta.creator = currentUser.name;
    meta.lastEditor = currentUser.name;
    meta.createTime = now;
    meta.modifyTime = now;
    writeTuxFile(dstPath, content, meta);
    colorcout("GREEN","Copied: "+src+" -> "+displayDst+"\n\n");
}

// ---------- Move ----------
void moveTuxFile(const std::string& src, const std::string& dst) {
    if (src.empty() || dst.empty()) { colorcout("RED","Usage: mv <src> <dst>\n"); return; }
    if (!isValidPath(src)) { colorcout("YELLOW","Invalid source path.\n"); return; }
    std::string srcPath = getTuxPath(src);
    if (!std::filesystem::exists(srcPath)) { colorcout("RED","Not found: "+src+"\n"); return; }

    // If dst is an existing directory, move into it with same filename
    std::string dstNorm = dst;
    for (char& c : dstNorm) if (c == '/') c = '\\';
    std::string dstAsDir = "Files\\" + dstNorm;
    std::string dstPath, displayDst;
    if (std::filesystem::is_directory(dstAsDir)) {
        std::string basename = std::filesystem::path(srcPath).filename().string();
        dstPath = dstAsDir + "\\" + basename;
        displayDst = dst + "/" + std::filesystem::path(srcPath).stem().string();
    } else {
        if (!isValidPath(dst)) { colorcout("YELLOW","Invalid destination path.\n"); return; }
        dstPath = getTuxPath(dst);
        displayDst = dst;
    }

    if (std::filesystem::exists(dstPath)) { colorcout("RED","Target already exists: "+dst+"\n"); return; }
    std::filesystem::path nfp(dstPath);
    if (nfp.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(nfp.parent_path(), ec);
        if (ec) { colorcout("RED","Failed to create directory: "+ec.message()+"\n"); return; }
    }
    std::error_code ec;
    std::filesystem::rename(srcPath, dstPath, ec);
    if (ec) { colorcout("RED","Move failed: "+ec.message()+"\n"); return; }
    colorcout("GREEN","Moved: "+src+" -> "+displayDst+"\n\n");
}

// ---------- Find ----------
void findTuxFiles(const std::string& pattern) {
    if (pattern.empty()) { colorcout("RED","Usage: find <pattern>\n"); return; }
    const std::string root = "Files";
    if (!std::filesystem::exists(root)) { colorcout("YELLOW","(Files directory does not exist)\n\n"); return; }

    std::string lowerPat = pattern;
    std::transform(lowerPat.begin(), lowerPat.end(), lowerPat.begin(), ::tolower);

    std::vector<std::string> results;
    std::function<void(const std::filesystem::path&, const std::string&)> walk;
    walk = [&](const std::filesystem::path& dir, const std::string& prefix) {
        for (auto& e : std::filesystem::directory_iterator(dir)) {
            if (e.is_directory() && e.path().filename() == "temp") continue;
            if (e.is_directory()) {
                walk(e.path(), prefix + e.path().filename().string() + "/");
            } else if (e.is_regular_file() && e.path().extension() == ".TUX") {
                std::string stem = e.path().stem().string();
                std::string lowerStem = stem;
                std::transform(lowerStem.begin(), lowerStem.end(), lowerStem.begin(), ::tolower);
                if (lowerStem.find(lowerPat) != std::string::npos)
                    results.push_back(prefix + stem);
            }
        }
    };
    walk(root, "");

    if (results.empty()) {
        colorcout("YELLOW","No files found matching: "+pattern+"\n\n");
    } else {
        colorcout("CYAN","Found "+std::to_string(results.size())+" file(s):\n");
        for (auto& r : results) colorcout("white","  "+r+"\n");
        std::cout << "\n";
    }
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
    colorcout("CYAN","=== Help ===\n\n");
    colorcout("CYAN","File Operations:\n");
    colorcout("white","  ls                    - list files\n");
    colorcout("white","  touch/new <f>         - create file\n");
    colorcout("white","  edit/open <f>         - edit file\n");
    colorcout("white","  cat/view <f>          - view file content\n");
    colorcout("white","  rm/del <f>            - delete file\n");
    colorcout("white","  cp <src> <dst>        - copy file\n");
    colorcout("white","  cp <f1> [f2..] <dir>  - copy multiple files to directory\n");
    colorcout("white","  mv <src> <dst>        - move / rename file\n");
    colorcout("white","  mv <f1> [f2..] <dir>  - move multiple files to directory\n");
    colorcout("white","  find <pattern>        - search files by name\n\n");
    colorcout("CYAN","Directory Operations:\n");
    colorcout("white","  mkdir <d>             - create directory\n");
    colorcout("white","  rmdir <d>             - remove directory\n\n");
    colorcout("CYAN","Privileged Operations:\n");
    colorcout("white","  meta <f>              - view file metadata\n");
    colorcout("white","  export <f>            - export file to .txt\n");
    colorcout("white","  import <f>            - import .txt as TUX file\n\n");
    colorcout("CYAN","General:\n");
    colorcout("white","  help / h / ?          - show this help\n");
    colorcout("white","  quit / q              - quit\n\n");
    colorcout("YELLOW","Tip: Use '/' for subdirectories, e.g. 'touch docs/readme'\n\n");
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
        if (cmd=="q" || cmd=="quit" || cmd=="exit") break;
        else if (cmd=="h" || cmd=="help" || cmd=="?") showHelp();
        else if (cmd=="ls" || cmd=="list" || cmd=="ll") listTuxFiles();
        else if (cmd=="c" || cmd=="create" || cmd=="touch" || cmd=="new") { std::string f; std::getline(iss>>std::ws, f); createTuxFile(f); }
        else if (cmd=="e" || cmd=="edit" || cmd=="open") { std::string f; std::getline(iss>>std::ws, f); editTuxFile(f); }
        else if (cmd=="v" || cmd=="view" || cmd=="cat" || cmd=="read") { std::string f; std::getline(iss>>std::ws, f); viewTuxFile(f); }
        else if (cmd=="rm" || cmd=="del" || cmd=="d" || cmd=="delete" || cmd=="remove") { std::string f; std::getline(iss>>std::ws, f); deleteTuxFile(f); }
        else if (cmd=="mv" || cmd=="move") {
            std::vector<std::string> args;
            std::string a; while (iss >> a) args.push_back(a);
            if (args.size() < 2) { colorcout("RED","Usage: mv <src> [src2..] <dst>\n"); }
            else if (args.size() == 2) { moveTuxFile(args[0], args[1]); }
            else {
                const std::string& dstDir = args.back();
                std::string dn = dstDir; for (char& c : dn) if (c=='/') c='\\';
                if (!std::filesystem::is_directory("Files\\"+dn)) { colorcout("RED","Destination must be an existing directory for batch move: "+dstDir+"\n"); }
                else { for (size_t i = 0; i+1 < args.size(); ++i) moveTuxFile(args[i], dstDir); }
            }
        }
        else if (cmd=="cp" || cmd=="copy") {
            std::vector<std::string> args;
            std::string a; while (iss >> a) args.push_back(a);
            if (args.size() < 2) { colorcout("RED","Usage: cp <src> [src2..] <dst>\n"); }
            else if (args.size() == 2) { copyTuxFile(args[0], args[1]); }
            else {
                const std::string& dstDir = args.back();
                std::string dn = dstDir; for (char& c : dn) if (c=='/') c='\\';
                if (!std::filesystem::is_directory("Files\\"+dn)) { colorcout("RED","Destination must be an existing directory for batch copy: "+dstDir+"\n"); }
                else { for (size_t i = 0; i+1 < args.size(); ++i) copyTuxFile(args[i], dstDir); }
            }
        }
        else if (cmd=="rn" || cmd=="rename") { std::string a,b; iss>>a>>b; renameTuxFile(a,b); }
        else if (cmd=="find" || cmd=="search") { std::string p; std::getline(iss>>std::ws, p); findTuxFiles(p); }
        else if (cmd=="mkdir" || cmd=="md") { std::string d; std::getline(iss>>std::ws, d); makeTuxDir(d); }
        else if (cmd=="rmdir" || cmd=="rd") { std::string d; std::getline(iss>>std::ws, d); removeTuxDir(d); }
        else if (cmd=="ex" || cmd=="export") {
            if (!hasPrivilege()) { colorcout("RED","Access denied: You don't have the required privileges\n"); continue; }
            std::string f; std::getline(iss>>std::ws, f); exportTuxFile(f);
        }
        else if (cmd=="im" || cmd=="import") {
            if (!hasPrivilege()) { colorcout("RED","Access denied: You don't have the required privileges\n"); continue; }
            std::string f; std::getline(iss>>std::ws, f); importTxtFile(f);
        }
        else if (cmd=="m" || cmd=="meta" || cmd=="metadata" || cmd=="info") {
            if (!hasPrivilege()) { colorcout("RED","Access denied: You don't have the required privileges\n"); continue; }
            std::string f; std::getline(iss>>std::ws, f); viewMetadata(f);
        }
        else { colorcout("RED","Unknown command: "+cmd+"\n"); }
    }
    colorcout("green", "Program exited\n");
}