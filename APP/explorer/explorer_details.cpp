#include "explorer_details.hpp"

#include "TUXfile.hpp"
#include "explorer_directory.hpp"
#include "explorer_permissions.hpp"
#include "explorer_text.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <windows.h>

namespace tundraux::explorer {
namespace {

std::string formatFileTime(const FILETIME& fileTime) {
    if (fileTime.dwLowDateTime == 0 && fileTime.dwHighDateTime == 0) {
        return "Unknown";
    }

    FILETIME localFileTime{};
    SYSTEMTIME systemTime{};
    if (!FileTimeToLocalFileTime(&fileTime, &localFileTime) ||
        !FileTimeToSystemTime(&localFileTime, &systemTime)) {
        return "Unknown";
    }

    std::ostringstream out;
    out << std::setfill('0')
        << std::setw(4) << systemTime.wYear << "-"
        << std::setw(2) << systemTime.wMonth << "-"
        << std::setw(2) << systemTime.wDay << " "
        << std::setw(2) << systemTime.wHour << ":"
        << std::setw(2) << systemTime.wMinute << ":"
        << std::setw(2) << systemTime.wSecond;
    return out.str();
}

void addSection(std::vector<DetailLine>& lines, const std::string& label) {
    lines.push_back({label, "", true});
}

void addDetail(std::vector<DetailLine>& lines, const std::string& label, const std::string& value) {
    lines.push_back({label, value, false});
}

std::string fileTypeDescription(const FileEntry& entry) {
    if (entry.isDirectory) {
        return "Directory";
    }

    const std::string extension = extensionOf(entry.path);
    if (extension == ".tux") {
        return "Encrypted TUX file";
    }
    if (extension == ".dat") {
        return "User data file";
    }
    if (extension == ".md") {
        return "Markdown text file";
    }
    if (extension == ".txt") {
        return "Plain text file";
    }
    if (extension.empty()) {
        return "File without extension";
    }
    return "File";
}

std::string fileStatusTypeName(fs::file_type type) {
    switch (type) {
        case fs::file_type::none: return "none";
        case fs::file_type::not_found: return "not found";
        case fs::file_type::regular: return "regular";
        case fs::file_type::directory: return "directory";
        case fs::file_type::symlink: return "symlink";
        case fs::file_type::block: return "block";
        case fs::file_type::character: return "character";
        case fs::file_type::fifo: return "fifo";
        case fs::file_type::socket: return "socket";
        case fs::file_type::unknown: return "unknown";
        default: return "implementation-defined";
    }
}

std::string permissionSet(fs::perms permissions, fs::perms read, fs::perms write, fs::perms exec) {
    std::string value;
    value += (permissions & read) != fs::perms::none ? 'r' : '-';
    value += (permissions & write) != fs::perms::none ? 'w' : '-';
    value += (permissions & exec) != fs::perms::none ? 'x' : '-';
    return value;
}

std::string formatPermissions(fs::perms permissions) {
    return "owner " + permissionSet(permissions, fs::perms::owner_read, fs::perms::owner_write, fs::perms::owner_exec) +
           ", group " + permissionSet(permissions, fs::perms::group_read, fs::perms::group_write, fs::perms::group_exec) +
           ", others " + permissionSet(permissions, fs::perms::others_read, fs::perms::others_write, fs::perms::others_exec);
}

std::string attributeList(DWORD attributes) {
    std::vector<std::string> names;
    auto add = [&](DWORD flag, const std::string& name) {
        if ((attributes & flag) != 0) {
            names.push_back(name);
        }
    };

    add(FILE_ATTRIBUTE_READONLY, "read-only");
    add(FILE_ATTRIBUTE_HIDDEN, "hidden");
    add(FILE_ATTRIBUTE_SYSTEM, "system");
    add(FILE_ATTRIBUTE_DIRECTORY, "directory");
    add(FILE_ATTRIBUTE_ARCHIVE, "archive");
    add(FILE_ATTRIBUTE_DEVICE, "device");
    add(FILE_ATTRIBUTE_NORMAL, "normal");
    add(FILE_ATTRIBUTE_TEMPORARY, "temporary");
    add(FILE_ATTRIBUTE_SPARSE_FILE, "sparse");
    add(FILE_ATTRIBUTE_REPARSE_POINT, "reparse point");
    add(FILE_ATTRIBUTE_COMPRESSED, "compressed");
    add(FILE_ATTRIBUTE_OFFLINE, "offline");
    add(FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, "not content-indexed");
    add(FILE_ATTRIBUTE_ENCRYPTED, "encrypted");

    if (names.empty()) {
        return "none";
    }

    std::ostringstream out;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << names[index];
    }
    return out.str();
}

std::string relativeToRoot(const fs::path& path, const fs::path& rootPath) {
    const fs::path relative = normalizedPath(path).lexically_relative(normalizedPath(rootPath));
    if (relative.empty()) {
        return ".";
    }
    return pathToDisplayString(relative);
}

std::string allocatedSizeString(const fs::path& path) {
    DWORD high = 0;
    const DWORD low = GetCompressedFileSizeW(path.wstring().c_str(), &high);
    if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
        return "Unavailable";
    }

