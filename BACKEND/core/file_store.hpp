#pragma once

#include "backend_error.hpp"

#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace tundraux::backend {

class BackendException : public std::exception {
public:
    BackendException(ErrorCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    ErrorCode code() const noexcept {
        return code_;
    }

    const char* what() const noexcept override {
        return message_.c_str();
    }

private:
    ErrorCode code_;
    std::string message_;
};

enum class FileEntryType {
    File,
    Directory
};

struct FileEntry {
    std::string name;
    std::string path;
    FileEntryType type;
    std::uintmax_t size = 0;
};

struct FileContent {
    std::string content;
};

class FileStore {
public:
    virtual ~FileStore() = default;
    virtual std::vector<FileEntry> listDirectory(const std::string& path) const = 0;
    virtual FileContent readFile(const std::string& path) const = 0;
    virtual void writeFile(const std::string& path, const std::string& content) = 0;
};

} // namespace tundraux::backend
