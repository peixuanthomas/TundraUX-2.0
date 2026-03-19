#ifndef EDITOR_H
#define EDITOR_H

#include <string>

enum class EditorBackend {
    Auto,
    Windows,
    Portable
};

int run_editor(const std::string& filepath, const std::string& displayName = "");

EditorBackend get_editor_backend();
bool set_editor_backend_by_name(const std::string& backendName);
std::string get_editor_backend_name();
std::string describe_editor_backend_options();

#endif // EDITOR_H
