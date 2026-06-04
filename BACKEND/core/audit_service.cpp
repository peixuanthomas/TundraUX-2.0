#include "audit_service.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <cstdint>
#include <sstream>
#include <system_error>
#include <vector>

namespace tundraux::backend {
namespace {

constexpr const char* kTlogHeader = "TLOG1";
constexpr std::size_t kTlogHeaderSize = 5;
constexpr std::uint64_t kDefaultLogRecordLengthType = sizeof(std::uint64_t);
constexpr std::uintmax_t kMaxTlogFileSize = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxTlogRecordSize = 1024ULL * 1024ULL;
constexpr const char* kLogFileName = "audit.tlog";
constexpr const char* kAccessDeniedMessage = "Access denied.";
constexpr const char* kStorageErrorMessage = "Storage error.";
constexpr const char* kReadUserDataError = "Unable to read user data.";
constexpr const char* kInvalidPathMessage = "Invalid tlog path.";
constexpr const char* kTlogNotFoundMessage = "TLOG file not found.";
constexpr char kCipherKey[] = "TundraUX2";
constexpr char kKeyPrefix[] = "Character '";
constexpr char kKeySuffix[] = "'";
constexpr char kRedactedKeyDetail[] = "Character [redacted]";

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string currentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    if (localtime_s(&localTime, &value) != 0) {
        return "1970-01-01 00:00:00";
    }
#else
    if (localtime_r(&value, &localTime) == nullptr) {
        return "1970-01-01 00:00:00";
    }
#endif
    std::ostringstream out;
    out << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string obfuscateCopy(std::string value) {
    if (value.empty()) {
        return value;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        value[i] ^= kCipherKey[i % (sizeof(kCipherKey) - 1)];
    }
    return value;
}

std::string legacyObfuscateCopy(std::string value) {
    for (char& ch : value) {
        ch ^= 0x55;
    }
    return value;
}

bool looksLikeAuditLine(const std::string& value) {
    return value.size() >= 19 &&
        value[4] == '-' &&
        value[7] == '-' &&
        value[10] == ' ' &&
        value[13] == ':' &&
        value[16] == ':' &&
        value.find(" | user=") != std::string::npos &&
        value.find(" | type=") != std::string::npos;
}

std::string decodeAuditPayload(const std::string& payload) {
    const std::string current = obfuscateCopy(payload);
    if (looksLikeAuditLine(current)) {
        return current;
    }

    const std::string legacy = legacyObfuscateCopy(payload);
    if (looksLikeAuditLine(legacy)) {
        return legacy;
    }

    return current;
}

std::string trimCopy(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

std::string escapeControlCharacters(std::string value) {
    constexpr const char* hex = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(value.size());

    for (const unsigned char ch : value) {
        switch (ch) {
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (std::iscntrl(ch)) {
                escaped.push_back('\\');
                escaped.push_back('u');
                escaped.push_back('0');
                escaped.push_back('0');
                escaped.push_back(hex[(ch >> 4) & 0x0F]);
                escaped.push_back(hex[ch & 0x0F]);
            } else {
                escaped.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return escaped;
}

} // namespace

AuditService::AuditService(UserStore& users, const SessionService& sessions, std::string logsRoot)
    : users_(users), sessions_(sessions), logsRoot_(std::move(logsRoot)) {}

bool AuditService::isSyntheticDebug(const BackendUser& user) {
    return lowerCopy(user.type) == "debug" && user.name == "debug" && user.password.empty();
}

bool AuditService::isPrivileged(const BackendUser& user) {
    const std::string type = lowerCopy(user.type);
    return type == "admin" || type == "debug";
}

std::string AuditService::sanitizeKey(const std::string& key) {
    if (key.empty()) {
        return "Unknown";
    }
    if (key.size() == 1) {
        const unsigned char ch = static_cast<unsigned char>(key[0]);
        if (std::isprint(ch)) {
            if (key[0] == '\\') {
                return "Character '\\\\'";
            }
            if (key[0] == '"') {
                return "Character '\"'";
            }
            std::string detail = kKeyPrefix;
            detail.push_back(key[0]);
            detail += kKeySuffix;
            return detail;
        }
    }
    return key;
}

std::string AuditService::formatRecordLine(
    const std::string& category,
    const BackendUser& user,
    const std::string& detail
) {
    const auto username = user.name.empty() ? std::string("(none)") : user.name;
    const auto type = user.type.empty() ? std::string("(none)") : user.type;
    return currentTimestamp() + " | user=" + username + " | type=" + type +
        " | " + category + " | " + detail;
}

std::string AuditService::obfuscate(const std::string& value) {
    return obfuscateCopy(value);
}

std::string AuditService::deobfuscate(const std::string& value) {
    return obfuscateCopy(value);
}

bool AuditService::isPathInsideRoot(
    std::filesystem::path candidate,
    const std::filesystem::path& root
) {
    std::error_code error;
    candidate = std::filesystem::weakly_canonical(candidate, error);
    if (error) {
        candidate = std::filesystem::absolute(candidate, error).lexically_normal();
        if (error) {
            return false;
        }
    }

    auto normalizedRoot = std::filesystem::weakly_canonical(root, error);
    if (error) {
        normalizedRoot = std::filesystem::absolute(root, error).lexically_normal();
        if (error) {
            return false;
        }
    }

    if (candidate == normalizedRoot) {
        return false;
    }
    const auto relative = candidate.lexically_relative(normalizedRoot);
    for (const auto& part : relative) {
        if (part == "..") {
            return false;
        }
    }
    return !relative.empty();
}

ServiceResult<std::filesystem::path> AuditService::resolveLogPath(const std::string& path) const {
    if (path.empty()) {
        return ServiceResult<std::filesystem::path>::failure(ErrorCode::InvalidPath, kInvalidPathMessage);
    }
    if (std::filesystem::path(path).is_absolute()) {
        return ServiceResult<std::filesystem::path>::failure(ErrorCode::InvalidPath, kInvalidPathMessage);
    }

    const std::filesystem::path logsRoot(logsRoot_);
    const std::filesystem::path candidate = logsRoot / path;
    if (!isPathInsideRoot(candidate, logsRoot) || lowerCopy(candidate.extension().string()) != ".tlog") {
        return ServiceResult<std::filesystem::path>::failure(ErrorCode::InvalidPath, kInvalidPathMessage);
    }
    return ServiceResult<std::filesystem::path>::success(std::filesystem::absolute(candidate));
}

ServiceResult<BackendUser> AuditService::resolveSessionUser(const std::string& sessionId) const {
    const auto session = sessions_.requireSession(sessionId);
    if (!session.ok) {
        return ServiceResult<BackendUser>::failure(session.error.code, session.error.message);
    }
    if (session.value.type == "guest" || session.value.name.empty()) {
        return ServiceResult<BackendUser>::failure(ErrorCode::PermissionDenied, kAccessDeniedMessage);
    }
    if (isSyntheticDebug(session.value)) {
        return ServiceResult<BackendUser>::success(session.value);
    }

    std::vector<BackendUser> users;
    try {
        users = users_.listUsers();
    } catch (const std::exception&) {
        return ServiceResult<BackendUser>::failure(ErrorCode::StorageError, kReadUserDataError);
    }

    const auto found = std::find_if(users.begin(), users.end(), [&](const BackendUser& user) {
        return user.name == session.value.name;
    });
    if (found == users.end()) {
        return ServiceResult<BackendUser>::failure(ErrorCode::NotFound, "User not found.");
    }
    if (found->failedCount > 7) {
        return ServiceResult<BackendUser>::failure(ErrorCode::PermissionDenied, kAccessDeniedMessage);
    }
    return ServiceResult<BackendUser>::success(*found);
}

ServiceResult<BackendUser> AuditService::resolveAppendUser(const std::string& sessionId) const {
    const auto session = sessions_.requireSession(sessionId);
    if (!session.ok) {
        return ServiceResult<BackendUser>::failure(session.error.code, session.error.message);
    }
    if (session.value.type == "guest" || session.value.name.empty()) {
        return ServiceResult<BackendUser>::success(session.value);
    }
    return resolveSessionUser(sessionId);
}

ServiceResult<EmptyResult> AuditService::appendRecord(
    const BackendUser& user,
    const AuditRecord& record
) {
    try {
        const std::filesystem::path logsRoot(logsRoot_);
        const std::filesystem::path logsPath = logsRoot / kLogFileName;
        std::error_code error;
        std::filesystem::create_directories(logsRoot, error);
        if (error) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
        }

        if (!std::filesystem::exists(logsPath, error)) {
            std::ofstream create(logsPath, std::ios::binary | std::ios::trunc);
            if (!create) {
                return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
            }
            create.write(kTlogHeader, static_cast<std::streamsize>(kTlogHeaderSize));
            if (!create) {
                return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
            }
        } else {
            std::ifstream existing(logsPath, std::ios::binary);
            if (!existing) {
                return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
            }
            char header[kTlogHeaderSize] = {};
            existing.read(header, static_cast<std::streamsize>(kTlogHeaderSize));
            if (!existing || !std::equal(header, header + kTlogHeaderSize, kTlogHeader)) {
                return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
            }
        }

        std::uintmax_t fileSize = std::filesystem::file_size(logsPath, error);
        if (error) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
        }

        const std::string line = formatRecordLine(
            escapeControlCharacters(record.category),
            user,
            escapeControlCharacters(record.detail)
        );
        const std::string payload = obfuscate(line);
        if (payload.size() > kMaxTlogRecordSize) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
        }

        const std::uint64_t payloadSize = static_cast<std::uint64_t>(payload.size());
        if (fileSize > kMaxTlogFileSize) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
        }
        if ((kMaxTlogFileSize - fileSize) < (kDefaultLogRecordLengthType + payloadSize)) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
        }

