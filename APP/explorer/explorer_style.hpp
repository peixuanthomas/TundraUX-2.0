#pragma once

#include <string>

namespace tundraux::explorer {

inline constexpr const char* kResetStyle = "\x1b[0m";
inline constexpr const char* kTitleStyle = "\x1b[1;38;5;51m";
inline constexpr const char* kRoleStyle = "\x1b[1;38;5;213m";
inline constexpr const char* kUserStyle = "\x1b[1;38;5;220m";
inline constexpr const char* kPathStyle = "\x1b[38;5;117m";
inline constexpr const char* kBorderStyle = "\x1b[38;5;39m";
inline constexpr const char* kHeaderStyle = "\x1b[1;38;5;195;48;5;24m";
inline constexpr const char* kSectionStyle = "\x1b[1;38;5;87m";
inline constexpr const char* kKeyStyle = "\x1b[1;38;5;220m";
inline constexpr const char* kHintStyle = "\x1b[38;5;245m";
inline constexpr const char* kHelpTextStyle = "\x1b[38;5;252m";
inline constexpr const char* kDirStyle = "\x1b[1;38;5;87m";
inline constexpr const char* kFileStyle = "\x1b[38;5;254m";
inline constexpr const char* kTextFileStyle = "\x1b[38;5;229m";
inline constexpr const char* kTuxFileStyle = "\x1b[1;38;5;213m";
inline constexpr const char* kDataFileStyle = "\x1b[38;5;203m";
inline constexpr const char* kHiddenStyle = "\x1b[38;5;244m";
inline constexpr const char* kSizeStyle = "\x1b[38;5;250m";
inline constexpr const char* kCopyStyle = "\x1b[1;38;5;119m";
inline constexpr const char* kCutStyle = "\x1b[38;5;246m";
inline constexpr const char* kSelectedBgStyle = "\x1b[48;5;24m";
inline constexpr const char* kSelectedMarkStyle = "\x1b[1;38;5;229m";
inline constexpr const char* kInputStyle = "\x1b[1;38;5;230m";
inline constexpr const char* kWarningStyle = "\x1b[1;38;5;203m";

inline std::string colorText(const std::string& text, const char* style) {
    if (text.empty()) {
        return text;
    }
    return std::string(style) + text + kResetStyle;
}

inline std::string colorCellPart(const std::string& text, const char* style, bool selected) {
    if (text.empty()) {
        return text;
    }

    std::string prefix;
    if (selected) {
        prefix += kSelectedBgStyle;
    }
    prefix += style;
    return prefix + text + kResetStyle;
}

inline std::string redMessage(const std::string& message) {
    return colorText(message, kWarningStyle);
}

}
