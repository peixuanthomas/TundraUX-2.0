#ifndef EDITOR_WIN_H
#define EDITOR_WIN_H

#include <string>

// Run the standalone text editor with the specified file.
// displayName overrides the filename shown in the editor UI.
// Returns 0 on success.
int run_editor(const std::string& filepath, const std::string& displayName = "");

#endif // EDITOR_WIN_H
