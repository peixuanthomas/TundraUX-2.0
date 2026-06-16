#include "account_settings.hpp"

#include "backend_facade.hpp"
#include "backend_client.hpp"
#include "backend_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

#include "TundraTUI/color.hpp"
#include "TundraTUI/input.hpp"
#include "TundraTUI/render_engine.hpp"
#include "TundraTUI/screen.hpp"
#include "TundraTUI/style.hpp"
#include "TundraTUI/text.hpp"

namespace {

namespace tui = tundra_tui;

using tundra_tui::ConsoleScreenGuard;
using tundra_tui::colorcout;
using tundra_tui::fitText;
using tundra_tui::Key;
using tundra_tui::KeyPress;
using tundra_tui::readKey;
using tundra_tui::set_title;

void setAuditCurrentUser(
    tundraux::frontend::FrontendAuditSink* auditSink,
    const tundraux::frontend::ShellUser& currentUser
) {
    if (auditSink == nullptr) {
        return;
    }
    auditSink->setCurrentUser(currentUser);
}
void logAuditEvent(
    tundraux::frontend::FrontendAuditSink* auditSink,
    const tundraux::frontend::ShellUser& currentUser,
    const std::string& category,
    const std::string& detail
) {
    if (auditSink == nullptr) {
        return;
    }
    setAuditCurrentUser(auditSink, currentUser);
    auditSink->logEvent(category, detail);
}

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
    tundraux::frontend::ShellUser original;
    std::string originalPassword;
    std::string newPassword;
    std::string confirmPassword;
    std::string passwordHint;
    std::string sourceLabel = "backend current profile";
    std::size_t field = 0;
    bool showPassword = false;
    bool showHelp = false;
    bool saved = false;
    std::string message = "Edit settings. Enter saves changes.";
};

bool usesBackendMode(tundraux::frontend::BackendRuntime* backendRuntime) {
    return backendRuntime != nullptr && backendRuntime->client() != nullptr;
}

tundraux::frontend::ShellUser guestUser() {
    return tundraux::frontend::ShellUser{"guest", "", "", 0};
}

void syncCurrentUserToGuest(
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    currentUser = guestUser();
    setAuditCurrentUser(auditSink, currentUser);
}

std::string backendErrorMessage(
    const std::string& fallback,
    const std::string& errorCode,
    const std::string& backendMessage
) {
    if (!backendMessage.empty()) {
        return backendMessage;
    }
    if (errorCode == "TransportError") {
        return "Backend unavailable.";
    }
    if (errorCode == "SessionExpired") {
        return "Backend session expired.";
    }
    if (errorCode == "PermissionDenied") {
        return "Access Denied.";
    }
    if (errorCode == "InvalidResponse") {
        return "Invalid backend response.";
    }
    return fallback;
}

std::string trimCopy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
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
    return trimCopy(state.passwordHint) != state.original.passwordHint;
}

bool hasAnyChange(const AccountSettingsState& state) {
    return passwordWillChange(state) || hintWillChange(state);
}

