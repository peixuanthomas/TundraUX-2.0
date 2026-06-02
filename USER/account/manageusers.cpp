// Attention: Windows only code.
#include "manageusers.hpp"

#include "backend_facade.hpp"
#include "backend_client.hpp"
#include "backend_runtime.hpp"
#include "user_conversion_compat.hpp"
#include "TundraTUI/color.hpp"
#include "TundraTUI/input.hpp"
#include "TundraTUI/render_engine.hpp"
#include "TundraTUI/screen.hpp"
#include "TundraTUI/style.hpp"
#include "TundraTUI/text.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using tundra_tui::colorCellPart;
using tundra_tui::colorText;
using tundra_tui::ConsoleScreenGuard;
using tundra_tui::fitText;
using tundra_tui::Key;
using tundra_tui::KeyPress;
using tundra_tui::readKey;
using tundra_tui::singleBorder;
using tundra_tui::splitBorder;
using tundra_tui::terminalSize;
using tundra_tui::trimToWidth;
using tundra_tui::kBorderStyle;
using tundra_tui::kCopyStyle;
using tundra_tui::kHeaderStyle;
using tundra_tui::kHelpTextStyle;
using tundra_tui::kHintStyle;
using tundra_tui::kInputStyle;
using tundra_tui::kKeyStyle;
using tundra_tui::kPathStyle;
using tundra_tui::kRoleStyle;
using tundra_tui::kSectionStyle;
using tundra_tui::kSelectedMarkStyle;
using tundra_tui::kTitleStyle;
using tundra_tui::kUserStyle;
using tundra_tui::kWarningStyle;

namespace frontend = tundraux::frontend;

struct DetailLine {
    std::string label;
    std::string value;
    bool section = false;
};

struct UserForm {
    bool editing = false;
    std::string originalName;
    std::string name;
    std::string type = "user";
    std::string password;
    std::string passwordHint;
    std::string count = "0";
    std::size_t field = 0;
    std::string error;
};

struct UserManagerState {
    std::vector<frontend::FrontendUser> users;
    std::size_t cursor = 0;
    std::size_t scroll = 0;
    bool showHelp = false;
    bool formOpen = false;
    bool confirmDelete = false;
    bool forceExit = false;
    UserForm form;
    std::string pendingDeleteName;
    std::string message = "Ready";
    std::string lastBackendErrorCode;
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr;
};

struct UserManagerBackend {
    frontend::BackendRuntime& runtime;
    frontend::BackendClient& client;
    std::string& sessionId;
};

std::string trimCopy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string backendFailureMessage(const std::string& fallback, const std::string& errorCode) {
    if (errorCode == "TransportError") {
        return "Backend unavailable.";
    }
    if (errorCode == "InvalidResponse") {
        return "Invalid backend response.";
    }
    if (errorCode == "SessionExpired") {
        return "Backend session expired.";
    }
    if (errorCode == "PermissionDenied") {
        return "Access Denied.";
    }
    if (errorCode == "NotFound") {
        return "User not found.";
    }
    return fallback;
}

USER guestUser() {
    return USER{"guest", "", "", "", 0};
}

void syncAuditUser(
    UserManagerState& state,
    USER& currentUser,
    USER user
) {
    currentUser = std::move(user);
    if (state.auditSink != nullptr) {
        state.auditSink->setCurrentUser(frontend::toShellUser(currentUser));
    }
}

void logAuditEvent(
    UserManagerState& state,
    USER& currentUser,
    const std::string& category,
    const std::string& detail
) {
    if (state.auditSink == nullptr) {
        return;
    }
    state.auditSink->setCurrentUser(frontend::toShellUser(currentUser));
    state.auditSink->logEvent(category, detail);
}

bool isTerminalBackendFailure(const std::string& errorCode) {
    return errorCode == "PermissionDenied" ||
           errorCode == "SessionExpired" ||
           errorCode == "TransportError" ||
           errorCode == "InvalidResponse";
}

bool syncCurrentUserFromBackend(
    UserManagerBackend& backend,
    USER& currentUser,
    UserManagerState& state,
    frontend::BackendFacade& facade,
    std::string* errorCode
) {
    backend.runtime.setSessionId(backend.sessionId);
    const auto profile = facade.refreshProfile();
    backend.sessionId = backend.runtime.sessionId();
    if (errorCode != nullptr) {
        errorCode->clear();
    }

    if (profile.ok) {
        syncAuditUser(state, currentUser, frontend::toLegacyUser(profile.value));
        return true;
    }

    if (errorCode != nullptr) {
        *errorCode = profile.errorCode;
    }
    syncAuditUser(state, currentUser, guestUser());
    return false;
}

