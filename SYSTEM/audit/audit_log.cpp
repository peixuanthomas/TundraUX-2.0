#include "audit_log.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#include "editor.hpp"

namespace tundraux::audit {
namespace {

constexpr const char kTlogHeader[] = "TLOG1";
constexpr std::size_t kTlogHeaderSize = 5;
constexpr std::uintmax_t kMaxTlogFileSize = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxTlogRecordSize = 1024ULL * 1024ULL;

bool g_initialized = false;
bool g_strictModeEnabled = false;
USER g_currentUser = {"guest", "", "", "", 0};
std::filesystem::path g_startupLogPath;

class ScopedFileRemoval {
public:
    explicit ScopedFileRemoval(std::filesystem::path path)
        : path_(path) {}

    ~ScopedFileRemoval() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    ScopedFileRemoval(const ScopedFileRemoval&) = delete;
    ScopedFileRemoval& operator=(const ScopedFileRemoval&) = delete;

private:
    std::filesystem::path path_;
};

class ScopedCurrentUserRestore {
public:
    ScopedCurrentUserRestore()
        : previous_(g_currentUser) {}

    ~ScopedCurrentUserRestore() {
        g_currentUser = previous_;
    }

    ScopedCurrentUserRestore(const ScopedCurrentUserRestore&) = delete;
    ScopedCurrentUserRestore& operator=(const ScopedCurrentUserRestore&) = delete;

private:
    USER previous_;
};

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::filesystem::path normalizeBoundaryPath(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
    if (error) {
        error.clear();
        normalized = std::filesystem::absolute(path, error);
    }
    if (error) {
        normalized = path;
    }
    return normalized.lexically_normal();
}

std::wstring normalizedPathPart(const std::filesystem::path& path) {
    std::wstring value = path.wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        if (ch == L'/') {
            return L'\\';
        }
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool isPathInsideRoot(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    const std::filesystem::path candidatePath = normalizeBoundaryPath(candidate);
    const std::filesystem::path rootPath = normalizeBoundaryPath(root);
    auto candidateIt = candidatePath.begin();
    for (auto rootIt = rootPath.begin(); rootIt != rootPath.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == candidatePath.end()) {
            return false;
        }
        if (normalizedPathPart(*candidateIt) != normalizedPathPart(*rootIt)) {
            return false;
        }
    }
    return true;
}

std::filesystem::path logsRootPath() {
    std::error_code error;
    const std::filesystem::path current = std::filesystem::current_path(error);
    const std::filesystem::path base = error ? std::filesystem::path(".") : current;
    return std::filesystem::absolute(base / "Logs", error).lexically_normal();
}

std::string currentTimestampForFilename() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &nowTime);
    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y%m%d-%H%M%S");
    return oss.str();
}

std::string currentTimestampForLine() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &nowTime);
    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

bool readExact(std::ifstream& in, void* data, std::size_t size) {
    in.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(in);
}

bool ensureLogFileWithHeader(const std::filesystem::path& filePath) {
    try {
        std::error_code error;
        std::filesystem::create_directories(filePath.parent_path(), error);
        if (error) {
            return false;
        }

        if (!std::filesystem::exists(filePath, error)) {
            std::ofstream create(filePath, std::ios::binary | std::ios::trunc);
            if (!create) {
                return false;
            }
            create.write(kTlogHeader, static_cast<std::streamsize>(kTlogHeaderSize));
            return static_cast<bool>(create);
        }

        std::ifstream in(filePath, std::ios::binary);
        if (!in) {
            return false;
        }
        char header[kTlogHeaderSize] = {};
        if (!readExact(in, header, kTlogHeaderSize)) {
            return false;
        }
        return std::equal(header, header + kTlogHeaderSize, kTlogHeader);
    } catch (...) {
        return false;
    }
}

void appendEncryptedLine(const std::filesystem::path& filePath, const std::string& line) {
    try {
        if (line.size() > kMaxTlogRecordSize) {
            return;
        }
        if (!ensureLogFileWithHeader(filePath)) {
            return;
        }

        std::error_code sizeError;
        const std::uintmax_t currentSize = std::filesystem::file_size(filePath, sizeError);
        if (sizeError) {
            return;
        }

        const std::string encrypted = encryptDecrypt(line);
        const std::size_t encryptedLength = encrypted.size();
        const std::uintmax_t required =
            static_cast<std::uintmax_t>(sizeof(std::size_t) + encryptedLength);
        if (currentSize > kMaxTlogFileSize || (kMaxTlogFileSize - currentSize) < required) {
            return;
        }

        const std::size_t length = encrypted.size();
        std::ofstream out(filePath, std::ios::binary | std::ios::app);
        if (!out) {
            return;
        }
        out.write(reinterpret_cast<const char*>(&length), sizeof(length));
        if (length > 0) {
            out.write(encrypted.data(), static_cast<std::streamsize>(length));
        }
    } catch (...) {
        return;
    }
}