std::string effectivePassword(const AccountSettingsState& state) {
    return passwordWillChange(state) ? state.newPassword : state.originalPassword;
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
        {"Failed attempts", std::to_string(state.original.failedCount), false},
        {"Current hint", state.original.passwordHint.empty() ? "(none)" : state.original.passwordHint, false},
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
    const tui::Size size = tui::terminalSize();
    const std::size_t width = std::max<int>(size.width, 92);
    const std::size_t height = std::max<int>(size.height, 20);
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
    std::cout << tui::colorText(state.sourceLabel, tui::kPathStyle) << "\n";
    std::cout << tui::colorText(tui::splitBorder(formWidth, detailsWidth), tui::kBorderStyle) << "\n";
    std::cout << tui::colorText("|", tui::kBorderStyle)
              << headerCell("Settings Form", formWidth)
              << tui::colorText("|", tui::kBorderStyle)
              << headerCell("Details", detailsWidth)
              << tui::colorText("|", tui::kBorderStyle)
              << "\n";
    std::cout << tui::colorText(tui::splitBorder(formWidth, detailsWidth), tui::kBorderStyle) << "\n";

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

    std::cout << tui::colorText(tui::splitBorder(formWidth, detailsWidth), tui::kBorderStyle) << "\n";
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

std::string& activeField(AccountSettingsState& state) {
    switch (state.field) {
        case 0: return state.newPassword;
        case 1: return state.confirmPassword;
        case 2: return state.passwordHint;
        default: return state.newPassword;
    }
}

bool saveSettings(
    AccountSettingsState& state,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    setAuditCurrentUser(auditSink, currentUser);
    const std::string validationError = validateSettings(state);
    if (!validationError.empty()) {
        state.message = validationError;
        logAuditEvent(
            auditSink,
            currentUser,
            "manage",
            "account settings update failure user=" + currentUser.name + " reason=" + validationError
        );
        return false;
    }

    tundraux::frontend::ShellUser updated = state.original;
    const std::string trimmedHint = trimCopy(state.passwordHint);
    if (usesBackendMode(backendRuntime)) {
        tundraux::frontend::BackendFacade facade(*backendRuntime);
        const auto updateResult = facade.updateOwnAccount(
            passwordWillChange(state),
            state.newPassword,
            hintWillChange(state),
            trimmedHint
        );
        if (!updateResult.ok) {
            state.message = updateResult.message.empty() ? "Unable to update account." : updateResult.message;
            logAuditEvent(
                auditSink,
                currentUser,
                "manage",
                "account settings update failure user=" + currentUser.name + " reason=" + state.message
            );
            return false;
        }

        const auto profileResult = facade.refreshProfile();
        if (!profileResult.ok) {
            state.message = profileResult.message.empty() ? "Unable to refresh account." : profileResult.message;
            logAuditEvent(
                auditSink,
                currentUser,
                "manage",
                "account settings update failure user=" + currentUser.name + " reason=" + state.message
            );
            return false;
        } else {
            updated = profileResult.value;
        }
    } else {
        state.message = "Backend unavailable. Account settings require the backend runtime.";
        logAuditEvent(
            auditSink,
            currentUser,
            "manage",
            "account settings update failure user=" + currentUser.name + " reason=backend unavailable"
        );
        return false;
    }

    currentUser = updated;
    state.original = updated;
    state.newPassword.clear();
    state.confirmPassword.clear();
    state.passwordHint = updated.passwordHint;
    state.saved = true;
    state.message = "Settings saved. Press Enter or Esc to return.";
    setAuditCurrentUser(auditSink, updated);
    logAuditEvent(auditSink, currentUser, "manage", "account settings update success user=" + updated.name);
    return true;
}

bool handleSettingsKey(
    AccountSettingsState& state,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink,
    const KeyPress& key
) {
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
            saveSettings(state, currentUser, backendRuntime, auditSink);
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

} // namespace

void open_account_settings(
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    const bool backendMode = usesBackendMode(backendRuntime);

    if (currentUser.type == "debug") {
        colorcout("yellow", "Cannot open account settings as debug user.\n");
        return;
    }
    if (!backendMode) {
        colorcout("red", "Backend unavailable. Account settings require the backend runtime.\n");
        return;
    }
    if (currentUser.type == "guest") {
        colorcout("yellow", "Cannot open account settings as guest user.\n");
        return;
    }

    AccountSettingsState state;
    tundraux::frontend::BackendFacade facade(*backendRuntime);
    const auto profileResult = facade.refreshProfile();
    if (!profileResult.ok) {
        if (profileResult.errorCode == "SessionExpired") {
            syncCurrentUserToGuest(currentUser, auditSink);
        }
        colorcout(
            "red",
            backendErrorMessage(
                "Unable to load account.",
                profileResult.errorCode,
                profileResult.message
            ) + "\n"
        );
        return;
    }
    state.original = profileResult.value;
    state.passwordHint = state.original.passwordHint;
    state.sourceLabel = "backend current profile";
    currentUser = state.original;

    set_title("Account Settings");
    ConsoleScreenGuard screenGuard;

    bool running = true;
    while (running) {
        if (state.showHelp) {
            renderHelp();
        } else {
            renderSettings(state);
        }

        running = handleSettingsKey(state, currentUser, backendRuntime, auditSink, readKey());
    }
}