bool handleTerminalBackendFailure(
    UserManagerBackend& backend,
    UserManagerState& state,
    USER& currentUser,
    frontend::BackendFacade& facade,
    const std::string& errorCode,
    const std::string& fallback
) {
    if (!isTerminalBackendFailure(errorCode)) {
        return false;
    }

    state.message = backendFailureMessage(fallback, errorCode);
    state.forceExit = true;
    if (errorCode == "PermissionDenied" || errorCode == "SessionExpired") {
        syncCurrentUserFromBackend(backend, currentUser, state, facade, nullptr);
    } else {
        syncAuditUser(state, currentUser, guestUser());
    }
    return true;
}

bool refreshUsers(UserManagerBackend& backend, UserManagerState& state) {
    const auto result = backend.client.listUsers(backend.sessionId);
    if (!result.ok) {
        state.users.clear();
        state.cursor = 0;
        state.scroll = 0;
        state.lastBackendErrorCode = result.errorCode;
        state.message = backendFailureMessage("Unable to list users.", result.errorCode);
        return false;
    }

    state.lastBackendErrorCode.clear();
    state.users = result.value;
    if (state.users.empty()) {
        state.cursor = 0;
        state.scroll = 0;
    } else if (state.cursor >= state.users.size()) {
        state.cursor = state.users.size() - 1;
    }
    return true;
}

bool syncCurrentUserAfterMutation(
    UserManagerBackend& backend,
    USER& currentUser,
    UserManagerState& state,
    frontend::BackendFacade& facade
) {
    std::string syncError;
    if (!syncCurrentUserFromBackend(backend, currentUser, state, facade, &syncError)) {
        state.message = backendFailureMessage(
            "Your account lost management privileges. Returning to shell.",
            syncError.empty() ? "PermissionDenied" : syncError
        );
        state.forceExit = true;
        return false;
    }
    return true;
}

std::size_t userCount(const UserManagerState& state) {
    return state.users.size();
}

const frontend::FrontendUser* selectedUser(const UserManagerState& state) {
    if (state.users.empty() || state.cursor >= state.users.size()) {
        return nullptr;
    }
    return &state.users[state.cursor];
}

void clampCursor(UserManagerState& state) {
    if (state.users.empty()) {
        state.cursor = 0;
        state.scroll = 0;
        return;
    }
    if (state.cursor >= state.users.size()) {
        state.cursor = state.users.size() - 1;
    }
}

void selectUserByName(UserManagerState& state, const std::string& name) {
    for (std::size_t i = 0; i < state.users.size(); ++i) {
        if (state.users[i].name == name) {
            state.cursor = i;
            return;
        }
    }
    clampCursor(state);
}

void keepCursorVisible(UserManagerState& state, std::size_t rows) {
    clampCursor(state);
    if (rows == 0) {
        state.scroll = 0;
        return;
    }
    if (state.cursor < state.scroll) {
        state.scroll = state.cursor;
    } else if (state.cursor >= state.scroll + rows) {
        state.scroll = state.cursor - rows + 1;
    }
}

std::string headerCell(const std::string& text, std::size_t width) {
    return colorText(fitText(" " + text, width), kHeaderStyle);
}

const char* typeStyle(const std::string& type) {
    const std::string normalized = toLowerCopy(type);
    if (normalized == "admin") {
        return kRoleStyle;
    }
    if (normalized == "debug") {
        return kWarningStyle;
    }
    return kUserStyle;
}

const char* statusStyle(const std::string& value) {
    const std::string normalized = toLowerCopy(value);
    if (normalized.find("locked") != std::string::npos ||
        normalized.find("error") != std::string::npos ||
        normalized.find("denied") != std::string::npos ||
        normalized.find("expired") != std::string::npos ||
        normalized.find("unavailable") != std::string::npos) {
        return kWarningStyle;
    }
    if (normalized.find("active") != std::string::npos ||
        normalized.find("saved") != std::string::npos ||
        normalized.find("created") != std::string::npos ||
        normalized.find("updated") != std::string::npos ||
        normalized.find("deleted") != std::string::npos ||
        normalized.find("reset") != std::string::npos ||
        normalized.find("disabled") != std::string::npos) {
        return kCopyStyle;
    }
    if (normalized == "(none)" || normalized == "(empty)" || normalized == "(hidden)") {
        return kHintStyle;
    }
    return kHelpTextStyle;
}