std::string userFieldValue(const std::string& value) {
    return value.empty() ? "(none)" : value;
}

} // namespace

void initialize() {
    if (g_initialized) {
        return;
    }
    try {
        g_startupLogPath = logsRootPath() /
                           ("audit-" + currentTimestampForFilename() + ".tlog");
    } catch (...) {
        g_startupLogPath.clear();
    }
    refreshStrictMode();
    g_initialized = true;
}

void refreshStrictMode() {
    try {
        DataManager dataManager("user_data.dat");
        g_strictModeEnabled = dataManager.GetStrictMode();
    } catch (...) {
        g_strictModeEnabled = false;
    }
}

void setCurrentUser(const USER& user) {
    g_currentUser = user;
}

bool isStrictModeEnabled() {
    return g_strictModeEnabled;
}

std::filesystem::path startupLogPath() {
    if (!g_initialized) {
        initialize();
    }
    return g_startupLogPath;
}

void logEvent(const std::string& category, const std::string& detail) {
    if (!g_initialized) {
        initialize();
    }
    if (!g_strictModeEnabled || g_startupLogPath.empty()) {
        return;
    }

    try {
        const std::string line =
            currentTimestampForLine() + " | user=" + userFieldValue(g_currentUser.name) +
            " | type=" + userFieldValue(g_currentUser.type) + " | " + category + " | " + detail;
        appendEncryptedLine(g_startupLogPath, line);
    } catch (...) {
        return;
    }
}

void logKeyPress(const tundra_tui::KeyPress& key, bool sensitive) {
    (void)sensitive;
    std::string detail;
    switch (key.key) {
        case tundra_tui::Key::Character: {
            const unsigned char ch = static_cast<unsigned char>(key.character);
            if (ch >= 1 && ch <= 26) {
                detail = "Ctrl+";
                detail.push_back(static_cast<char>('A' + (ch - 1)));
            } else if (std::isprint(ch)) {
                if (ch == '\\') {
                    detail = "Character '\\\\'";
                } else if (ch == '\'') {
                    detail = "Character \"'\"";
                } else {
                    detail = "Character '";
                    detail.push_back(static_cast<char>(ch));
                    detail.push_back('\'');
                }
            } else {
                detail = "Unknown";
            }
            break;
        }
        case tundra_tui::Key::Enter:
            detail = "Enter";
            break;
        case tundra_tui::Key::Escape:
            detail = "Escape";
            break;
        case tundra_tui::Key::Backspace:
            detail = "Backspace";
            break;
        case tundra_tui::Key::Delete:
            detail = "Delete";
            break;
        case tundra_tui::Key::Tab:
            detail = "Tab";
            break;
        case tundra_tui::Key::Up:
            detail = "Up";
            break;
        case tundra_tui::Key::Down:
            detail = "Down";
            break;
        case tundra_tui::Key::Left:
            detail = "Left";
            break;
        case tundra_tui::Key::Right:
            detail = "Right";
            break;
        case tundra_tui::Key::Home:
            detail = "Home";
            break;
        case tundra_tui::Key::End:
            detail = "End";
            break;
        case tundra_tui::Key::PageUp:
            detail = "PageUp";
            break;
        case tundra_tui::Key::PageDown:
            detail = "PageDown";
            break;
        case tundra_tui::Key::F1:
            detail = "F1";
            break;
        case tundra_tui::Key::F2:
            detail = "F2";
            break;
        default:
            detail = "Unknown";
            break;
    }
    logEvent("key", detail);
}

bool isPrivileged(const std::string& usertype) {
    const std::string normalized = lowerCopy(usertype);
    return normalized == "admin" || normalized == "debug";
}

