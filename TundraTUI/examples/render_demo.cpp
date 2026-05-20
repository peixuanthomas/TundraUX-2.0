#include "TundraTUI/TundraTUI.hpp"

#include <iostream>

int main() {
    using namespace tundra_tui;

    ConsoleScreenGuard screenGuard;
    RenderEngine render(std::cout);
    const Size size = terminalSize();

    render.hideCursor();
    render.writeLine(colorText("TundraTUI render demo", kTitleStyle));
    render.writeLine(colorText("Terminal size: ", kSectionStyle) +
                     std::to_string(size.width) + "x" + std::to_string(size.height));
    render.writeLine(colorText(singleBorder(44), kBorderStyle));
    render.writeLine(colorText("Press any key to leave the alternate screen.", kHintStyle));
    render.flush();

    readKey();
    render.showCursor();
    render.flush();
    return 0;
}