std::string detailLineText(const DetailLine& line, std::size_t width) {
    if (line.section) {
        return colorText(fitText(" " + line.label, width), kHeaderStyle);
    }

    const std::size_t labelWidth = std::min<std::size_t>(18, std::max<std::size_t>(10, width / 3));
    const std::size_t valueWidth = width > labelWidth + 1 ? width - labelWidth - 1 : 0;
    return colorText(fitText(line.label + ":", labelWidth), kKeyStyle) +
           " " +
           colorText(fitText(line.value, valueWidth), statusStyle(line.value));
}

std::string formatUserCell(const frontend::FrontendUser* user, bool selected, std::size_t width) {
    if (user == nullptr) {
        return std::string(width, ' ');
    }

    const std::string marker = selected ? "> " : "  ";
    const std::string type = "[" + user->type + "]";
    const std::string attempts = " " + std::to_string(user->failedCount) + "/7";
    const std::string prefix = marker + type + " ";
    const std::size_t fixedWidth = prefix.size() + attempts.size();

    if (fixedWidth >= width) {
        return colorCellPart(fitText(marker + user->name, width), typeStyle(user->type), selected);
    }

    const std::string name = fitText(trimToWidth(user->name, width - fixedWidth), width - fixedWidth);
    return colorCellPart(marker, selected ? kSelectedMarkStyle : kHintStyle, selected) +
           colorCellPart(type, typeStyle(user->type), selected) +
           colorCellPart(" ", kHelpTextStyle, selected) +
           colorCellPart(name, kHelpTextStyle, selected) +
           colorCellPart(attempts, user->failedCount > 7 ? kWarningStyle : kHintStyle, selected);
}

std::vector<DetailLine> buildDetailLines(const frontend::FrontendUser* user) {
    if (user == nullptr) {
        return {
            {"No user selected", "", true},
            {"Action", "Press a to create a user.", false}
        };
    }

    return {
        {"Account", "", true},
        {"Name", user->name, false},
        {"Type", user->type, false},
        {"Password", "(hidden by backend)", false},
        {"Password hint", user->passwordHint.empty() ? "(none)" : user->passwordHint, false},
        {"Failed attempts", std::to_string(user->failedCount), false},
        {"Status", user->failedCount > 7 ? "Locked" : "Active", false},
        {"Actions", "", true},
        {"Enter / e", "Edit selected user", false},
        {"r", "Reset failed attempts", false},
        {"x", "Disable selected user", false},
        {"d", "Delete selected user", false}
    };
}

void renderHelpBinding(const std::string& keys, const std::string& description) {
    std::cout << "  "
              << colorText(fitText(keys, 22), kKeyStyle)
              << colorText(description, kHelpTextStyle)
              << "\n";
}

void renderHelp() {
    std::cout << "\x1b[0m\x1b[2J\x1b[H\x1b[?25l";
    std::cout << colorText("TundraUX User Management Help", kTitleStyle) << "\n\n";
    std::cout << colorText("Navigation", kSectionStyle) << "\n";
    renderHelpBinding("Up/Down or j/k", "Move through users or form fields");
    renderHelpBinding("Home/End", "Jump to first or last user");
    std::cout << "\n";

    std::cout << colorText("Actions", kSectionStyle) << "\n";
    renderHelpBinding("a", "Add a user with the form");
    renderHelpBinding("Enter or e", "Edit the selected user");
    renderHelpBinding("d", "Delete the selected user after confirmation");
    renderHelpBinding("r", "Reset failed login attempts");
    renderHelpBinding("x", "Disable the selected user");
    std::cout << "\n";

    std::cout << colorText("Form editing", kSectionStyle) << "\n";
    renderHelpBinding("Type / Backspace", "Edit the active field");
    renderHelpBinding("Left/Right/Space", "Toggle the user type field");
    renderHelpBinding("Tab", "Move to the next field");
    renderHelpBinding("Enter", "Save the form");
    renderHelpBinding("Esc", "Cancel or close help");
    std::cout << "\n";
    std::cout << colorText("Press h, q, Esc, or Enter to return.", kHintStyle) << std::flush;
}

