#include "account_settings.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <conio.h>
#include <windows.h>

#include "color.hpp"
#include "console_screen.hpp"
#include "explorer_style.hpp"

namespace {

namespace tui = tundraux::explorer;

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
    Home,
    End,
    F1,
    F2
};

struct KeyPress {
    Key key = Key::Unknown;
    char character = '\0';
};

struct PasswordStatus {
    bool hasMinLength = false;
    bool hasUpper = false;
    bool hasLower = false;
    bool hasDigit = false;
};

struct DetailLine {
    std::string label;
    std::string value;
    bool section = false;
};

struct AccountSettingsState {
    USER original;
    std::string newPassword;
    std::string confirmPassword;
    std::string passwordHint;
    std::size_t field = 0;
    bool showPassword = false;
    bool showHelp = false;
    bool saved = false;
    std::string message = "Edit settings. Enter saves changes.";
};

std::string trimCopy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string trimToWidth(const std::string& text, std::size_t width) {
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

std::string fitText(const std::string& text, std::size_t width) {
    std::string fitted = trimToWidth(text, width);
    if (fitted.size() < width) {
        fitted += std::string(width - fitted.size(), ' ');
    }
    return fitted;
}

std::string maskText(const std::string& value) {
    if (value.empty()) {
        return "(empty)";
    }
    return std::string(value.size(), '*');
}

std::string passwordSummary(const std::string& value, bool showPassword) {
    if (value.empty()) {
        return "(keep current)";
    }
    const std::string display = showPassword ? value : std::string(value.size(), '*');
    return display + " (" + std::to_string(value.size()) + " chars)";
}

COORD consoleSize() {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(output, &info)) {
        return {
            static_cast<SHORT>(info.srWindow.Right - info.srWindow.Left + 1),
            static_cast<SHORT>(info.srWindow.Bottom - info.srWindow.Top + 1)
        };
    }
    return {120, 30};
}

std::string border(std::size_t leftWidth, std::size_t rightWidth) {
    return "+" + std::string(leftWidth, '-') + "+" + std::string(rightWidth, '-') + "+";
}

std::string headerCell(const std::string& text, std::size_t width) {
    return tui::colorText(fitText(" " + text, width), tui::kHeaderStyle);
}

const char* statusStyle(const std::string& value) {
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized.find("error") != std::string::npos ||
        normalized.find("missing") != std::string::npos ||
        normalized.find("invalid") != std::string::npos ||
        normalized.find("cannot") != std::string::npos ||
        normalized.find("does not") != std::string::npos ||
        normalized.find("failed") != std::string::npos ||
        normalized.find("incomplete") != std::string::npos) {
        return tui::kWarningStyle;
    }
    if (normalized.find("ok") != std::string::npos ||
        normalized.find("ready") != std::string::npos ||
        normalized.find("saved") != std::string::npos ||
        normalized.find("updated") != std::string::npos ||
        normalized.find("will") != std::string::npos) {
        return tui::kCopyStyle;
    }
    if (normalized == "(none)" || normalized == "(empty)" ||
        normalized == "unchanged" || normalized == "not needed" ||
        normalized == "no changes") {
        return tui::kHintStyle;
    }
    return tui::kHelpTextStyle;
}

std::string detailLineText(const DetailLine& line, std::size_t width) {
    if (line.section) {
        return tui::colorText(fitText(" " + line.label, width), tui::kHeaderStyle);
    }

    const std::size_t labelWidth = std::min<std::size_t>(18, std::max<std::size_t>(10, width / 3));
    const std::size_t valueWidth = width > labelWidth + 1 ? width - labelWidth - 1 : 0;
    return tui::colorText(fitText(line.label + ":", labelWidth), tui::kKeyStyle) +
           " " +
           tui::colorText(fitText(line.value, valueWidth), statusStyle(line.value));
}

