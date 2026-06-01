#pragma once

#include "backend_error.hpp"
#include "file_store.hpp"

#include <ctime>
#include <string>
#include <vector>

namespace tundraux::backend {

struct TuxMetadata {
    std::string creator;
    std::string lastEditor;
    std::time_t createTime = 0;
    std::time_t modifyTime = 0;
};

struct TuxContent {
    std::string content;
    TuxMetadata metadata;
};

class TuxStore {
public:
    virtual ~TuxStore() = default;
    virtual std::vector<FileEntry> list(const std::string& path) const = 0;
    virtual TuxMetadata metadata(const std::string& path) const = 0;
    virtual TuxContent read(const std::string& path) const = 0;
    virtual void create(const std::string& path, const TuxMetadata& metadata, bool overwrite) = 0;
    virtual void write(const std::string& path, const std::string& content, const TuxMetadata& metadata) = 0;
    virtual void deleteFile(const std::string& path) = 0;
    virtual void renameFile(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual void copyFile(const std::string& from, const std::string& to, const TuxMetadata& metadata, bool overwrite) = 0;
    virtual void moveFile(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual std::vector<FileEntry> search(const std::string& root, const std::string& query) const = 0;
};

} // namespace tundraux::backend