std::string formFieldValue(const UserForm& form, std::size_t index) {
    switch (index) {
        case 0: return form.name;
        case 1: return form.type;
        case 2: return form.password;
        case 3: return form.passwordHint;
        case 4: return form.count;
        default: return "";
    }
}

std::string formFieldLabel(const UserForm& form, std::size_t index) {
    switch (index) {
        case 0: return "Username";
        case 1: return "Type";
        case 2: return form.editing ? "Password *" : "Password";
        case 3: return "Password hint";
        case 4: return "Failed count";
        default: return "";
    }
}

std::string formatFormLine(const UserForm& form, std::size_t index, std::size_t width) {
    const bool selected = form.field == index;
    const std::string marker = selected ? "> " : "  ";
    const std::string label = fitText(formFieldLabel(form, index), 16);
    std::string value = formFieldValue(form, index);
    if (index == 1) {
        value += "  (Left/Right/Space)";
    } else if (index == 2 && form.editing && value.empty()) {
        value = "(leave unchanged)";
    }

    const std::string prefix = marker + label + " ";
    const std::size_t valueWidth = width > prefix.size() ? width - prefix.size() : 0;
    return colorCellPart(marker, selected ? kSelectedMarkStyle : kHintStyle, selected) +
           colorCellPart(label, kKeyStyle, selected) +
           colorCellPart(" ", kHelpTextStyle, selected) +
           colorCellPart(fitText(value, valueWidth), index == 1 ? typeStyle(form.type) : kInputStyle, selected);
}

void renderForm(const UserForm& form) {
    const tundra_tui::Size size = terminalSize();
    const std::size_t width = std::max<int>(size.width, 90);
    const std::size_t contentWidth = width > 2 ? width - 2 : width;

    std::cout << "\x1b[0m\x1b[2J\x1b[H\x1b[?25l";
    std::cout << colorText(form.editing ? "Edit User" : "Add User", kTitleStyle)
              << colorText(" - backend form", kHintStyle)
              << "\n";
    std::cout << colorText(singleBorder(width), kBorderStyle) << "\n";
    std::cout << colorText("|", kBorderStyle)
              << headerCell(form.editing ? "Update account" : "Create account", contentWidth)
              << colorText("|", kBorderStyle)
              << "\n";
    std::cout << colorText(singleBorder(width), kBorderStyle) << "\n";

    for (std::size_t i = 0; i < 5; ++i) {
        std::cout << colorText("|", kBorderStyle)
                  << formatFormLine(form, i, contentWidth)
                  << colorText("|", kBorderStyle)
                  << "\n";
    }

    std::cout << colorText(singleBorder(width), kBorderStyle) << "\n";
    std::cout << colorText("Up/Down", kKeyStyle)
              << colorText(" field | ", kHintStyle)
              << colorText("Tab", kKeyStyle)
              << colorText(" next | ", kHintStyle)
              << colorText("Enter", kKeyStyle)
              << colorText(" save | ", kHintStyle)
              << colorText("Esc", kKeyStyle)
              << colorText(" cancel", kHintStyle)
              << "\n";
    if (!form.error.empty()) {
        std::cout << colorText("Error: ", kWarningStyle)
                  << colorText(form.error, kWarningStyle);
    } else {
        std::cout << colorText("Status: ", kSectionStyle)
                  << colorText(form.editing ? "* blank password keeps the backend value." : "Fill the highlighted field directly.", kHelpTextStyle);
    }
    std::cout << std::flush;
}