PasswordStatus getPasswordStatus(const std::string& password) {
    PasswordStatus status;
    status.hasMinLength = password.length() >= 6;
    for (char c : password) {
        if (std::isupper(static_cast<unsigned char>(c))) {
            status.hasUpper = true;
        } else if (std::islower(static_cast<unsigned char>(c))) {
            status.hasLower = true;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            status.hasDigit = true;
        }
    }
    return status;
}

bool isValidPassword(const PasswordStatus& status) {
    return status.hasMinLength && status.hasUpper && status.hasLower && status.hasDigit;
}

std::string passwordRuleValue(const std::string& password, bool passed) {
    if (password.empty()) {
        return "Unchanged";
    }
    return passed ? "OK" : "Missing";
}

bool passwordWillChange(const AccountSettingsState& state) {
    return !state.newPassword.empty();
}

bool hintWillChange(const AccountSettingsState& state) {
    return trimCopy(state.passwordHint) != state.original.password_hint;
}

bool hasAnyChange(const AccountSettingsState& state) {
    return passwordWillChange(state) || hintWillChange(state);
}

std::string effectivePassword(const AccountSettingsState& state) {
    return passwordWillChange(state) ? state.newPassword : state.original.password;
}

std::string validateSettings(const AccountSettingsState& state) {
    if (!passwordWillChange(state) && !state.confirmPassword.empty()) {
        return "New password is empty; clear confirm or enter a password.";
    }

    if (passwordWillChange(state)) {
        const PasswordStatus passwordStatus = getPasswordStatus(state.newPassword);
        if (!isValidPassword(passwordStatus)) {
            return "Password requirements are incomplete.";
        }
        if (state.newPassword != state.confirmPassword) {
            return "Password confirmation does not match.";
        }
    }

    if (!state.passwordHint.empty() && trimCopy(state.passwordHint) == effectivePassword(state)) {
        return "Password hint cannot equal the password.";
    }

    if (!hasAnyChange(state)) {
        return "No changes to save.";
    }

    return "";
}

std::vector<DetailLine> buildDetailLines(const AccountSettingsState& state) {
    const PasswordStatus passwordStatus = getPasswordStatus(state.newPassword);
    const bool changingPassword = passwordWillChange(state);
    const bool confirmMatches = changingPassword && state.newPassword == state.confirmPassword;
    const bool hintValid = state.passwordHint.empty() || trimCopy(state.passwordHint) != effectivePassword(state);
    const bool ready = validateSettings(state).empty();

    return {
        {"Account", "", true},
        {"Name", state.original.name, false},
        {"Role", state.original.type, false},
        {"Failed attempts", std::to_string(state.original.count), false},
        {"Current hint", state.original.password_hint.empty() ? "(none)" : state.original.password_hint, false},
        {"Changes", "", true},
        {"Password", changingPassword ? "Will update" : "Unchanged", false},
        {"New password", passwordSummary(state.newPassword, state.showPassword), false},
        {"Hint", hintWillChange(state) ? "Will update" : "Unchanged", false},
        {"Validation", "", true},
        {"6+ characters", passwordRuleValue(state.newPassword, passwordStatus.hasMinLength), false},
        {"Uppercase", passwordRuleValue(state.newPassword, passwordStatus.hasUpper), false},
        {"Lowercase", passwordRuleValue(state.newPassword, passwordStatus.hasLower), false},
        {"Number", passwordRuleValue(state.newPassword, passwordStatus.hasDigit), false},
        {"Confirm", changingPassword ? (confirmMatches ? "OK" : "Does not match") : "Not needed", false},
        {"Hint", hintValid ? "OK" : "Cannot equal password", false},
        {"Ready", ready ? "Ready" : (hasAnyChange(state) ? "Incomplete" : "No changes"), false}
    };
}

std::string fieldLabel(std::size_t index) {
    switch (index) {
        case 0: return "New password";
        case 1: return "Confirm";
        case 2: return "Password hint";
        default: return "";
    }
}

