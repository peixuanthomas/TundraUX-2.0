#pragma once

namespace tundra_tui {

enum class Key {
    Unknown,
    Character,
    Enter,
    Escape,
    Backspace,
    Delete,
    Tab,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    F1,
    F2
};

struct KeyPress {
    Key key = Key::Unknown;
    char character = '\0';
};

using KeyAuditSink = void(*)(const KeyPress& key, bool sensitive);

KeyPress readKey();
void setKeyAuditSink(KeyAuditSink sink);
void emitKeyAudit(const KeyPress& key, bool sensitive = false);

}