    ULARGE_INTEGER size{};
    size.LowPart = low;
    size.HighPart = high;
    return formatSize(size.QuadPart) + " (" + std::to_string(size.QuadPart) + " bytes)";
}

}

std::vector<DetailLine> buildDetailLines(const ExplorerState& state) {
    std::vector<DetailLine> lines;
    if (state.entries.empty() || state.cursor >= state.entries.size()) {
        addDetail(lines, "Selection", "No file selected");
        return lines;
    }

    const FileEntry& entry = state.entries[state.cursor];
    const std::string extension = extensionOf(entry.path);
    const std::string fullPath = pathToDisplayString(normalizedPath(entry.path));
    const std::string parentPath = pathToDisplayString(entry.path.parent_path());
    const std::string stem = entry.path.stem().u8string();

    std::error_code statusError;
    const fs::file_status status = fs::symlink_status(entry.path, statusError);

    WIN32_FILE_ATTRIBUTE_DATA attributeData{};
    const bool hasAttributes = GetFileAttributesExW(
        entry.path.wstring().c_str(),
        GetFileExInfoStandard,
        &attributeData
    ) != 0;

    addSection(lines, "Basic");
    addDetail(lines, "Name", entry.name);
    addDetail(lines, "Type", fileTypeDescription(entry));
    addDetail(lines, "Filesystem type", statusError ? "Unavailable: " + statusError.message() : fileStatusTypeName(status.type()));
    addDetail(lines, "Directory", yesNo(entry.isDirectory));
    addDetail(lines, "Hidden", yesNo(entry.isHidden));
    addDetail(lines, "Extension", extension.empty() ? "(none)" : extension);
    addDetail(lines, "Stem", stem.empty() ? "(none)" : stem);
    addDetail(lines, "Name length", std::to_string(entry.name.size()) + " characters");
    addDetail(lines, "Path length", std::to_string(fullPath.size()) + " characters");

    if (!entry.isDirectory) {
        addDetail(lines, "Size", formatSize(entry.size) + " (" + std::to_string(entry.size) + " bytes)");
        addDetail(lines, "Allocated size", allocatedSizeString(entry.path));
        addDetail(lines, "Opens in editor", yesNo(opensWithEditor(entry.path) || extension == ".tux"));
    }

    addSection(lines, "Paths");
    addDetail(lines, "Full path", fullPath);
    addDetail(lines, "Parent", parentPath.empty() ? "(none)" : parentPath);
    addDetail(lines, "Explorer root", pathToDisplayString(state.rootPath));
    addDetail(lines, "Relative to root", relativeToRoot(entry.path, state.rootPath));

    addSection(lines, "Timestamps");
    if (hasAttributes) {
        addDetail(lines, "Created", formatFileTime(attributeData.ftCreationTime));
        addDetail(lines, "Modified", formatFileTime(attributeData.ftLastWriteTime));
        addDetail(lines, "Accessed", formatFileTime(attributeData.ftLastAccessTime));
    } else {
        addDetail(lines, "Created", "Unavailable");
        addDetail(lines, "Modified", "Unavailable");
        addDetail(lines, "Accessed", "Unavailable");
    }

    addSection(lines, "Windows attributes");
    if (hasAttributes) {
        addDetail(lines, "Raw value", hexValue(attributeData.dwFileAttributes));
        addDetail(lines, "Flags", attributeList(attributeData.dwFileAttributes));
        addDetail(lines, "Read-only", yesNo((attributeData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0));
        addDetail(lines, "System", yesNo((attributeData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0));
        addDetail(lines, "Archive", yesNo((attributeData.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) != 0));
        addDetail(lines, "Compressed", yesNo((attributeData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) != 0));
        addDetail(lines, "Encrypted", yesNo((attributeData.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) != 0));
        addDetail(lines, "Reparse point", yesNo((attributeData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0));
    } else {
        addDetail(lines, "Attributes", "Unavailable");
    }

    addSection(lines, "Permissions");
    addDetail(lines, "Current user", state.username.empty() ? "(none)" : state.username);
    addDetail(lines, "User type", state.usertype.empty() ? "(none)" : state.usertype);
    if (!statusError) {
        addDetail(lines, "Filesystem perms", formatPermissions(status.permissions()));
    }
    const std::string permissionError = deletePermissionError(state, entry);
    addDetail(
        lines,
        "Explorer modify",
        permissionError.empty() ? "yes" : "no - " + permissionError
    );
    if (extension == ".tux") {
        const bool canModify = can_modify_tux_file(pathToDisplayString(entry.path), state.username, state.usertype);
        addDetail(lines, "TUX edit allowed", yesNo(canModify));
        addDetail(lines, "TUX open mode", canModify ? "edit" : "read-only");
    }

    addSection(lines, "Filesystem identity");
    const DWORD flags = entry.isDirectory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL;
    HANDLE handle = CreateFileW(
        entry.path.wstring().c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        flags,
        nullptr
    );
    if (handle != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info{};
        if (GetFileInformationByHandle(handle, &info)) {
            ULARGE_INTEGER fileIndex{};
            fileIndex.HighPart = info.nFileIndexHigh;
            fileIndex.LowPart = info.nFileIndexLow;
            ULARGE_INTEGER fileSize{};
            fileSize.HighPart = info.nFileSizeHigh;
            fileSize.LowPart = info.nFileSizeLow;
            addDetail(lines, "Volume serial", hexValue(info.dwVolumeSerialNumber));
            addDetail(lines, "File index", std::to_string(fileIndex.QuadPart));
            addDetail(lines, "Hard links", std::to_string(info.nNumberOfLinks));
            if (!entry.isDirectory) {
                addDetail(lines, "Handle size", formatSize(fileSize.QuadPart) + " (" + std::to_string(fileSize.QuadPart) + " bytes)");
            }
        } else {
            addDetail(lines, "Handle info", "Unavailable");
        }
        CloseHandle(handle);
    } else {
        addDetail(lines, "Handle info", "Unavailable");
    }

    if (entry.isDirectory) {
        addSection(lines, "Directory contents");
        const DirectoryStats stats = scanDirectoryStats(entry.path);
        addDetail(lines, "Direct children", std::to_string(stats.directChildren));
        addDetail(lines, "Recursive files", std::to_string(stats.recursiveFiles));
        addDetail(lines, "Recursive folders", std::to_string(stats.recursiveDirectories));
        addDetail(lines, "Recursive file size", formatSize(stats.totalFileSize) + " (" + std::to_string(stats.totalFileSize) + " bytes)");
        addDetail(lines, "Scan errors", std::to_string(stats.scanErrors));
    }

    return lines;
}

}