std::string fieldValue(const AccountSettingsState& state, std::size_t index) {
    switch (index) {
        case 0:
            if (state.newPassword.empty()) {
                return "(keep current)";
            }
            return state.showPassword ? state.newPassword : maskText(state.newPassword);
        case 1:
            if (state.newPassword.empty() && state.confirmPassword.empty()) {
                return "(not needed)";
            }
            if (state.confirmPassword.empty()) {
                return "(empty)";
            }
            return state.showPassword ? state.confirmPassword : maskText(state.confirmPassword);
        case 2:
            return state.passwordHint.empty() ? "(none)" : state.passwordHint;
        default:
            return "";
    }
}

std::string formatFormLine(const AccountSettingsState& state, std::size_t index, std::size_t width) {
    const bool selected = state.field == index;
    const std::string marker = selected ? "> " : "  ";
    const std::string label = fitText(fieldLabel(index), 16);
    const std::string prefix = marker + label + " ";
    const std::size_t valueWidth = width > prefix.size() ? width - prefix.size() : 0;

    const char* valueStyle = tui::kInputStyle;
    if ((index == 0 && state.newPassword.empty()) ||
        (index == 1 && state.newPassword.empty() && state.confirmPassword.empty()) ||
        (index == 2 && state.passwordHint.empty())) {
        valueStyle = tui::kHintStyle;
    }

    return tui::colorCellPart(marker, selected ? tui::kSelectedMarkStyle : tui::kHintStyle, selected) +
           tui::colorCellPart(label, tui::kKeyStyle, selected) +
           tui::colorCellPart(" ", tui::kHelpTextStyle, selected) +
           tui::colorCellPart(fitText(fieldValue(state, index), valueWidth), valueStyle, selected);
}

void renderHelpBinding(const std::string& keys, const std::string& description) {
    std::cout << "  "
              << tui::colorText(fitText(keys, 24), tui::kKeyStyle)
              << tui::colorText(description, tui::kHelpTextStyle)
              << "\n";
}

void renderHelp() {
    std::cout << "\x1b[0m\x1b[2J\x1b[H\x1b[?25l";
    std::cout << tui::colorText("TundraUX Account Settings Help", tui::kTitleStyle) << "\n\n";
    std::cout << tui::colorText("Navigation", tui::kSectionStyle) << "\n";
    renderHelpBinding("Up/Down or Tab", "Move through setting fields");
    renderHelpBinding("Home/End", "Jump to first or last field");
    std::cout << "\n";

    std::cout << tui::colorText("Editing", tui::kSectionStyle) << "\n";
    renderHelpBinding("Type / Backspace", "Edit the highlighted field");
    renderHelpBinding("Delete", "Clear the highlighted field");
    renderHelpBinding("F2", "Show or hide new password fields");
    std::cout << "\n";

    std::cout << tui::colorText("Saving", tui::kSectionStyle) << "\n";
    renderHelpBinding("Enter", "Save changes when validation passes");
    renderHelpBinding("Esc", "Return without saving");
    renderHelpBinding("F1", "Return from help");
    std::cout << "\n";
    std::cout << tui::colorText("Press F1, q, Esc, or Enter to return.", tui::kHintStyle) << std::flush;
}