void renderMain(const UserManagerState& state) {
    const tundra_tui::Size size = terminalSize();
    const std::size_t width = std::max<int>(size.width, 90);
    const std::size_t height = std::max<int>(size.height, 18);
    const std::size_t rows = height > 8 ? height - 8 : 10;
    const std::size_t usableWidth = width > 3 ? width - 3 : width;
    const std::size_t usersWidth = std::max<std::size_t>(30, usableWidth * 40 / 100);
    const std::size_t detailsWidth = usableWidth - usersWidth;
    const auto details = buildDetailLines(selectedUser(state));

    std::cout << "\x1b[0m\x1b[2J\x1b[H\x1b[?25l";
    std::cout << colorText("TundraUX User Management", kTitleStyle)
              << colorText(" - ", kHintStyle)
              << colorText(std::to_string(userCount(state)) + " users", kPathStyle)
              << "\n";
    std::cout << colorText("backend RPC", kPathStyle) << "\n";
    std::cout << colorText(splitBorder(usersWidth, detailsWidth), kBorderStyle) << "\n";
    std::cout << colorText("|", kBorderStyle)
              << headerCell("Users", usersWidth)
              << colorText("|", kBorderStyle)
              << headerCell("Details", detailsWidth)
              << colorText("|", kBorderStyle)
              << "\n";
    std::cout << colorText(splitBorder(usersWidth, detailsWidth), kBorderStyle) << "\n";

    for (std::size_t rowIndex = 0; rowIndex < rows; ++rowIndex) {
        const std::size_t userIndex = state.scroll + rowIndex;
        const frontend::FrontendUser* user = userIndex < state.users.size() ? &state.users[userIndex] : nullptr;
        const std::string userText = formatUserCell(
            user,
            userIndex == state.cursor && user != nullptr,
            usersWidth
        );
        const std::string detailText = rowIndex < details.size()
            ? detailLineText(details[rowIndex], detailsWidth)
            : std::string(detailsWidth, ' ');

        std::cout << colorText("|", kBorderStyle)
                  << userText
                  << colorText("|", kBorderStyle)
                  << detailText
                  << colorText("|", kBorderStyle)
                  << "\n";
    }

    std::cout << colorText(splitBorder(usersWidth, detailsWidth), kBorderStyle) << "\n";
    if (state.confirmDelete) {
        std::cout << colorText("Delete ", kWarningStyle)
                  << colorText(state.pendingDeleteName, kUserStyle)
                  << colorText("? ", kWarningStyle)
                  << colorText("y", kKeyStyle)
                  << colorText(" confirm | ", kHintStyle)
                  << colorText("n/Esc", kKeyStyle)
                  << colorText(" cancel", kHintStyle)
                  << "\n";
    } else {
        std::cout << colorText("Up/Down", kKeyStyle)
                  << colorText(" select | ", kHintStyle)
                  << colorText("Enter/e", kKeyStyle)
                  << colorText(" edit | ", kHintStyle)
                  << colorText("a", kKeyStyle)
                  << colorText(" add | ", kHintStyle)
                  << colorText("d", kKeyStyle)
                  << colorText(" delete | ", kHintStyle)
                  << colorText("r", kKeyStyle)
                  << colorText(" reset | ", kHintStyle)
                  << colorText("x", kKeyStyle)
                  << colorText(" disable | ", kHintStyle)
                  << colorText("h", kKeyStyle)
                  << colorText(" help | ", kHintStyle)
                  << colorText("q", kKeyStyle)
                  << colorText(" quit", kHintStyle)
                  << "\n";
    }
    std::cout << colorText("Status: ", kSectionStyle)
              << colorText(state.message, statusStyle(state.message))
              << std::flush;
}

void moveUp(UserManagerState& state) {
    if (state.cursor > 0) {
        --state.cursor;
    }
}

void moveDown(UserManagerState& state) {
    if (state.cursor + 1 < userCount(state)) {
        ++state.cursor;
    }
}

void beginAdd(UserManagerState& state) {
    state.form = UserForm{};
    state.formOpen = true;
    state.confirmDelete = false;
}

void beginEdit(UserManagerState& state, const frontend::FrontendUser& user) {
    state.form = UserForm{};
    state.form.editing = true;
    state.form.originalName = user.name;
    state.form.name = user.name;
    state.form.type = user.type;
    state.form.password.clear();
    state.form.passwordHint = user.passwordHint;
    state.form.count = std::to_string(user.failedCount);
    state.formOpen = true;
    state.confirmDelete = false;
}

bool parseCount(const std::string& value, int& parsed) {
    const std::string trimmed = trimCopy(value);
    if (trimmed.empty()) {
        return false;
    }
    for (char ch : trimmed) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }

    try {
        parsed = std::stoi(trimmed);
    } catch (...) {
        return false;
    }
    return true;
}

bool buildFormUser(const UserForm& form, frontend::FrontendUser& user) {
    user.name = trimCopy(form.name);
    user.type = toLowerCopy(trimCopy(form.type));
    user.passwordHint = trimCopy(form.passwordHint);
    if (!parseCount(form.count, user.failedCount)) {
        return false;
    }
    return true;
}