        std::ofstream out(logsPath, std::ios::binary | std::ios::app);
        if (!out) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
        }
        out.write(reinterpret_cast<const char*>(&payloadSize), static_cast<std::streamsize>(kDefaultLogRecordLengthType));
        if (!payload.empty()) {
            out.write(payload.data(), static_cast<std::streamsize>(payloadSize));
        }
        if (!out) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
        }
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
    } catch (...) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
    }

    return ServiceResult<EmptyResult>::success(EmptyResult{});
}

ServiceResult<AuditReadResult> AuditService::readRecords(const std::filesystem::path& absolutePath) const {
    try {
        if (!std::filesystem::exists(absolutePath)) {
            return ServiceResult<AuditReadResult>::failure(ErrorCode::NotFound, kTlogNotFoundMessage);
        }

        std::ifstream in(absolutePath, std::ios::binary);
        if (!in) {
            return ServiceResult<AuditReadResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
        }

        std::error_code error;
        const std::uintmax_t fileSize = std::filesystem::file_size(absolutePath, error);
        if (error || fileSize < kTlogHeaderSize) {
            return ServiceResult<AuditReadResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
        }
        if (fileSize > kMaxTlogFileSize) {
            return ServiceResult<AuditReadResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
        }

        char header[kTlogHeaderSize] = {};
        in.read(header, static_cast<std::streamsize>(kTlogHeaderSize));
        if (!in || !std::equal(header, header + kTlogHeaderSize, kTlogHeader)) {
            return ServiceResult<AuditReadResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
        }

        AuditReadResult lines;
        while (true) {
            std::uint64_t payloadSize = 0;
            in.read(reinterpret_cast<char*>(&payloadSize), static_cast<std::streamsize>(kDefaultLogRecordLengthType));
            if (!in) {
                if (in.eof() && in.gcount() == 0) {
                    break;
                }
                return ServiceResult<AuditReadResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
            }
            if (payloadSize > kMaxTlogRecordSize) {
                return ServiceResult<AuditReadResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
            }
            std::string payload(payloadSize, '\0');
            if (payloadSize > 0) {
                in.read(payload.data(), static_cast<std::streamsize>(payloadSize));
                if (!in) {
                    return ServiceResult<AuditReadResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
                }
            }
            lines.lines.push_back(trimCopy(decodeAuditPayload(payload)));
        }
        return ServiceResult<AuditReadResult>::success(std::move(lines));
    } catch (const std::exception&) {
        return ServiceResult<AuditReadResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
    } catch (...) {
        return ServiceResult<AuditReadResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
    }
}

