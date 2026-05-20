#include "TundraTUI/TundraTUI.hpp"

#include <iostream>
#include <string>

int main() {
    using namespace tundra_tui;

    std::cout << colorText("TundraTUI style demo", kTitleStyle) << "\n";
    std::cout << colorText(singleBorder(36), kBorderStyle) << "\n";
    std::cout << colorText("Title", kTitleStyle) << "\n";
    std::cout << colorText("Section", kSectionStyle) << "\n";
    std::cout << colorText("Path: ", kKeyStyle)
              << colorText("Files/readme.tux", kPathStyle) << "\n";
    std::cout << colorCellPart(fitText("> selected row", 24), kSelectedMarkStyle, true) << "\n";
    std::cout << colorText("Success", kCopyStyle) << " "
              << colorText("Warning", kWarningStyle) << " "
              << colorText("Hint", kHintStyle) << "\n";

    return 0;
}