void saveForm(
    UserManagerState& state,
    UserManagerBackend& backend,
    USER& currentUser,
    frontend::BackendFacade& facade
) {
    frontend::FrontendUser user;
    if (!buildFormUser(state.form, user)) {
        state.form.error = "Failed count must be a number.";
        return;
    }

    frontend::ClientResult<bool> result;
    if (state.form.editing) {
        const bool passwordProvided = !state.form.password.empty();
        result = backend.client.updateUser(
            backend.sessionId,
            state.form.originalName,
            user,
            passwordProvided,
            state.form.password
        );
    } else {
        result = backend.client.createUser(backend.sessionId, user, state.form.password);
    }

    if (!result.ok || !result.value) {
        if (!result.ok && handleTerminalBackendFailure(
                backend,
                state,
                currentUser,
                facade,
                result.errorCode,
                "Unable to save user.")) {
            state.formOpen = false;
            return;
        }
        state.form.error = result.ok ? "Backend rejected the user operation." : result.message;
        if (state.form.error.empty()) {
            state.form.error = backendFailureMessage("Unable to save user.", result.errorCode);
        }
        return;
    }

    state.formOpen = false;
    state.message = state.form.editing ? "User updated: " + user.name : "User created: " + user.name;
    logAuditEvent(
        state,
        currentUser,
        "manage-users",
        std::string(state.form.editing ? "backend edit " : "backend create ") + user.name
    );
    syncCurrentUserAfterMutation(backend, currentUser, state, facade);
    if (!state.forceExit) {
        if (refreshUsers(backend, state)) {
            selectUserByName(state, user.name);
        } else {
            handleTerminalBackendFailure(
                backend,
                state,
                currentUser,
                facade,
                state.lastBackendErrorCode,
                "Unable to list users."
            );
        }
    }
}

std::string& activeTextField(UserForm& form) {
    switch (form.field) {
        case 0: return form.name;
        case 2: return form.password;
        case 3: return form.passwordHint;
        case 4: return form.count;
        default: return form.name;
    }
}

void toggleType(UserForm& form) {
    form.type = toLowerCopy(form.type) == "admin" ? "user" : "admin";
}

void handleFormKey(
    UserManagerState& state,
    UserManagerBackend& backend,
    USER& currentUser,
    frontend::BackendFacade& facade,
    const KeyPress& key
) {
    UserForm& form = state.form;
    form.error.clear();

    switch (key.key) {
        case Key::Escape:
            state.formOpen = false;
            state.message = form.editing ? "Edit cancelled." : "Add cancelled.";
            break;
        case Key::Up:
            if (form.field > 0) {
                --form.field;
            }
            break;
        case Key::Down:
        case Key::Tab:
            form.field = (form.field + 1) % 5;
            break;
        case Key::Left:
        case Key::Right:
            if (form.field == 1) {
                toggleType(form);
            }
            break;
        case Key::Home:
            form.field = 0;
            break;
        case Key::End:
            form.field = 4;
            break;
        case Key::Backspace:
            if (form.field != 1) {
                std::string& value = activeTextField(form);
                if (!value.empty()) {
                    value.pop_back();
                }
            }
            break;
        case Key::Delete:
            if (form.field != 1) {
                activeTextField(form).clear();
            }
            break;
        case Key::Enter:
            saveForm(state, backend, currentUser, facade);
            break;
        case Key::Character:
            if (form.field == 1) {
                if (key.character == ' ' || key.character == 'a' || key.character == 'A' ||
                    key.character == 'u' || key.character == 'U') {
                    if (key.character == 'a' || key.character == 'A') {
                        form.type = "admin";
                    } else if (key.character == 'u' || key.character == 'U') {
                        form.type = "user";
                    } else {
                        toggleType(form);
                    }
                }
            } else if (form.field == 4) {
                if (std::isdigit(static_cast<unsigned char>(key.character))) {
                    activeTextField(form).push_back(key.character);
                }
            } else {
                activeTextField(form).push_back(key.character);
            }
            break;
        case Key::Unknown:
            break;
    }
}

