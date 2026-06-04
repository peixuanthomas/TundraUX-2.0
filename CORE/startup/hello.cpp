#include "hello.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "backend_facade.hpp"
#include "TundraTUI/color.hpp"
#include "TundraTUI/input.hpp"
#include "TundraTUI/render_engine.hpp"
#include "TundraTUI/screen.hpp"
#include "TundraTUI/style.hpp"
#include "TundraTUI/text.hpp"

namespace {

namespace tui = tundra_tui;

using tundra_tui::ConsoleScreenGuard;
using tundra_tui::fitText;
using tundra_tui::Key;
using tundra_tui::KeyPress;
using tundra_tui::readKey;
using tundra_tui::set_title;

struct PasswordStatus {
    bool hasMinLength = false;
    bool hasUpper = false;
    bool hasLower = false;
    bool hasDigit = false;
};

struct SetupState {
    std::string username;
    std::string password;
    std::string confirmPassword;
    std::string passwordHint;
    std::size_t field = 0;
    bool showPassword = false;
    bool showHelp = false;
    bool created = false;
    std::string message = "Fill the setup form. Enter creates the admin account.";
};

struct DetailLine {
    std::string label;
    std::string value;
    bool section = false;
};

std::string trimCopy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool hasWhitespace(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

std::string maskText(const std::string& value) {
    if (value.empty()) {
        return "(empty)";
    }
    return std::string(value.size(), '*');
}

std::string passwordSummary(const std::string& value, bool showPassword) {
    if (value.empty()) {
        return "(empty)";
    }
    const std::string display = showPassword ? value : std::string(value.size(), '*');
    return display + " (" + std::to_string(value.size()) + " chars)";
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
        normalized.find("incomplete") != std::string::npos ||
        normalized.find("replace") != std::string::npos) {
        return tui::kWarningStyle;
    }
    if (normalized.find("ok") != std::string::npos ||
        normalized.find("ready") != std::string::npos ||
        normalized.find("created") != std::string::npos ||
        normalized.find("valid") != std::string::npos ||
        normalized.find("saved") != std::string::npos) {
        return tui::kCopyStyle;
    }
    if (normalized == "(none)" || normalized == "(empty)" || normalized == "optional") {
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

std::string validateUsername(const std::string& username) {
    const std::string trimmed = trimCopy(username);
    if (trimmed.empty()) {
        return "Username cannot be empty.";
    }
    if (trimmed == "null") {
        return "\"null\" is reserved for setup.";
    }
    if (hasWhitespace(trimmed)) {
        return "Username cannot contain spaces.";
    }
    return "";
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

std::string passwordRuleValue(bool passed) {
    return passed ? "OK" : "Missing";
}

std::vector<DetailLine> buildDetailLines(const SetupState& state) {
    const PasswordStatus passwordStatus = getPasswordStatus(state.password);
    const std::string usernameError = validateUsername(state.username);
    const bool passwordValid = isValidPassword(passwordStatus);
    const bool confirmMatches = !state.password.empty() && state.password == state.confirmPassword;
    const bool hintValid = state.passwordHint.empty() || state.passwordHint != state.password;

    std::vector<DetailLine> lines = {
        {"Account", "", true},
        {"Role", "admin", false},
        {"Username", state.username.empty() ? "(empty)" : trimCopy(state.username), false},
        {"Password", passwordSummary(state.password, state.showPassword), false},
        {"Hint", state.passwordHint.empty() ? "(none)" : state.passwordHint, false},
        {"Validation", "", true},
        {"Username", usernameError.empty() ? "OK" : usernameError, false},
        {"6+ characters", passwordRuleValue(passwordStatus.hasMinLength), false},
        {"Uppercase", passwordRuleValue(passwordStatus.hasUpper), false},
        {"Lowercase", passwordRuleValue(passwordStatus.hasLower), false},
        {"Number", passwordRuleValue(passwordStatus.hasDigit), false},
        {"Confirm", confirmMatches ? "OK" : "Does not match", false},
        {"Hint", hintValid ? "OK" : "Cannot equal password", false},
        {"Ready", usernameError.empty() && passwordValid && confirmMatches && hintValid ? "Ready" : "Incomplete", false}
    };

    return lines;
}

std::string fieldLabel(std::size_t index) {
    switch (index) {
        case 0: return "Username";
        case 1: return "Password";
        case 2: return "Confirm";
        case 3: return "Password hint";
        default: return "";
    }
}

std::string fieldValue(const SetupState& state, std::size_t index) {
    switch (index) {
        case 0: return state.username.empty() ? "(empty)" : state.username;
        case 1: return state.showPassword ? state.password : maskText(state.password);
        case 2: return state.showPassword ? state.confirmPassword : maskText(state.confirmPassword);
        case 3: return state.passwordHint.empty() ? "(optional)" : state.passwordHint;
        default: return "";
    }
}

std::string formatFormLine(const SetupState& state, std::size_t index, std::size_t width) {
    const bool selected = state.field == index;
    const std::string marker = selected ? "> " : "  ";
    const std::string label = fitText(fieldLabel(index), 16);
    const std::string prefix = marker + label + " ";
    const std::size_t valueWidth = width > prefix.size() ? width - prefix.size() : 0;

    const char* valueStyle = index == 0 ? tui::kUserStyle : tui::kInputStyle;
    if (index == 3 && state.passwordHint.empty()) {
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
    std::cout << tui::colorText("TundraUX First-Time Setup Help", tui::kTitleStyle) << "\n\n";
    std::cout << tui::colorText("Navigation", tui::kSectionStyle) << "\n";
    renderHelpBinding("Up/Down or Tab", "Move through setup fields");
    renderHelpBinding("Home/End", "Jump to first or last field");
    std::cout << "\n";

    std::cout << tui::colorText("Editing", tui::kSectionStyle) << "\n";
    renderHelpBinding("Type / Backspace", "Edit the highlighted field");
    renderHelpBinding("Delete", "Clear the highlighted field");
    renderHelpBinding("F2", "Show or hide password fields");
    std::cout << "\n";

    std::cout << tui::colorText("Create Admin", tui::kSectionStyle) << "\n";
    renderHelpBinding("Enter", "Create the first administrator when validation passes");
    renderHelpBinding("F1", "Return from help");
    std::cout << "\n";
    std::cout << tui::colorText("Press F1, q, Esc, or Enter to return.", tui::kHintStyle) << std::flush;
}

void renderSetup(const SetupState& state) {
    const tui::Size size = tui::terminalSize();
    const std::size_t width = std::max<int>(size.width, 92);
    const std::size_t height = std::max<int>(size.height, 20);
    const std::size_t rows = height > 8 ? height - 8 : 12;
    const std::size_t usableWidth = width > 3 ? width - 3 : width;
    const std::size_t formWidth = std::max<std::size_t>(38, usableWidth * 45 / 100);
    const std::size_t detailsWidth = usableWidth - formWidth;
    const auto details = buildDetailLines(state);

    std::cout << "\x1b[0m\x1b[2J\x1b[H\x1b[?25l";
    std::cout << tui::colorText("TundraUX First-Time Setup", tui::kTitleStyle)
              << tui::colorText(" - ", tui::kHintStyle)
              << tui::colorText("administrator account", tui::kPathStyle)
              << "\n";
    std::cout << tui::colorText("backend setup", tui::kPathStyle) << "\n";
    std::cout << tui::colorText(tui::splitBorder(formWidth, detailsWidth), tui::kBorderStyle) << "\n";
    std::cout << tui::colorText("|", tui::kBorderStyle)
              << headerCell("Setup Form", formWidth)
              << tui::colorText("|", tui::kBorderStyle)
              << headerCell("Details", detailsWidth)
              << tui::colorText("|", tui::kBorderStyle)
              << "\n";
    std::cout << tui::colorText(tui::splitBorder(formWidth, detailsWidth), tui::kBorderStyle) << "\n";

    for (std::size_t rowIndex = 0; rowIndex < rows; ++rowIndex) {
        std::string formText = std::string(formWidth, ' ');
        if (rowIndex < 4) {
            formText = formatFormLine(state, rowIndex, formWidth);
        } else if (rowIndex == 5) {
            formText = tui::colorText(fitText(" Password rules: 6+ chars, upper, lower, number", formWidth), tui::kHintStyle);
        } else if (rowIndex == 6) {
            formText = tui::colorText(fitText(" Password hint is optional but cannot equal password.", formWidth), tui::kHintStyle);
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

    std::cout << tui::colorText(tui::splitBorder(formWidth, detailsWidth), tui::kBorderStyle) << "\n";
    std::cout << tui::colorText("Up/Down", tui::kKeyStyle)
              << tui::colorText(" field | ", tui::kHintStyle)
              << tui::colorText("Tab", tui::kKeyStyle)
              << tui::colorText(" next | ", tui::kHintStyle)
              << tui::colorText("Enter", tui::kKeyStyle)
              << tui::colorText(" create | ", tui::kHintStyle)
              << tui::colorText("F2", tui::kKeyStyle)
              << tui::colorText(state.showPassword ? " hide password | " : " show password | ", tui::kHintStyle)
              << tui::colorText("F1", tui::kKeyStyle)
              << tui::colorText(" help", tui::kHintStyle)
              << "\n";
    std::cout << tui::colorText("Status: ", tui::kSectionStyle)
              << tui::colorText(state.message, statusStyle(state.message))
              << std::flush;
}

std::string& activeField(SetupState& state) {
    switch (state.field) {
        case 0: return state.username;
        case 1: return state.password;
        case 2: return state.confirmPassword;
        case 3: return state.passwordHint;
        default: return state.username;
    }
}

std::string validateSetup(const SetupState& state) {
    const std::string usernameError = validateUsername(state.username);
    if (!usernameError.empty()) {
        return usernameError;
    }

    const PasswordStatus passwordStatus = getPasswordStatus(state.password);
    if (!isValidPassword(passwordStatus)) {
        return "Password requirements are incomplete.";
    }
    if (state.password != state.confirmPassword) {
        return "Password confirmation does not match.";
    }
    if (!state.passwordHint.empty() && state.passwordHint == state.password) {
        return "Password hint cannot equal the password.";
    }
    return "";
}

void applyCreateResult(SetupState& state, const tundraux::frontend::FacadeResult& result) {
    if (!result.ok) {
        if (result.errorCode == "PermissionDenied" && result.message == "Setup already initialized.") {
            state.created = true;
        }
        state.message = result.message.empty() ? "Failed to create initial admin." : result.message;
        return;
    }

    state.created = true;
    state.message = "Admin user created. Press Enter to continue.";
}

void tryCreate(SetupState& state, const std::function<tundraux::frontend::FacadeResult(
    const std::string&,
    const std::string&,
    const std::string&
)>& createInitialAdmin) {
    const std::string validationError = validateSetup(state);
    if (!validationError.empty()) {
        state.message = validationError;
        return;
    }

    applyCreateResult(state, createInitialAdmin(
        trimCopy(state.username),
        state.password,
        trimCopy(state.passwordHint)
    ));
}

void handleSetupKey(SetupState& state, const KeyPress& key, const std::function<tundraux::frontend::FacadeResult(
    const std::string&,
    const std::string&,
    const std::string&
)>& createInitialAdmin) {
    if (state.showHelp) {
        if (key.key == Key::Escape || key.key == Key::Enter ||
            key.key == Key::F1 ||
            (key.key == Key::Character && (key.character == 'q' || key.character == 'Q'))) {
            state.showHelp = false;
        }
        return;
    }

    if (state.created) {
        if (key.key == Key::Enter || key.key == Key::Escape ||
            (key.key == Key::Character && (key.character == 'q' || key.character == 'Q'))) {
            return;
        }
        state.message = "Admin user created. Press Enter to continue.";
        return;
    }

    state.message = "Fill the setup form. Enter creates the admin account.";
    switch (key.key) {
        case Key::Up:
            if (state.field > 0) {
                --state.field;
            }
            break;
        case Key::Down:
        case Key::Tab:
            state.field = (state.field + 1) % 4;
            break;
        case Key::Home:
            state.field = 0;
            break;
        case Key::End:
            state.field = 3;
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
            tryCreate(state, createInitialAdmin);
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
        case Key::Escape:
            state.message = "Setup is required before using TundraUX.";
            break;
        case Key::Left:
        case Key::Right:
        case Key::Unknown:
            break;
    }
}

} // namespace

void hello() {
    tundra_tui::colorcout("red", "First-time setup requires backend mode.\n");
}

void hello(tundraux::frontend::BackendFacade& facade) {
    set_title("TundraUX 2.0 init");
    ConsoleScreenGuard screenGuard;

    SetupState state;

    while (true) {
        if (state.showHelp) {
            renderHelp();
        } else {
            renderSetup(state);
        }

        const KeyPress key = readKey();
        if (state.created &&
            (key.key == Key::Enter || key.key == Key::Escape ||
             (key.key == Key::Character && (key.character == 'q' || key.character == 'Q')))) {
            break;
        }
        handleSetupKey(state, key, [&facade](
            const std::string& username,
            const std::string& password,
            const std::string& passwordHint
        ) {
            return facade.createInitialAdmin(username, password, passwordHint);
        });
    }
}
