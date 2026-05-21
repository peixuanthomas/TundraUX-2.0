#include "TundraTUI/render_engine.hpp"

#include <iostream>
#include <vector>

int main() {
    const std::vector<tundra_tui::FooterHint> hints = {
        {"Enter", "open"},
        {"Backspace", "parent"},
        {"y", "name"},
        {"c", "copy"}
    };

    const auto wide = tundra_tui::fitFooterHints(hints, {80, 24});
    if (wide.size() != hints.size()) {
        std::cerr << "wide terminal did not keep every footer hint\n";
        return 1;
    }

    const auto narrow = tundra_tui::fitFooterHints(hints, {22, 24});
    if (narrow.size() != 1 || narrow.front().key != "Enter") {
        std::cerr << "narrow terminal did not keep the highest-priority fitting hint\n";
        return 1;
    }

    const auto withReservedStatus = tundra_tui::fitFooterHints(hints, {35, 24}, 13);
    if (withReservedStatus.size() != 1) {
        std::cerr << "reserved width was not subtracted from available footer width\n";
        return 1;
    }

    const auto tooSmall = tundra_tui::fitFooterHints(hints, {4, 24});
    if (!tooSmall.empty()) {
        std::cerr << "terminal too small for first hint still returned footer hints\n";
        return 1;
    }

    const auto noRows = tundra_tui::fitFooterHints(hints, {80, 0});
    if (!noRows.empty()) {
        std::cerr << "terminal with no rows should not return footer hints\n";
        return 1;
    }

    return 0;
}
