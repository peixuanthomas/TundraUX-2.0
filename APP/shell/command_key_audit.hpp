#pragma once

#include <string>

#include <TundraTUI/input.hpp>

namespace tundraux::frontend {

inline std::string toFrontendAuditKeyText(const tundra_tui::KeyPress& key) {
    switch (key.key) {
        case tundra_tui::Key::Character:
            return std::string(1, key.character);
        case tundra_tui::Key::Enter:
            return "Enter";
        case tundra_tui::Key::Escape:
            return "Escape";
        case tundra_tui::Key::Backspace:
            return "Backspace";
        case tundra_tui::Key::Delete:
            return "Delete";
        case tundra_tui::Key::Tab:
            return "Tab";
        case tundra_tui::Key::Up:
            return "Up";
        case tundra_tui::Key::Down:
            return "Down";
        case tundra_tui::Key::Left:
            return "Left";
        case tundra_tui::Key::Right:
            return "Right";
        case tundra_tui::Key::Home:
            return "Home";
        case tundra_tui::Key::End:
            return "End";
        case tundra_tui::Key::PageUp:
            return "PageUp";
        case tundra_tui::Key::PageDown:
            return "PageDown";
        case tundra_tui::Key::F1:
            return "F1";
        case tundra_tui::Key::F2:
            return "F2";
        default:
            return "Unknown";
    }
}

inline tundra_tui::KeyPress keyPressFromFrontendAuditText(const std::string& key) {
    if (key.size() == 1) {
        return {
            tundra_tui::Key::Character,
            key.front()
        };
    }

    if (key == "Enter") {
        return {tundra_tui::Key::Enter, '\0'};
    }
    if (key == "Escape") {
        return {tundra_tui::Key::Escape, '\0'};
    }
    if (key == "Backspace") {
        return {tundra_tui::Key::Backspace, '\0'};
    }
    if (key == "Delete") {
        return {tundra_tui::Key::Delete, '\0'};
    }
    if (key == "Tab") {
        return {tundra_tui::Key::Tab, '\0'};
    }
    if (key == "Up") {
        return {tundra_tui::Key::Up, '\0'};
    }
    if (key == "Down") {
        return {tundra_tui::Key::Down, '\0'};
    }
    if (key == "Left") {
        return {tundra_tui::Key::Left, '\0'};
    }
    if (key == "Right") {
        return {tundra_tui::Key::Right, '\0'};
    }
    if (key == "Home") {
        return {tundra_tui::Key::Home, '\0'};
    }
    if (key == "End") {
        return {tundra_tui::Key::End, '\0'};
    }
    if (key == "PageUp") {
        return {tundra_tui::Key::PageUp, '\0'};
    }
    if (key == "PageDown") {
        return {tundra_tui::Key::PageDown, '\0'};
    }
    if (key == "F1") {
        return {tundra_tui::Key::F1, '\0'};
    }
    if (key == "F2") {
        return {tundra_tui::Key::F2, '\0'};
    }

    return {tundra_tui::Key::Unknown, '\0'};
}

} // namespace tundraux::frontend
