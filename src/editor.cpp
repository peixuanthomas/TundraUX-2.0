#include "editor.h"

#include <algorithm>
#include <cctype>

#include "editor_win.h"

namespace {
EditorBackend g_editorBackend = EditorBackend::Auto;

std::string normalizeBackendName(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

EditorBackend resolve_backend() {
#ifdef _WIN32
    if (g_editorBackend == EditorBackend::Auto) {
        return EditorBackend::Windows;
    }
#else
    if (g_editorBackend == EditorBackend::Auto) {
        return EditorBackend::Portable;
    }
#endif
    return g_editorBackend;
}
}

int run_editor(const std::string& filepath, const std::string& displayName) {
    switch (resolve_backend()) {
        case EditorBackend::Windows:
            return run_editor_windows(filepath, displayName);
        case EditorBackend::Portable:
            return run_editor_portable(filepath, displayName);
        case EditorBackend::Auto:
        default:
            return run_editor_portable(filepath, displayName);
    }
}

EditorBackend get_editor_backend() {
    return g_editorBackend;
}

bool set_editor_backend_by_name(const std::string& backendName) {
    const std::string normalized = normalizeBackendName(backendName);
    if (normalized == "auto") {
        g_editorBackend = EditorBackend::Auto;
        return true;
    }
    if (normalized == "portable" || normalized == "cross" || normalized == "cross-platform") {
        g_editorBackend = EditorBackend::Portable;
        return true;
    }
    if (normalized == "win" || normalized == "windows") {
#ifdef _WIN32
        g_editorBackend = EditorBackend::Windows;
        return true;
#else
        return false;
#endif
    }
    return false;
}

std::string get_editor_backend_name() {
    switch (get_editor_backend()) {
        case EditorBackend::Auto:
            return "auto";
        case EditorBackend::Windows:
            return "windows";
        case EditorBackend::Portable:
            return "portable";
        default:
            return "unknown";
    }
}

std::string describe_editor_backend_options() {
#ifdef _WIN32
    return "auto, windows, portable";
#else
    return "auto, portable";
#endif
}
