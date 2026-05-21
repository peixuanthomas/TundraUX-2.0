// Attention: Windows only code.
#include "manageusers.hpp"

#include "TundraTUI/color.hpp"
#include "TundraTUI/input.hpp"
#include "TundraTUI/render_engine.hpp"
#include "TundraTUI/screen.hpp"
#include "TundraTUI/style.hpp"
#include "TundraTUI/text.hpp"
#include "audit_log.hpp"
#include "udata.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
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

struct DetailLine {
    std::string label;
    std::string value;
    bool section = false;
};

struct PasswordStatus {
    bool hasMinLength = false;
    bool hasUpper = false;
    bool hasLower = false;
    bool hasDigit = false;
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
    std::size_t cursor = 0;
    std::size_t scroll = 0;
    bool showHelp = false;
    bool showPassword = false;
    bool formOpen = false;
    bool confirmDelete = false;
    bool forceExit = false;
    UserForm form;
    std::string pendingDeleteName;
    std::string message = "Ready";
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

bool hasWhitespace(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
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

std::string maskText(const std::string& value) {
    if (value.empty()) {
        return "(empty)";
    }
    return std::string(value.size(), '*');
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
        normalized.find("denied") != std::string::npos) {
        return kWarningStyle;
    }
    if (normalized.find("active") != std::string::npos ||
        normalized.find("saved") != std::string::npos ||
        normalized.find("created") != std::string::npos ||
        normalized.find("updated") != std::string::npos ||
        normalized.find("deleted") != std::string::npos) {
        return kCopyStyle;
    }
    if (normalized == "(none)" || normalized == "(empty)") {
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

std::size_t userCount(const DataManager& dataManager) {
    return dataManager.GetAllUsers().size();
}

const USER* selectedUser(const DataManager& dataManager, const UserManagerState& state) {
    const auto& users = dataManager.GetAllUsers();
    if (users.empty() || state.cursor >= users.size()) {
        return nullptr;
    }
    return &users[state.cursor];
}

std::size_t activeAdminCount(const DataManager& dataManager, const std::string& excludingName = "") {
    std::size_t count = 0;
    for (const auto& user : dataManager.GetAllUsers()) {
        if (user.name == excludingName) {
            continue;
        }
        if (toLowerCopy(user.type) == "admin" && user.count <= 7) {
            ++count;
        }
    }
    return count;
}

bool isOnlyActiveAdmin(const DataManager& dataManager, const USER& user) {
    return toLowerCopy(user.type) == "admin" &&
           user.count <= 7 &&
           activeAdminCount(dataManager, user.name) == 0;
}

bool nameExists(const DataManager& dataManager, const std::string& name, const std::string& exceptName = "") {
    for (const auto& user : dataManager.GetAllUsers()) {
        if (user.name == name && user.name != exceptName) {
            return true;
        }
    }
    return false;
}

bool isDebugType(const std::string& type) {
    return toLowerCopy(type) == "debug";
}

bool isAdminOrDebugType(const std::string& type) {
    const std::string normalized = toLowerCopy(type);
    return normalized == "admin" || normalized == "debug";
}

bool syncCurrentUserAfterMutation(USER& currentUser, std::string& message) {
    if (isDebugType(currentUser.type)) {
        tundraux::audit::setCurrentUser(currentUser);
        return true;
    }

    const std::string lookupName = currentUser.name;
    bool found = false;
    USER refreshed = {"guest", "", "", "", 0};

    if (!lookupName.empty()) {
        try {
            DataManager refreshedData("user_data.dat");
            const auto& users = refreshedData.GetAllUsers();
            const auto it = std::find_if(users.begin(), users.end(), [&](const USER& user) {
                return user.name == lookupName;
            });
            if (it != users.end()) {
                refreshed = *it;
                found = true;
            }
        } catch (...) {
            found = false;
        }
    }

    if (!found || refreshed.count > 7 || !isAdminOrDebugType(refreshed.type)) {
        currentUser = {"guest", "", "", "", 0};
        tundraux::audit::setCurrentUser(currentUser);
        message = "Your account lost management privileges. Returning to shell.";
        return false;
    }

    currentUser = refreshed;
    tundraux::audit::setCurrentUser(currentUser);
    return true;
}

void clampCursor(UserManagerState& state, const DataManager& dataManager) {
    const std::size_t count = userCount(dataManager);
    if (count == 0) {
        state.cursor = 0;
        state.scroll = 0;
        return;
    }
    if (state.cursor >= count) {
        state.cursor = count - 1;
    }
}

void selectUserByName(UserManagerState& state, const DataManager& dataManager, const std::string& name) {
    const auto& users = dataManager.GetAllUsers();
    for (std::size_t i = 0; i < users.size(); ++i) {
        if (users[i].name == name) {
            state.cursor = i;
            return;
        }
    }
    clampCursor(state, dataManager);
}

void keepCursorVisible(UserManagerState& state, const DataManager& dataManager, std::size_t rows) {
    clampCursor(state, dataManager);
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

std::string formatUserCell(const USER* user, bool selected, std::size_t width) {
    if (user == nullptr) {
        return std::string(width, ' ');
    }

    const std::string marker = selected ? "> " : "  ";
    const std::string type = "[" + user->type + "]";
    const std::string attempts = " " + std::to_string(user->count) + "/7";
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
           colorCellPart(attempts, user->count > 7 ? kWarningStyle : kHintStyle, selected);
}

std::vector<DetailLine> buildDetailLines(const USER* user, bool showPassword) {
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
        {"Password", showPassword ? user->password : maskText(user->password), false},
        {"Password hint", user->password_hint.empty() ? "(none)" : user->password_hint, false},
        {"Failed attempts", std::to_string(user->count), false},
        {"Status", user->count > 7 ? "Locked" : "Active", false},
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
    renderHelpBinding("p", "Show or hide the password in details");
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

std::string formFieldLabel(std::size_t index) {
    switch (index) {
        case 0: return "Username";
        case 1: return "Type";
        case 2: return "Password";
        case 3: return "Password hint";
        case 4: return "Failed count";
        default: return "";
    }
}

std::string formatFormLine(const UserForm& form, std::size_t index, std::size_t width) {
    const bool selected = form.field == index;
    const std::string marker = selected ? "> " : "  ";
    const std::string label = fitText(formFieldLabel(index), 16);
    std::string value = formFieldValue(form, index);
    if (index == 1) {
        value += "  (Left/Right/Space)";
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
              << colorText(" - form", kHintStyle)
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
                  << colorText("Fill the highlighted field directly.", kHelpTextStyle);
    }
    std::cout << std::flush;
}

void renderMain(const DataManager& dataManager, const UserManagerState& state) {
    const tundra_tui::Size size = terminalSize();
    const std::size_t width = std::max<int>(size.width, 90);
    const std::size_t height = std::max<int>(size.height, 18);
    const std::size_t rows = height > 8 ? height - 8 : 10;
    const std::size_t usableWidth = width > 3 ? width - 3 : width;
    const std::size_t usersWidth = std::max<std::size_t>(30, usableWidth * 40 / 100);
    const std::size_t detailsWidth = usableWidth - usersWidth;
    const auto details = buildDetailLines(selectedUser(dataManager, state), state.showPassword);

    std::cout << "\x1b[0m\x1b[2J\x1b[H\x1b[?25l";
    std::cout << colorText("TundraUX User Management", kTitleStyle)
              << colorText(" - ", kHintStyle)
              << colorText(std::to_string(userCount(dataManager)) + " users", kPathStyle)
              << "\n";
    std::cout << colorText("user_data.dat", kPathStyle) << "\n";
    std::cout << colorText(splitBorder(usersWidth, detailsWidth), kBorderStyle) << "\n";
    std::cout << colorText("|", kBorderStyle)
              << headerCell("Users", usersWidth)
              << colorText("|", kBorderStyle)
              << headerCell("Details", detailsWidth)
              << colorText("|", kBorderStyle)
              << "\n";
    std::cout << colorText(splitBorder(usersWidth, detailsWidth), kBorderStyle) << "\n";

    const auto& users = dataManager.GetAllUsers();
    for (std::size_t rowIndex = 0; rowIndex < rows; ++rowIndex) {
        const std::size_t userIndex = state.scroll + rowIndex;
        const USER* user = userIndex < users.size() ? &users[userIndex] : nullptr;
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
                  << colorText("p", kKeyStyle)
                  << colorText(state.showPassword ? " hide password | " : " show password | ", kHintStyle)
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

void moveDown(UserManagerState& state, const DataManager& dataManager) {
    if (state.cursor + 1 < userCount(dataManager)) {
        ++state.cursor;
    }
}

void beginAdd(UserManagerState& state) {
    state.form = UserForm{};
    state.formOpen = true;
    state.confirmDelete = false;
}

void beginEdit(UserManagerState& state, const USER& user) {
    state.form = UserForm{};
    state.form.editing = true;
    state.form.originalName = user.name;
    state.form.name = user.name;
    state.form.type = user.type;
    state.form.password = user.password;
    state.form.passwordHint = user.password_hint;
    state.form.count = std::to_string(user.count);
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

std::string validateForm(const UserForm& form, const DataManager& dataManager, USER& user) {
    user.name = trimCopy(form.name);
    user.type = toLowerCopy(trimCopy(form.type));
    user.password = form.password;
    user.password_hint = trimCopy(form.passwordHint);

    if (user.name.empty()) {
        return "Username cannot be empty.";
    }
    if (user.name == "null") {
        return "\"null\" is reserved for setup.";
    }
    if (hasWhitespace(user.name)) {
        return "Username cannot contain spaces.";
    }
    if (nameExists(dataManager, user.name, form.editing ? form.originalName : "")) {
        return "Username already exists.";
    }
    if (user.type == "debug") {
        return "Debug users cannot be created here.";
    }
    if (user.type != "admin" && user.type != "user") {
        return "Type must be admin or user.";
    }
    if (form.editing && form.originalName != user.name &&
        nameExists(dataManager, user.name, form.originalName)) {
        return "Renamed user conflicts with an existing account.";
    }
    if (user.password.empty()) {
        return "Password cannot be empty.";
    }
    if (!isValidPassword(getPasswordStatus(user.password))) {
        return "Password must be 6+ chars with uppercase, lowercase, and number.";
    }
    if (!user.password_hint.empty() && user.password_hint == user.password) {
        return "Password hint cannot equal the password.";
    }

    if (!parseCount(form.count, user.count)) {
        return "Failed count must be a number.";
    }
    if (user.count < 0 || user.count > 8) {
        return "Failed count must be between 0 and 8.";
    }

    if (form.editing) {
        const auto* oldUser = static_cast<const USER*>(nullptr);
        for (const auto& existing : dataManager.GetAllUsers()) {
            if (existing.name == form.originalName) {
                oldUser = &existing;
                break;
            }
        }
        if (oldUser != nullptr &&
            toLowerCopy(oldUser->type) == "admin" &&
            oldUser->count <= 7 &&
            (user.type != "admin" || user.count > 7) &&
            activeAdminCount(dataManager, oldUser->name) == 0) {
            return "At least one active admin user is required.";
        }
    }

    return "";
}

void saveForm(UserManagerState& state, DataManager& dataManager, USER& currentUser) {
    USER user;
    const std::string error = validateForm(state.form, dataManager, user);
    if (!error.empty()) {
        state.form.error = error;
        return;
    }

    bool ok = false;
    if (state.form.editing) {
        ok = dataManager.UpdateUser(state.form.originalName, user);
        state.message = ok ? "User updated: " + user.name : "Failed to update user.";
    } else {
        ok = dataManager.AddUser(user);
        state.message = ok ? "User created: " + user.name : "Failed to create user.";
    }

    if (ok) {
        state.formOpen = false;
        selectUserByName(state, dataManager, user.name);
        tundraux::audit::logEvent("manage-users", std::string(state.form.editing ? "edit " : "create ") + user.name);
        if (!syncCurrentUserAfterMutation(currentUser, state.message)) {
            state.forceExit = true;
        }
    } else {
        state.form.error = state.message;
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

void handleFormKey(UserManagerState& state, DataManager& dataManager, USER& currentUser, const KeyPress& key) {
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
            saveForm(state, dataManager, currentUser);
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

void deleteSelected(UserManagerState& state, DataManager& dataManager, USER& currentUser) {
    const USER* user = selectedUser(dataManager, state);
    if (user == nullptr) {
        state.message = "No user selected.";
        state.confirmDelete = false;
        return;
    }

    if (isOnlyActiveAdmin(dataManager, *user)) {
        state.message = "At least one active admin user is required.";
        state.confirmDelete = false;
        return;
    }

    const std::string name = user->name;
    if (dataManager.RemoveUser(name)) {
        state.message = "User deleted: " + name;
        clampCursor(state, dataManager);
        tundraux::audit::logEvent("manage-users", "delete " + name);
        if (!syncCurrentUserAfterMutation(currentUser, state.message)) {
            state.forceExit = true;
        }
    } else {
        state.message = "Failed to delete user.";
    }
    state.confirmDelete = false;
}

void disableSelected(UserManagerState& state, DataManager& dataManager, USER& currentUser) {
    const USER* user = selectedUser(dataManager, state);
    if (user == nullptr) {
        state.message = "No user selected.";
        return;
    }
    if (isOnlyActiveAdmin(dataManager, *user)) {
        state.message = "At least one active admin user is required.";
        return;
    }

    USER updated = *user;
    updated.count = 8;
    const std::string name = updated.name;
    if (dataManager.UpdateUser(name, updated)) {
        state.message = "User disabled: " + name;
        selectUserByName(state, dataManager, name);
        tundraux::audit::logEvent("manage-users", "disable " + name);
        if (!syncCurrentUserAfterMutation(currentUser, state.message)) {
            state.forceExit = true;
        }
    } else {
        state.message = "Failed to disable user.";
    }
}

void resetSelected(UserManagerState& state, DataManager& dataManager, USER& currentUser) {
    const USER* user = selectedUser(dataManager, state);
    if (user == nullptr) {
        state.message = "No user selected.";
        return;
    }

    USER updated = *user;
    updated.count = 0;
    const std::string name = updated.name;
    if (dataManager.UpdateUser(name, updated)) {
        state.message = "Login count reset: " + name;
        tundraux::audit::logEvent("manage-users", "reset " + name);
        if (!syncCurrentUserAfterMutation(currentUser, state.message)) {
            state.forceExit = true;
        }
    } else {
        state.message = "Failed to reset count.";
    }
    selectUserByName(state, dataManager, name);
}

bool handleMainKey(UserManagerState& state, DataManager& dataManager, USER& currentUser, const KeyPress& key) {
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
        handleFormKey(state, dataManager, currentUser, key);
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
            deleteSelected(state, dataManager, currentUser);
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
            moveDown(state, dataManager);
            break;
        case Key::Home:
            state.cursor = 0;
            break;
        case Key::End:
            state.cursor = userCount(dataManager) == 0 ? 0 : userCount(dataManager) - 1;
            break;
        case Key::Enter:
            if (const USER* user = selectedUser(dataManager, state)) {
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
                    moveDown(state, dataManager);
                    break;
                case 'k':
                    moveUp(state);
                    break;
                case 'g':
                    state.cursor = 0;
                    break;
                case 'G':
                    state.cursor = userCount(dataManager) == 0 ? 0 : userCount(dataManager) - 1;
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
                    if (const USER* user = selectedUser(dataManager, state)) {
                        beginEdit(state, *user);
                    } else {
                        state.message = "No user selected.";
                    }
                    break;
                case 'd':
                case 'D':
                    if (const USER* user = selectedUser(dataManager, state)) {
                        state.pendingDeleteName = user->name;
                        state.confirmDelete = true;
                        state.message = "Confirm delete.";
                    } else {
                        state.message = "No user selected.";
                    }
                    break;
                case 'r':
                case 'R':
                    resetSelected(state, dataManager, currentUser);
                    break;
                case 'x':
                case 'X':
                    disableSelected(state, dataManager, currentUser);
                    break;
                case 'p':
                case 'P':
                    state.showPassword = !state.showPassword;
                    state.message = state.showPassword ? "Password visible." : "Password hidden.";
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

} // namespace

void manage_users(USER& currentUser) {
    tundra_tui::set_title("User Management");

    std::ifstream check("user_data.dat");
    if (!check.good()) {
        tundra_tui::colorcout("red", "Error: user_data.dat not found.\n");
        tundraux::audit::setCurrentUser(currentUser);
        return;
    }
    check.close();

    DataManager dataManager("user_data.dat");
    ConsoleScreenGuard screenGuard;
    UserManagerState state;

    bool running = true;
    while (running) {
        const tundra_tui::Size size = terminalSize();
        const std::size_t rows = std::max<int>(size.height, 18) > 8
            ? static_cast<std::size_t>(std::max<int>(size.height, 18) - 8)
            : 10;
        keepCursorVisible(state, dataManager, rows);

        if (state.showHelp) {
            renderHelp();
        } else if (state.formOpen) {
            renderForm(state.form);
        } else {
            renderMain(dataManager, state);
        }

        running = handleMainKey(state, dataManager, currentUser, readKey());
    }

    tundraux::audit::setCurrentUser(currentUser);
}