std::vector<std::string> readTlogPlaintext(const std::filesystem::path& path, std::string& error) {
    std::vector<std::string> lines;
    error.clear();

    try {
        std::error_code stateError;
        if (!std::filesystem::exists(path, stateError) || stateError) {
            error = "File does not exist.";
            return lines;
        }

        std::error_code sizeError;
        const std::uintmax_t fileSize = std::filesystem::file_size(path, sizeError);
        if (sizeError) {
            error = "Unable to read file size.";
            return lines;
        }
        if (fileSize > kMaxTlogFileSize) {
            error = "File exceeds maximum size.";
            return lines;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in) {
            error = "Unable to open file.";
            return lines;
        }

        char header[kTlogHeaderSize] = {};
        if (!readExact(in, header, kTlogHeaderSize) ||
            !std::equal(header, header + kTlogHeaderSize, kTlogHeader)) {
            error = "Invalid TLOG header.";
            lines.clear();
            return lines;
        }

        while (true) {
            std::size_t length = 0;
            in.read(reinterpret_cast<char*>(&length), sizeof(length));
            if (!in) {
                if (in.eof() && in.gcount() == 0) {
                    in.clear();
                    break;
                }
                error = "Corrupted record length.";
                lines.clear();
                return lines;
            }

            if (length > kMaxTlogRecordSize) {
                error = "Record exceeds maximum size.";
                lines.clear();
                return lines;
            }

            std::string encrypted(length, '\0');
            if (length > 0) {
                in.read(&encrypted[0], static_cast<std::streamsize>(length));
                if (!in) {
                    error = "Truncated TLOG record.";
                    lines.clear();
                    return lines;
                }
            }

            lines.push_back(encryptDecrypt(encrypted));
        }
    } catch (...) {
        error = "Failed to read TLOG.";
        lines.clear();
    }

    return lines;
}

int openTlogInEditor(
    const std::string& path,
    const std::string& displayName,
    const std::string& username,
    const std::string& usertype
) {
    (void)username;
    if (!isPrivileged(usertype)) {
        return 3;
    }

    std::string readError;
    const std::vector<std::string> lines = readTlogPlaintext(path, readError);
    if (!readError.empty()) {
        return 2;
    }

    try {
        const std::filesystem::path sourcePath(path);
        std::filesystem::path tempDir = std::filesystem::path("Files") / "temp";
        std::error_code dirError;
        std::filesystem::create_directories(tempDir, dirError);
        if (dirError) {
            return 4;
        }

        std::string stem = sourcePath.stem().string();
        if (stem.empty()) {
            stem = "audit";
        }
        std::filesystem::path tempPath = tempDir / (stem + "-view.log");
        ScopedFileRemoval tempFileGuard(tempPath);

        {
            std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
            if (!out) {
                return 4;
            }
            for (std::size_t i = 0; i < lines.size(); ++i) {
                out << lines[i];
                if (i + 1 < lines.size()) {
                    out << '\n';
                }
            }
            if (!out) {
                return 4;
            }
        }

        const std::string effectiveDisplayName =
            displayName.empty() ? sourcePath.filename().string() : displayName;
        return run_editor(tempPath.string(), effectiveDisplayName);
    } catch (...) {
        return 4;
    }
}

bool exportTlogToPlaintext(
    const std::string& inputPath,
    const std::string& username,
    const std::string& usertype,
    std::string& message
) {
    message.clear();

    if (!isPrivileged(usertype)) {
        message = "Access denied.";
        return false;
    }

    try {
        const std::filesystem::path logsRoot = logsRootPath();
        const std::filesystem::path inputCandidate(inputPath);
        const std::filesystem::path resolvedInput = normalizeBoundaryPath(inputCandidate);

        if (lowerCopy(resolvedInput.extension().string()) != ".tlog") {
            message = "Input must have .tlog extension.";
            return false;
        }

        if (!isPathInsideRoot(resolvedInput, logsRoot)) {
            message = "Input path is outside Logs directory.";
            return false;
        }

        std::string error;
        const std::vector<std::string> lines = readTlogPlaintext(resolvedInput, error);
        if (!error.empty()) {
            message = error;
            return false;
        }

        const std::filesystem::path outputDir =
            std::filesystem::path("Files") / "exported_logs";
        std::error_code dirError;
        std::filesystem::create_directories(outputDir, dirError);
        if (dirError) {
            message = "Failed to create export directory.";
            return false;
        }

        const std::filesystem::path outputPath =
            outputDir / (resolvedInput.stem().string() + ".log");

        std::error_code existsError;
        if (std::filesystem::exists(outputPath, existsError) && !existsError) {
            message = "Export target already exists.";
            return false;
        }
        if (existsError) {
            message = "Failed to inspect export target.";
            return false;
        }

        std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            message = "Failed to write export file.";
            return false;
        }
        for (std::size_t i = 0; i < lines.size(); ++i) {
            out << lines[i];
            if (i + 1 < lines.size()) {
                out << '\n';
            }
        }
        if (!out) {
            message = "Failed while writing export file.";
            return false;
        }

        {
            ScopedCurrentUserRestore restoreUser;
            g_currentUser = {usertype, username, "", "", 0};
            logEvent(
                "audit",
                "Exported TLOG " + resolvedInput.filename().string() + " -> " +
                    outputPath.filename().string());
        }

        message = "Exported to " + outputPath.string();
        return true;
    } catch (...) {
        message = "Failed to export TLOG.";
        return false;
    }
}

} // namespace tundraux::audit
