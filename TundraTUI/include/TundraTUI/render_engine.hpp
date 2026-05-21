#pragma once

#include <iosfwd>
#include <cstddef>
#include <string>
#include <vector>

namespace tundra_tui {

struct Size {
    int width = 0;
    int height = 0;
};

struct FooterHint {
    std::string key;
    std::string label;
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
std::size_t footerHintWidth(const FooterHint& hint);
std::vector<FooterHint> fitFooterHints(
    const std::vector<FooterHint>& hints,
    Size terminal,
    std::size_t reservedWidth = 0
);

}