void renderSettings(const AccountSettingsState& state) {
    const COORD size = consoleSize();
    const std::size_t width = std::max<int>(size.X, 92);
    const std::size_t height = std::max<int>(size.Y, 20);
    const std::size_t rows = height > 8 ? height - 8 : 12;
    const std::size_t usableWidth = width > 3 ? width - 3 : width;
    const std::size_t formWidth = std::max<std::size_t>(38, usableWidth * 45 / 100);
    const std::size_t detailsWidth = usableWidth - formWidth;
    const auto details = buildDetailLines(state);

    std::cout << "\x1b[0m\x1b[2J\x1b[H\x1b[?25l";
    std::cout << tui::colorText("TundraUX Account Settings", tui::kTitleStyle)
              << tui::colorText(" - ", tui::kHintStyle)
              << tui::colorText(state.original.name, tui::kPathStyle)
              << "\n";
    std::cout << tui::colorText("user_data.dat", tui::kPathStyle) << "\n";
    std::cout << tui::colorText(border(formWidth, detailsWidth), tui::kBorderStyle) << "\n";
    std::cout << tui::colorText("|", tui::kBorderStyle)
              << headerCell("Settings Form", formWidth)
              << tui::colorText("|", tui::kBorderStyle)
              << headerCell("Details", detailsWidth)
              << tui::colorText("|", tui::kBorderStyle)
              << "\n";
    std::cout << tui::colorText(border(formWidth, detailsWidth), tui::kBorderStyle) << "\n";

    for (std::size_t rowIndex = 0; rowIndex < rows; ++rowIndex) {
        std::string formText = std::string(formWidth, ' ');
        if (rowIndex < 3) {
            formText = formatFormLine(state, rowIndex, formWidth);
        } else if (rowIndex == 4) {
            formText = tui::colorText(fitText(" Leave new password empty to keep the current password.", formWidth), tui::kHintStyle);
        } else if (rowIndex == 5) {
            formText = tui::colorText(fitText(" Password rules: 6+ chars, upper, lower, number", formWidth), tui::kHintStyle);
        }

        const std::string detailText = rowIndex < details.size()
            ? detailLineText(details[rowIndex], detailsWidth)
            : std::string(detailsWidth, ' ');

        std::cout << tui::colorText("|", tui::kBorderStyle)
                  << formText
                  << tui::colorText("|", tui::kBorderStyle)
                  << detailText
                  << tui::colorText("|", tui::kBorderStyle)
                  << "\n";
    }

    std::cout << tui::colorText(border(formWidth, detailsWidth), tui::kBorderStyle) << "\n";
    std::cout << tui::colorText("Up/Down", tui::kKeyStyle)
              << tui::colorText(" field | ", tui::kHintStyle)
              << tui::colorText("Tab", tui::kKeyStyle)
              << tui::colorText(" next | ", tui::kHintStyle)
              << tui::colorText("Enter", tui::kKeyStyle)
              << tui::colorText(" save | ", tui::kHintStyle)
              << tui::colorText("F2", tui::kKeyStyle)
              << tui::colorText(state.showPassword ? " hide password | " : " show password | ", tui::kHintStyle)
              << tui::colorText("F1", tui::kKeyStyle)
              << tui::colorText(" help | ", tui::kHintStyle)
              << tui::colorText("Esc", tui::kKeyStyle)
              << tui::colorText(" quit", tui::kHintStyle)
              << "\n";
    std::cout << tui::colorText("Status: ", tui::kSectionStyle)
              << tui::colorText(state.message, statusStyle(state.message))
              << std::flush;
}

KeyPress readKey() {
    const int ch = _getch();
    if (ch == 0 || ch == 224) {
        const int ext = _getch();
        switch (ext) {
            case 72: return {Key::Up, '\0'};
            case 80: return {Key::Down, '\0'};
            case 71: return {Key::Home, '\0'};
            case 79: return {Key::End, '\0'};
            case 83: return {Key::Delete, '\0'};
            case 59: return {Key::F1, '\0'};
            case 60: return {Key::F2, '\0'};
            default: return {Key::Unknown, '\0'};
        }
    }

    switch (ch) {
        case 13: return {Key::Enter, '\0'};
        case 27: return {Key::Escape, '\0'};
        case 8: return {Key::Backspace, '\0'};
        case 9: return {Key::Tab, '\0'};
        default:
            if (std::isprint(static_cast<unsigned char>(ch))) {
                return {Key::Character, static_cast<char>(ch)};
            }
            return {Key::Unknown, '\0'};
    }
}

