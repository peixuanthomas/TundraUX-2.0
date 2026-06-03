#include "tux_editor.hpp"

#include "backend_facade.hpp"
#include "crypto.hpp"
#include "editor.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <windows.h>

namespace {

constexpr std::size_t kMaxTuxStringLen = 1024;
constexpr std::size_t kMaxTuxContentLen = 16 * 1024 * 1024;

struct FileMetadata {
    std::string creator;
    std::string lastEditor;
    std::time_t createTime{};
    std::time_t modifyTime{};
};

class ScopedTuxTempFiles {
public:
    explicit ScopedTuxTempFiles(std::filesystem::path tempDir)
        : tempDir_(std::move(tempDir)) {}

    void add(const std::filesystem::path& path) {
        paths_.push_back(path);
    }

    ~ScopedTuxTempFiles() {
        std::error_code error;
        for (const auto& path : paths_) {
            std::filesystem::remove(path, error);
            error.clear();
        }
        std::filesystem::remove(tempDir_, error);
    }

private:
    std::filesystem::path tempDir_;
    std::vector<std::filesystem::path> paths_;
};

class TuxReader {
public:
    TuxReader(std::ifstream& stream, uintmax_t size)
        : stream_(stream), remaining_(size) {}

    bool readExact(void* destination, std::size_t size) {
        if (remaining_ < size) {
            return false;
        }
        stream_.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(size));
        if (!stream_) {
            return false;
        }
        remaining_ -= size;
        return true;
    }

    bool readEncryptedString(std::string& value, std::size_t maxLen) {
        std::size_t len = 0;
        if (!readExact(&len, sizeof(len)) || len > maxLen || len > remaining_) {
            return false;
        }

        std::string encrypted(len, '\0');
        if (!readExact(encrypted.data(), len)) {
            return false;
        }
        value = encryptDecrypt(encrypted);
        return true;
    }

private:
    std::ifstream& stream_;
    uintmax_t remaining_;
};

bool isPrivilegedType(std::string usertype) {
    std::transform(usertype.begin(), usertype.end(), usertype.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return usertype == "debug" || usertype == "admin";
}

bool canModifyTuxMetadata(
    const FileMetadata& metadata,
    const std::string& username,
    const std::string& usertype
) {
    return isPrivilegedType(usertype) || (!username.empty() && metadata.creator == username);
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

void writeEncryptedString(std::ofstream& out, const std::string& data) {
    const std::string encrypted = encryptDecrypt(data);
    const std::size_t len = encrypted.size();
    out.write(reinterpret_cast<const char*>(&len), sizeof(len));
    out.write(encrypted.data(), static_cast<std::streamsize>(encrypted.size()));
}

void setAuditUser(
    tundraux::frontend::FrontendAuditSink* auditSink,
    const std::string& username,
    const std::string& usertype
) {
    if (auditSink == nullptr) {
        return;
    }
    auditSink->setCurrentUser({usertype, username, "", 0});
}

void logTuxOperation(
    tundraux::frontend::FrontendAuditSink* auditSink,
    const std::string& username,
    const std::string& usertype,
    const std::string& operation,
    const std::string& status,
    const std::string& path,
    const std::string& reason = ""
) {
    setAuditUser(auditSink, username, usertype);
    std::string detail = operation + " " + status + " path=" + path;
    if (!reason.empty()) {
        detail += " reason=" + reason;
    }
    if (auditSink != nullptr) {
        auditSink->logEvent("tux", detail);
    }
}

std::filesystem::path createUniqueEditorTempDir(std::error_code& error) {
    error.clear();
    std::filesystem::path base = std::filesystem::temp_directory_path(error);
    if (error) {
        return {};
    }

    base /= "TundraUX";
    std::filesystem::create_directories(base, error);
    if (error) {
        return {};
    }

    const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
        const std::filesystem::path candidate =
            base / ("tux-edit-" + std::to_string(seed) + "-" + std::to_string(attempt));
        std::filesystem::create_directory(candidate, error);
        if (!error) {
            return candidate;
        }
        if (error == std::errc::file_exists) {
            error.clear();
            continue;
        }
        return {};
    }

    error = std::make_error_code(std::errc::file_exists);
    return {};
}

bool readTempFile(const std::filesystem::path& path, std::string& content) {
    std::ifstream tempFile(path, std::ios::binary);
    if (!tempFile) {
        return false;
    }

    std::ostringstream buffer;
    buffer << tempFile.rdbuf();
    content = buffer.str();
    return !tempFile.bad();
}

bool writeTempFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream tempFile(path, std::ios::binary | std::ios::trunc);
    if (!tempFile) {
        return false;
    }
    tempFile.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(tempFile);
}

std::pair<std::string, FileMetadata> readFullTuxFile(const std::string& path, bool& ok) {
    ok = false;
    FileMetadata metadata{};
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {"", metadata};
    }

    uintmax_t fileSize = 0;
    try {
        fileSize = std::filesystem::file_size(path);
    } catch (...) {
        return {"", metadata};
    }

    TuxReader reader(in, fileSize);
    unsigned int version = 0;
    if (!reader.readExact(&version, sizeof(version)) || version != 1) {
        return {"", metadata};
    }
    if (!reader.readEncryptedString(metadata.creator, kMaxTuxStringLen)) {
        return {"", metadata};
    }
    if (!reader.readEncryptedString(metadata.lastEditor, kMaxTuxStringLen)) {
        return {"", metadata};
    }
    if (!reader.readExact(&metadata.createTime, sizeof(metadata.createTime))) {
        return {"", metadata};
    }
    if (!reader.readExact(&metadata.modifyTime, sizeof(metadata.modifyTime))) {
        return {"", metadata};
    }

    std::string content;
    if (!reader.readEncryptedString(content, kMaxTuxContentLen)) {
        return {"", metadata};
    }

    ok = true;
    return {content, metadata};
}