void deleteSelected(
    UserManagerState& state,
    UserManagerBackend& backend,
    USER& currentUser,
    frontend::BackendFacade& facade
) {
    const frontend::FrontendUser* user = selectedUser(state);
    if (user == nullptr) {
        state.message = "No user selected.";
        state.confirmDelete = false;
        return;
    }

    const std::string name = user->name;
    const auto result = backend.client.deleteUser(backend.sessionId, name);
    if (!result.ok || !result.value) {
        if (!result.ok && handleTerminalBackendFailure(
                backend,
                state,
                currentUser,
                facade,
                result.errorCode,
                "Unable to delete user.")) {
            state.confirmDelete = false;
            return;
        }
        state.message = result.ok ? "Failed to delete user." : backendFailureMessage(result.message, result.errorCode);
        state.confirmDelete = false;
        return;
    }

    state.message = "User deleted: " + name;
    logAuditEvent(state, currentUser, "manage-users", "backend delete " + name);
    syncCurrentUserAfterMutation(backend, currentUser, state, facade);
    if (!state.forceExit) {
        if (refreshUsers(backend, state)) {
            clampCursor(state);
        } else {
            handleTerminalBackendFailure(
                backend,
                state,
                currentUser,
                facade,
                state.lastBackendErrorCode,
                "Unable to list users."
            );
        }
    }
    state.confirmDelete = false;
}

void disableSelected(
    UserManagerState& state,
    UserManagerBackend& backend,
    USER& currentUser,
    frontend::BackendFacade& facade
) {
    const frontend::FrontendUser* user = selectedUser(state);
    if (user == nullptr) {
        state.message = "No user selected.";
        return;
    }

    const std::string name = user->name;
    const auto result = backend.client.disableUser(backend.sessionId, name);
    if (!result.ok || !result.value) {
        if (!result.ok && handleTerminalBackendFailure(
                backend,
                state,
                currentUser,
                facade,
                result.errorCode,
                "Unable to disable user.")) {
            return;
        }
        state.message = result.ok ? "Failed to disable user." : backendFailureMessage(result.message, result.errorCode);
        return;
    }

    state.message = "User disabled: " + name;
    logAuditEvent(state, currentUser, "manage-users", "backend disable " + name);
    syncCurrentUserAfterMutation(backend, currentUser, state, facade);
    if (!state.forceExit) {
        if (refreshUsers(backend, state)) {
            selectUserByName(state, name);
        } else {
            handleTerminalBackendFailure(
                backend,
                state,
                currentUser,
                facade,
                state.lastBackendErrorCode,
                "Unable to list users."
            );
        }
    }
}

void resetSelected(
    UserManagerState& state,
    UserManagerBackend& backend,
    USER& currentUser,
    frontend::BackendFacade& facade
) {
    const frontend::FrontendUser* user = selectedUser(state);
    if (user == nullptr) {
        state.message = "No user selected.";
        return;
    }

    const std::string name = user->name;
    const auto result = backend.client.resetFailedCount(backend.sessionId, name);
    if (!result.ok || !result.value) {
        if (!result.ok && handleTerminalBackendFailure(
                backend,
                state,
                currentUser,
                facade,
                result.errorCode,
                "Unable to reset count.")) {
            return;
        }
        state.message = result.ok ? "Failed to reset count." : backendFailureMessage(result.message, result.errorCode);
        return;
    }

    state.message = "Login count reset: " + name;
    logAuditEvent(state, currentUser, "manage-users", "backend reset " + name);
    syncCurrentUserAfterMutation(backend, currentUser, state, facade);
    if (!state.forceExit) {
        if (refreshUsers(backend, state)) {
            selectUserByName(state, name);
        } else {
            handleTerminalBackendFailure(
                backend,
                state,
                currentUser,
                facade,
                state.lastBackendErrorCode,
                "Unable to list users."
            );
        }
    }
}