std::string& activeField(AccountSettingsState& state) {
    switch (state.field) {
        case 0: return state.newPassword;
        case 1: return state.confirmPassword;
        case 2: return state.passwordHint;
        default: return state.newPassword;
    }
}

bool saveSettings(AccountSettingsState& state, USER& currentUser) {
    const std::string validationError = validateSettings(state);
    if (!validationError.empty()) {
        state.message = validationError;
        return false;
    }

    USER updated = state.original;
    if (passwordWillChange(state)) {
        updated.password = state.newPassword;
    }
    updated.password_hint = trimCopy(state.passwordHint);

    DataManager dataManager("user_data.dat");
    if (!dataManager.UpdateUser(state.original.name, updated)) {
        state.message = "Failed to update user info.";
        return false;
    }

    currentUser = updated;
    state.original = updated;
    state.newPassword.clear();
    state.confirmPassword.clear();
    state.passwordHint = updated.password_hint;
    state.saved = true;
    state.message = "Settings saved. Press Enter or Esc to return.";
    return true;
}

bool handleSettingsKey(AccountSettingsState& state, USER& currentUser, const KeyPress& key) {
    if (state.showHelp) {
        if (key.key == Key::Escape || key.key == Key::Enter ||
            key.key == Key::F1 ||
            (key.key == Key::Character && (key.character == 'q' || key.character == 'Q'))) {
            state.showHelp = false;
        }
        return true;
    }

    if (state.saved) {
        if (key.key == Key::Enter || key.key == Key::Escape ||
            (key.key == Key::Character && (key.character == 'q' || key.character == 'Q'))) {
            return false;
        }
        state.message = "Settings saved. Press Enter or Esc to return.";
        return true;
    }

    state.message = "Edit settings. Enter saves changes.";
    switch (key.key) {
        case Key::Escape:
            return false;
        case Key::Up:
            if (state.field > 0) {
                --state.field;
            }
            break;
        case Key::Down:
        case Key::Tab:
            state.field = (state.field + 1) % 3;
            break;
        case Key::Home:
            state.field = 0;
            break;
        case Key::End:
            state.field = 2;
            break;
        case Key::Backspace: {
            std::string& value = activeField(state);
            if (!value.empty()) {
                value.pop_back();
            }
            break;
        }
        case Key::Delete:
            activeField(state).clear();
            break;
        case Key::Enter:
            saveSettings(state, currentUser);
            break;
        case Key::Character:
            activeField(state).push_back(key.character);
            break;
        case Key::F1:
            state.showHelp = true;
            break;
        case Key::F2:
            state.showPassword = !state.showPassword;
            state.message = state.showPassword ? "Password visible." : "Password hidden.";
            break;
        case Key::Unknown:
            break;
    }

    return true;
}

const USER* findCurrentUser(const DataManager& dataManager, const USER& currentUser) {
    for (const auto& user : dataManager.GetAllUsers()) {
        if (user.name == currentUser.name) {
            return &user;
        }
    }
    return nullptr;
}

} // namespace

void open_account_settings(USER& currentUser) {
    if (currentUser.name.empty()) {
        colorcout("yellow", "No user is currently logged in.\n");
        return;
    }

    std::ifstream check("user_data.dat");
    if (!check.good()) {
        colorcout("red", "Error: user_data.dat not found.\n");
        return;
    }
    check.close();

    DataManager dataManager("user_data.dat");
    const USER* storedUser = findCurrentUser(dataManager, currentUser);
    if (storedUser == nullptr) {
        colorcout("red", "Current user is not stored in user_data.dat.\n");
        return;
    }

    set_title("Account Settings");
    ConsoleScreenGuard screenGuard;

    AccountSettingsState state;
    state.original = *storedUser;
    state.passwordHint = storedUser->password_hint;

    bool running = true;
    while (running) {
        if (state.showHelp) {
            renderHelp();
        } else {
            renderSettings(state);
        }

        running = handleSettingsKey(state, currentUser, readKey());
    }
}
