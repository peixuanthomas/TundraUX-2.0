#include "explorer_open.hpp"
#include "explorer_backend.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace tundraux::audit {
void logEvent(const std::string&, const std::string&) {}
int openTlogInEditor(const std::string&, const std::string&, const std::string&, const std::string&) {
    return 0;
}
}

namespace tundraux::explorer {
std::string explorerRelativePath(const fs::path& root, const fs::path& path) {
    std::error_code error;
    const fs::path relative = fs::relative(path, root, error);
    if (error) {
        return path.u8string();
    }
    const std::string value = relative.generic_u8string();
    return value == "." ? std::string{} : value;
}
}

int open_tux_file_in_editor(
    const std::string&,
    const std::string&,
    const std::string&,
    const std::string&,
    bool
) {
    return 0;
}

namespace {
std::string g_editorOutput = "edited";
}

int run_editor(const std::string& path, const std::string&) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << g_editorOutput;
    return out ? 0 : 1;
}

namespace {

class FakeExplorerBackend final : public tundraux::explorer::ExplorerBackend {
public:
    std::vector<std::string> calls;
    bool readFileOk = true;
    std::string readFileErrorCode;
    std::string readFileMessage;
    std::string readFileContent = "original";

    tundraux::explorer::ExplorerBackendResult<std::vector<tundraux::explorer::FileEntry>> listDirectory(
        const std::string& path
    ) override {
        calls.push_back("list:" + path);
        return {true, {}, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<bool> createDirectory(const std::string&) override {
        return {true, true, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<bool> deletePath(const std::string&, bool) override {
        return {true, true, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<bool> copyPath(
        const std::string&,
        const std::string&,
        bool
    ) override {
        return {true, true, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<bool> movePath(
        const std::string&,
        const std::string&,
        bool
    ) override {
        return {true, true, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<std::vector<tundraux::explorer::FileEntry>> search(
        const std::string&,
        const std::string&
    ) override {
        return {true, {}, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<std::string> readFile(const std::string& path) override {
        calls.push_back("readFile:" + path);
        return {readFileOk, readFileContent, readFileErrorCode, readFileMessage};
    }

    tundraux::explorer::ExplorerBackendResult<bool> writeFile(
        const std::string& path,
        const std::string& content
    ) override {
        calls.push_back("writeFile:" + path + ":" + content);
        return {true, true, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<tundraux::frontend::FrontendTuxContent> readTux(
        const std::string& path
    ) override {
        calls.push_back("readTux:" + path);
        return {true, {"original", "alice", "alice"}, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<bool> writeTux(
        const std::string& path,
        const std::string& content
    ) override {
        calls.push_back("writeTux:" + path + ":" + content);
        return {true, true, "", ""};
    }
};

bool containsCall(const std::vector<std::string>& calls, const std::string& expected) {
    for (const auto& call : calls) {
        if (call == expected) {
            return true;
        }
    }
    return false;
}

} // namespace

bool backendTuxOpenUsesBackendReadWrite() {
    namespace fs = std::filesystem;

    FakeExplorerBackend backend;
    tundraux::explorer::ExplorerState state;
    state.backend = &backend;
    state.rootPath = fs::u8path("C:/root");
    state.currentPath = state.rootPath;
    state.username = "alice";
    state.usertype = "user";
    state.entries.push_back({"note.TUX", state.rootPath / "docs" / "note.TUX", false, false, 12});

    tundraux::explorer::openSelected(state);

    if (!containsCall(backend.calls, "readTux:docs/note")) {
        std::cerr << "backend TUX read was not called with explorer-relative path\n";
        return 1;
    }
    if (!containsCall(backend.calls, "writeTux:docs/note:edited")) {
        std::cerr << "backend TUX write was not called after editor changed content\n";
        return 1;
    }
    if (state.message != "0 item(s)") {
        std::cerr << "unexpected final message after refresh: " << state.message << "\n";
        return false;
    }

    return true;
}

bool backendPlainFileOpenUsesBackendReadWrite() {
    namespace fs = std::filesystem;

    FakeExplorerBackend backend;
    tundraux::explorer::ExplorerState state;
    state.backend = &backend;
    state.rootPath = fs::u8path("C:/root");
    state.currentPath = state.rootPath;
    state.username = "alice";
    state.usertype = "user";
    state.entries.push_back({"note.txt", state.rootPath / "docs" / "note.txt", false, false, 8});

    tundraux::explorer::openSelected(state);

    if (!containsCall(backend.calls, "readFile:docs/note.txt")) {
        std::cerr << "backend file read was not called with explorer-relative path\n";
        return false;
    }
    if (!containsCall(backend.calls, "writeFile:docs/note.txt:edited")) {
        std::cerr << "backend file write was not called after editor changed content\n";
        return false;
    }
    if (state.message != "0 item(s)") {
        std::cerr << "unexpected final message after plain file refresh: " << state.message << "\n";
        return false;
    }

    return true;
}

bool backendPlainFileReadDeniedDoesNotOpenOrWrite() {
    namespace fs = std::filesystem;

    FakeExplorerBackend backend;
    backend.readFileOk = false;
    backend.readFileErrorCode = "PermissionDenied";
    tundraux::explorer::ExplorerState state;
    state.backend = &backend;
    state.rootPath = fs::u8path("C:/root");
    state.currentPath = state.rootPath;
    state.entries.push_back({"note.txt", state.rootPath / "docs" / "note.txt", false, false, 8});

    tundraux::explorer::openSelected(state);

    if (!containsCall(backend.calls, "readFile:docs/note.txt")) {
        std::cerr << "backend file read was not called before denying access\n";
        return false;
    }
    if (containsCall(backend.calls, "writeFile:docs/note.txt:edited")) {
        std::cerr << "backend file write should not be called after read denial\n";
        return false;
    }
    if (state.message.find("Access denied") == std::string::npos) {
        std::cerr << "expected access denied message, got: " << state.message << "\n";
        return false;
    }

    return true;
}

bool backendPlainFileUnchangedDoesNotWrite() {
    namespace fs = std::filesystem;

    FakeExplorerBackend backend;
    backend.readFileContent = "same";
    g_editorOutput = "same";
    tundraux::explorer::ExplorerState state;
    state.backend = &backend;
    state.rootPath = fs::u8path("C:/root");
    state.currentPath = state.rootPath;
    state.entries.push_back({"same.txt", state.rootPath / "docs" / "same.txt", false, false, 4});

    tundraux::explorer::openSelected(state);
    g_editorOutput = "edited";

    if (!containsCall(backend.calls, "readFile:docs/same.txt")) {
        std::cerr << "backend file read was not called for unchanged file\n";
        return false;
    }
    for (const auto& call : backend.calls) {
        if (call.rfind("writeFile:", 0) == 0) {
            std::cerr << "backend file write should not be called when content is unchanged\n";
            return false;
        }
    }

    return true;
}

bool backendExternalOpenUsesBackendReadForPermissionBoundary() {
    namespace fs = std::filesystem;

    FakeExplorerBackend backend;
    backend.readFileOk = false;
    backend.readFileErrorCode = "PermissionDenied";
    tundraux::explorer::ExplorerState state;
    state.backend = &backend;
    state.rootPath = fs::u8path("C:/root");
    state.currentPath = state.rootPath;
    state.entries.push_back({"settings.json", state.rootPath / "docs" / "settings.json", false, false, 2});

    tundraux::explorer::openSelected(state);

    if (!containsCall(backend.calls, "readFile:docs/settings.json")) {
        std::cerr << "backend file read was not called for non-editor file before open\n";
        return false;
    }
    if (state.message.find("Access denied") == std::string::npos) {
        std::cerr << "expected access denied message for non-editor file, got: " << state.message << "\n";
        return false;
    }

    return true;
}

int main() {
    if (!backendTuxOpenUsesBackendReadWrite()) return 1;
    if (!backendPlainFileOpenUsesBackendReadWrite()) return 1;
    if (!backendPlainFileReadDeniedDoesNotOpenOrWrite()) return 1;
    if (!backendPlainFileUnchangedDoesNotWrite()) return 1;
    if (!backendExternalOpenUsesBackendReadForPermissionBoundary()) return 1;
    return 0;
}
