#pragma once

#include <algorithm>
#include <string>

namespace tundra_tui {

inline std::string trimToWidth(const std::string& text, std::size_t width) {
    if (text.size() <= width) {
        return text;
    }
    if (width == 0) {
        return "";
    }
    if (width <= 3) {
        return text.substr(0, width);
    }
    return text.substr(0, width - 3) + "...";
}

inline std::string fitText(const std::string& text, std::size_t width) {
    std::string fitted = trimToWidth(text, width);
    if (fitted.size() < width) {
        fitted += std::string(width - fitted.size(), ' ');
    }
    return fitted;
}

inline std::string singleBorder(std::size_t width) {
    if (width < 2) {
        return "";
    }
    return "+" + std::string(width - 2, '-') + "+";
}

inline std::string splitBorder(std::size_t leftWidth, std::size_t rightWidth) {
    return "+" + std::string(leftWidth, '-') + "+" + std::string(rightWidth, '-') + "+";
}

}
