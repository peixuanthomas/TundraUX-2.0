#include "editor.hpp"

#include "editor_win.hpp"

int run_editor(const std::string& filepath, const std::string& displayName) {
    return run_editor_windows(filepath, displayName);
}
