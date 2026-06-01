#include "tux_backend.hpp"

#include <iostream>
#include <string>
#include <vector>

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

class FakeTuxBackend final : public tundraux::file_manager::TuxBackend {
public:
    std::vector<std::string> calls;

    tundraux::file_manager::TuxBackendResult<std::vector<tundraux::frontend::FrontendFileEntry>> list(
        const std::string& path
    ) override {
        calls.push_back("list:" + path);
        return {true, {}, "", ""};
    }

    tundraux::file_manager::TuxBackendResult<bool> create(const std::string& path, bool overwrite) override {
        calls.push_back("create:" + path + ":" + (overwrite ? "1" : "0"));
        return {true, true, "", ""};
    }

    tundraux::file_manager::TuxBackendResult<tundraux::frontend::FrontendTuxContent> read(
        const std::string& path
    ) override {
        calls.push_back("read:" + path);
        return {true, {"content", "alice", "alice"}, "", ""};
    }

    tundraux::file_manager::TuxBackendResult<bool> write(
        const std::string& path,
        const std::string& content
    ) override {
        calls.push_back("write:" + path + ":" + content);
        return {true, true, "", ""};
    }

    tundraux::file_manager::TuxBackendResult<bool> deleteFile(const std::string& path) override {
        calls.push_back("delete:" + path);
        return {true, true, "", ""};
    }

    tundraux::file_manager::TuxBackendResult<bool> renameFile(
        const std::string& from,
        const std::string& to,
        bool overwrite
    ) override {
        calls.push_back("rename:" + from + ":" + to + ":" + (overwrite ? "1" : "0"));
        return {true, true, "", ""};
    }

    tundraux::file_manager::TuxBackendResult<bool> copyFile(
        const std::string& from,
        const std::string& to,
        bool overwrite
    ) override {
        calls.push_back("copy:" + from + ":" + to + ":" + (overwrite ? "1" : "0"));
        return {true, true, "", ""};
    }

    tundraux::file_manager::TuxBackendResult<bool> moveFile(
        const std::string& from,
        const std::string& to,
        bool overwrite
    ) override {
        calls.push_back("move:" + from + ":" + to + ":" + (overwrite ? "1" : "0"));
        return {true, true, "", ""};
    }

    tundraux::file_manager::TuxBackendResult<std::vector<tundraux::frontend::FrontendFileEntry>> search(
        const std::string& root,
        const std::string& query
    ) override {
        calls.push_back("search:" + root + ":" + query);
        return {true, {}, "", ""};
    }

    tundraux::file_manager::TuxBackendResult<bool> createDirectory(const std::string& path) override {
        calls.push_back("mkdir:" + path);
        return {true, true, "", ""};
    }

    tundraux::file_manager::TuxBackendResult<bool> removeDirectory(const std::string& path, bool recursive) override {
        calls.push_back("rmdir:" + path + ":" + (recursive ? "1" : "0"));
        return {true, true, "", ""};
    }
};

bool tux_backend_facade_can_be_faked() {
    FakeTuxBackend backend;
    const auto create = backend.create("docs/note", false);
    const auto read = backend.read("docs/note");
    return expect(create.ok, "create should be ok") &&
           expect(read.ok && read.value.content == "content", "read should return content") &&
           expect(backend.calls.size() == 2, "expected two calls");
}

int main() {
    return tux_backend_facade_can_be_faked() ? 0 : 1;
}