ServiceResult<EmptyResult> AuditService::logEvent(
    const std::string& sessionId,
    const std::string& category,
    const std::string& detail
) {
    const auto user = resolveAppendUser(sessionId);
    if (!user.ok) {
        return ServiceResult<EmptyResult>::failure(user.error.code, user.error.message);
    }

    try {
        if (!users_.getStrictMode()) {
            return ServiceResult<EmptyResult>::success(EmptyResult{});
        }
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kStorageErrorMessage);
    }

    return appendRecord(user.value, {category, detail});
}

ServiceResult<EmptyResult> AuditService::logKeyPress(
    const std::string& sessionId,
    const std::string& key,
    bool sensitive
) {
    const auto detail = sensitive ? kRedactedKeyDetail : sanitizeKey(key);
    return logEvent(sessionId, "key", detail);
}

ServiceResult<AuditReadResult> AuditService::readTlog(const std::string& sessionId, const std::string& path) const {
    const auto user = resolveSessionUser(sessionId);
    if (!user.ok) {
        return ServiceResult<AuditReadResult>::failure(user.error.code, user.error.message);
    }
    if (!isPrivileged(user.value)) {
        return ServiceResult<AuditReadResult>::failure(ErrorCode::PermissionDenied, kAccessDeniedMessage);
    }

    const auto resolvedPath = resolveLogPath(path);
    if (!resolvedPath.ok) {
        return ServiceResult<AuditReadResult>::failure(resolvedPath.error.code, resolvedPath.error.message);
    }
    return readRecords(resolvedPath.value);
}

ServiceResult<FileContent> AuditService::exportTlog(const std::string& sessionId, const std::string& path) const {
    const auto user = resolveSessionUser(sessionId);
    if (!user.ok) {
        return ServiceResult<FileContent>::failure(user.error.code, user.error.message);
    }
    if (!isPrivileged(user.value)) {
        return ServiceResult<FileContent>::failure(ErrorCode::PermissionDenied, kAccessDeniedMessage);
    }

    const auto resolvedPath = resolveLogPath(path);
    if (!resolvedPath.ok) {
        return ServiceResult<FileContent>::failure(resolvedPath.error.code, resolvedPath.error.message);
    }

    const auto lines = readRecords(resolvedPath.value);
    if (!lines.ok) {
        return ServiceResult<FileContent>::failure(lines.error.code, lines.error.message);
    }

    std::string content;
    for (std::size_t i = 0; i < lines.value.lines.size(); ++i) {
        content += lines.value.lines[i];
        if (i + 1 < lines.value.lines.size()) {
            content += '\n';
        }
    }
    return ServiceResult<FileContent>::success(FileContent{std::move(content)});
}

} // namespace tundraux::backend