FileMetadata readMetadata(const std::string& path) {
    bool ok = false;
    const auto content = readFullTuxFile(path, ok);
    return ok ? content.second : FileMetadata{};
}

bool writeTuxFile(const std::string& path, const std::string& content, const FileMetadata& metadata) {
    const std::filesystem::path targetPath = std::filesystem::u8path(path);
    const std::filesystem::path tempPath = targetPath.u8string() + ".tmp";
    std::error_code removeError;
    std::filesystem::remove(tempPath, removeError);

    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    unsigned int version = 1;
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    writeEncryptedString(out, metadata.creator);
    writeEncryptedString(out, metadata.lastEditor);
    out.write(reinterpret_cast<const char*>(&metadata.createTime), sizeof(metadata.createTime));
    out.write(reinterpret_cast<const char*>(&metadata.modifyTime), sizeof(metadata.modifyTime));
    writeEncryptedString(out, content);
    out.flush();
    if (!out) {
        out.close();
        std::filesystem::remove(tempPath, removeError);
        return false;
    }

    out.close();
    if (!out) {
        std::filesystem::remove(tempPath, removeError);
        return false;
    }

    if (!MoveFileExW(
            tempPath.wstring().c_str(),
            targetPath.wstring().c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(tempPath, removeError);
        return false;
    }
    return true;
}

} // namespace

bool can_modify_tux_file(
    const std::string& tuxPath,
    const std::string& currentUsername,
    const std::string& currentUsertype
) {
    if (isPrivilegedType(currentUsertype)) {
        return true;
    }
    const FileMetadata metadata = readMetadata(tuxPath);
    if (metadata.creator.empty() && metadata.lastEditor.empty()) {
        return false;
    }
    return canModifyTuxMetadata(metadata, currentUsername, currentUsertype);
}

bool can_read_tux_file(
    const std::string& tuxPath,
    const std::string& currentUsername,
    const std::string& currentUsertype
) {
    return can_modify_tux_file(tuxPath, currentUsername, currentUsertype);
}

bool directory_has_protected_tux_files(
    const std::string& directoryPath,
    const std::string& currentUsername,
    const std::string& currentUsertype
) {
    if (isPrivilegedType(currentUsertype)) {
        return false;
    }

    std::error_code error;
    for (const auto& item : std::filesystem::recursive_directory_iterator(
             directoryPath,
             std::filesystem::directory_options::skip_permission_denied,
             error
         )) {
        if (error) {
            error.clear();
            continue;
        }

        std::error_code statusError;
        if (!item.is_directory(statusError) &&
            lowerCopy(item.path().extension().string()) == ".tux" &&
            !can_modify_tux_file(item.path().string(), currentUsername, currentUsertype)) {
            return true;
        }
        statusError.clear();
    }
    return false;
}

int open_tux_file_in_editor(
    const std::string& tuxPath,
    const std::string& displayName,
    const std::string& currentUsername,
    const std::string& currentUsertype,
    bool allowReadOnly,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    if (!std::filesystem::exists(tuxPath)) {
        logTuxOperation(auditSink, currentUsername, currentUsertype, "edit", "failure", tuxPath, "not found");
        return 1;
    }

    bool readOk = false;
    auto [oldContent, metadata] = readFullTuxFile(tuxPath, readOk);
    if (!readOk) {
        logTuxOperation(auditSink, currentUsername, currentUsertype, "edit", "failure", tuxPath, "corrupted file");
        return 2;
    }

    const bool canModify = canModifyTuxMetadata(metadata, currentUsername, currentUsertype);
    if (!canModify && !allowReadOnly) {
        logTuxOperation(auditSink, currentUsername, currentUsertype, "edit", "denied", tuxPath, "cannot modify");
        return 3;
    }

    std::error_code error;
    const std::filesystem::path tempDir = createUniqueEditorTempDir(error);
    if (error) {
        logTuxOperation(auditSink, currentUsername, currentUsertype, "edit", "failure", tuxPath, "failed to create temp directory");
        return 4;
    }
    ScopedTuxTempFiles tempCleanup(tempDir);

    static std::mt19937 rng(
        static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> hexDist(0, 15);

    std::string name(16, '0');
    for (auto& character : name) {
        const int value = hexDist(rng);
        character = value < 10
            ? static_cast<char>('0' + value)
            : static_cast<char>('a' + value - 10);
    }

    const std::filesystem::path tempPath = tempDir / (name + ".txt");
    tempCleanup.add(tempPath);
    if (!writeTempFile(tempPath, oldContent)) {
        logTuxOperation(auditSink, currentUsername, currentUsertype, "edit", "failure", tuxPath, "failed to create temp file");
        return 5;
    }

    const int editorResult = run_editor(tempPath.string(), displayName);

    std::string newContent;
    if (!readTempFile(tempPath, newContent)) {
        logTuxOperation(auditSink, currentUsername, currentUsertype, "edit", "failure", tuxPath, "failed to read temp file");
        return 6;
    }

    if (canModify && newContent != oldContent) {
        metadata.lastEditor = currentUsername;
        metadata.modifyTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        if (!writeTuxFile(tuxPath, newContent, metadata)) {
            logTuxOperation(auditSink, currentUsername, currentUsertype, "edit", "failure", tuxPath, "write failed");
            return 8;
        }
        logTuxOperation(auditSink, currentUsername, currentUsertype, "edit", "success", tuxPath);
    }

    return canModify ? editorResult : 7;
}