bool handleMainKey(
    UserManagerState& state,
    UserManagerBackend& backend,
    USER& currentUser,
    frontend::BackendFacade& facade,
    const KeyPress& key
) {
    if (state.showHelp) {
        if (key.key == Key::Escape || key.key == Key::Enter ||
            (key.key == Key::Character &&
             (key.character == 'h' || key.character == 'H' ||
              key.character == 'q' || key.character == 'Q'))) {
            state.showHelp = false;
        }
        return !state.forceExit;
    }

    if (state.formOpen) {
        handleFormKey(state, backend, currentUser, facade, key);
        return !state.forceExit;
    }

    if (state.confirmDelete) {
        if (key.key == Key::Escape ||
            (key.key == Key::Character && (key.character == 'n' || key.character == 'N'))) {
            state.confirmDelete = false;
            state.message = "Delete cancelled.";
            return !state.forceExit;
        }
        if (key.key == Key::Enter ||
            (key.key == Key::Character && (key.character == 'y' || key.character == 'Y'))) {
            deleteSelected(state, backend, currentUser, facade);
            return !state.forceExit;
        }
        return !state.forceExit;
    }

    switch (key.key) {
        case Key::Escape:
            return false;
        case Key::Up:
            moveUp(state);
            break;
        case Key::Down:
            moveDown(state);
            break;
        case Key::Home:
            state.cursor = 0;
            break;
        case Key::End:
            state.cursor = userCount(state) == 0 ? 0 : userCount(state) - 1;
            break;
        case Key::Enter:
            if (const frontend::FrontendUser* user = selectedUser(state)) {
                beginEdit(state, *user);
            } else {
                state.message = "No user selected.";
            }
            break;
        case Key::Character:
            switch (key.character) {
                case 'q':
                case 'Q':
                    return false;
                case 'j':
                    moveDown(state);
                    break;
                case 'k':
                    moveUp(state);
                    break;
                case 'g':
                    state.cursor = 0;
                    break;
                case 'G':
                    state.cursor = userCount(state) == 0 ? 0 : userCount(state) - 1;
                    break;
                case 'h':
                case 'H':
                    state.showHelp = true;
                    break;
                case 'a':
                case 'A':
                    beginAdd(state);
                    break;
                case 'e':
                case 'E':
                    if (const frontend::FrontendUser* user = selectedUser(state)) {
                        beginEdit(state, *user);
                    } else {
                        state.message = "No user selected.";
                    }
                    break;
                case 'd':
                case 'D':
                    if (const frontend::FrontendUser* user = selectedUser(state)) {
                        state.pendingDeleteName = user->name;
                        state.confirmDelete = true;
                        state.message = "Confirm delete.";
                    } else {
                        state.message = "No user selected.";
                    }
                    break;
                case 'r':
                case 'R':
                    resetSelected(state, backend, currentUser, facade);
                    break;
                case 'x':
                case 'X':
                    disableSelected(state, backend, currentUser, facade);
                    break;
                default:
                    state.message = std::string("Unknown key: ") + key.character;
                    break;
            }
            break;
        case Key::Left:
        case Key::Right:
        case Key::Backspace:
        case Key::Delete:
        case Key::Tab:
        case Key::Unknown:
            break;
    }

    return !state.forceExit;
}

void renderBackendUnavailable(const std::string& message) {
    tundra_tui::colorcout("red", message + "\n");
}

} // namespace

void manage_users(
    USER& currentUser,
    frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    tundra_tui::set_title("User Management");

    if (backendRuntime == nullptr || backendRuntime->legacyDirect() ||
        backendRuntime->client() == nullptr || backendRuntime->sessionId().empty()) {
        renderBackendUnavailable("User management requires an active backend session.");
        if (auditSink != nullptr) {
            auditSink->setCurrentUser(frontend::toShellUser(currentUser));
        }
        return;
    }

    std::string sessionId = backendRuntime->sessionId();
    UserManagerBackend backend{*backendRuntime, *backendRuntime->client(), sessionId};
    frontend::BackendFacade facade(*backendRuntime);
    ConsoleScreenGuard screenGuard;
    UserManagerState state;
    state.auditSink = auditSink;
    if (!refreshUsers(backend, state)) {
        handleTerminalBackendFailure(
            backend,
            state,
            currentUser,
            facade,
            state.lastBackendErrorCode,
            "Unable to list users."
        );
    }
    if (state.forceExit) {
        backendRuntime->setSessionId(sessionId);
        if (state.auditSink != nullptr) {
            state.auditSink->setCurrentUser(frontend::toShellUser(currentUser));
        }
        return;
    }

    bool running = true;
    while (running) {
        const tundra_tui::Size size = terminalSize();
        const std::size_t rows = std::max<int>(size.height, 18) > 8
            ? static_cast<std::size_t>(std::max<int>(size.height, 18) - 8)
            : 10;
        keepCursorVisible(state, rows);

        if (state.showHelp) {
            renderHelp();
        } else if (state.formOpen) {
            renderForm(state.form);
        } else {
            renderMain(state);
        }

        running = handleMainKey(state, backend, currentUser, facade, readKey());
    }

    backendRuntime->setSessionId(sessionId);
    if (state.auditSink != nullptr) {
        state.auditSink->setCurrentUser(frontend::toShellUser(currentUser));
    }
}
