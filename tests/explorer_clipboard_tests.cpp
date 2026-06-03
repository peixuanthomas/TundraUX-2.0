#include "explorer_clipboard.hpp"
#include "explorer_backend.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

bool can_modify_tux_file(const std::string&, const std::string&, const std::string&) {
    return true;
}

bool can_read_tux_file(const std::string&, const std::string&, const std::string&) {
    return true;
}

bool directory_has_protected_tux_files(const std::string&, const std::string&, const std::string&) {
    return false;
}

int run_editor(const std::string&, const std::string&) {
    return 0;
}

namespace {

class FakeExplorerBackend final : public tundraux::explorer::ExplorerBackend {
public:
    std::vector<std::string> calls;

    tundraux::explorer::ExplorerBackendResult<std::vector<tundraux::explorer::FileEntry>> listDirectory(
        const std::string&
    ) override {
        return {true, {}, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<bool> createDirectory(const std::string& path) override {
        calls.push_back("mkdir:" + path);
        return {true, true, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<bool> deletePath(const std::string& path, bool recursive) override {
        calls.push_back("delete:" + path + ":" + (recursive ? "1" : "0"));
        return {true, true, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<bool> copyPath(
        const std::string& from,
        const std::string& to,
        bool overwrite
    ) override {
        calls.push_back("copy:" + from + ":" + to + ":" + (overwrite ? "1" : "0"));
        return {true, true, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<bool> movePath(
        const std::string& from,
        const std::string& to,
        bool overwrite
    ) override {
        calls.push_back("move:" + from + ":" + to + ":" + (overwrite ? "1" : "0"));
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
        return {true, "content", "", ""};
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
        return {true, {"content", "alice", "alice"}, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<bool> writeTux(
        const std::string& path,
        const std::string& content
    ) override {
        calls.push_back("writeTux:" + path + ":" + content);
        return {true, true, "", ""};
    }

    tundraux::explorer::ExplorerBackendResult<std::vector<std::string>> readTlog(
        const std::string&
    ) override {
        return {true, {}, "", ""};
    }
};

bool writerCalled = false;
bool writerShouldSucceed = true;
std::string writerText;
std::string writerFailure = "clipboard busy";

bool fakeSystemClipboardWriter(const std::string& text, std::string& error) {
    writerCalled = true;
    writerText = text;
    if (!writerShouldSucceed) {
        error = writerFailure;
        return false;
    }
    return true;
}

tundraux::explorer::ExplorerState stateWithSelection() {
    namespace fs = std::filesystem;

    tundraux::explorer::ExplorerState state;
    state.rootPath = fs::u8path("C:/root");
    state.currentPath = state.rootPath;
    state.entries.push_back({"first.txt", state.rootPath / "first.txt", false, false, 10});
    state.entries.push_back({"report.final.txt", state.rootPath / "nested" / "report.final.txt", false, false, 42});
    state.cursor = 1;
    return state;
}

void resetWriter() {
    writerCalled = false;
    writerShouldSucceed = true;
    writerText.clear();
    writerFailure = "clipboard busy";
}

bool explorerPasteUsesBackendCopy() {
    namespace fs = std::filesystem;

    FakeExplorerBackend backend;
    tundraux::explorer::ExplorerState state;
    state.backend = &backend;
    state.rootPath = fs::u8path("C:/root");
    state.currentPath = state.rootPath;
    state.clipboard.mode = tundraux::explorer::ClipboardMode::Copy;
    state.clipboard.path = state.rootPath / "source.txt";
    state.clipboard.name = "source.txt";
    state.clipboard.isDirectory = false;

    tundraux::explorer::pasteClipboard(state);

    return !backend.calls.empty() &&
        backend.calls.front() == "copy:source.txt:source.txt:0";
}

} // namespace

int main() {
    resetWriter();
    auto state = stateWithSelection();
    if (!tundraux::explorer::copySelectedFileName(state, fakeSystemClipboardWriter)) {
        std::cerr << "copySelectedFileName returned failure for selected file\n";
        return 1;
    }
    if (!writerCalled) {
        std::cerr << "system clipboard writer was not called\n";
        return 1;
    }
    if (writerText != "report.final.txt") {
        std::cerr << "copied text included path or wrong name: " << writerText << "\n";
        return 1;
    }
    if (state.message != "Copied file name: report.final.txt") {
        std::cerr << "unexpected success message: " << state.message << "\n";
        return 1;
    }

    resetWriter();
    tundraux::explorer::ExplorerState emptyState;
    if (tundraux::explorer::copySelectedFileName(emptyState, fakeSystemClipboardWriter)) {
        std::cerr << "copySelectedFileName succeeded without a selected file\n";
        return 1;
    }
    if (writerCalled) {
        std::cerr << "system clipboard writer was called without a selection\n";
        return 1;
    }
    if (emptyState.message != "Nothing selected") {
        std::cerr << "unexpected no-selection message: " << emptyState.message << "\n";
        return 1;
    }

    resetWriter();
    writerShouldSucceed = false;
    state = stateWithSelection();
    if (tundraux::explorer::copySelectedFileName(state, fakeSystemClipboardWriter)) {
        std::cerr << "copySelectedFileName succeeded after writer failure\n";
        return 1;
    }
    if (state.message.find("Could not copy file name: clipboard busy") == std::string::npos) {
        std::cerr << "unexpected writer failure message: " << state.message << "\n";
        return 1;
    }

    if (!explorerPasteUsesBackendCopy()) {
        std::cerr << "pasteClipboard did not route copy through backend\n";
        return 1;
    }

    return 0;
}
