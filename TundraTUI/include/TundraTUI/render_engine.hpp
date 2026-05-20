#pragma once

#include <iosfwd>
#include <string>

namespace tundra_tui {

struct Size {
    int width = 0;
    int height = 0;
};

class RenderEngine {
public:
    explicit RenderEngine(std::ostream& output);

    void clear();
    void moveCursor(int row, int column);
    void hideCursor();
    void showCursor();
    void resetStyle();
    void write(const std::string& text);
    void writeLine(const std::string& text = "");
    void flush();

private:
    std::ostream* output = nullptr;
};

Size terminalSize();

}
