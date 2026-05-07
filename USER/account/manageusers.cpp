// Attention: Windows only code.
#include "manageusers.hpp"

#include "color.hpp"
#include "console_screen.hpp"
#include "udata.hpp"

#include <algorithm>
#include <cctype>
#include <conio.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>

namespace {

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
    Left,
    Right,
    Home,
    End
};

struct KeyPress {
    Key key = Key::Unknown;
    char character = '\0';
};

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
    std::size_t cursor = 0;
    std::size_t scroll = 0;
    bool showHelp = false;
    bool showPassword = false;
    bool formOpen = false;
    bool confirmDelete = false;
    UserForm form;
    std::string pendingDeleteName;
    std::string message = "Ready";
};

constexpr const char* kResetStyle = "\x1b[0m";
constexpr const char* kTitleStyle = "\x1b[1;38;5;51m";
constexpr const char* kRoleStyle = "\x1b[1;38;5;213m";
constexpr const char* kUserStyle = "\x1b[1;38;5;220m";
constexpr const char* kPathStyle = "\x1b[38;5;117m";
constexpr const char* kBorderStyle = "\x1b[38;5;39m";
constexpr const char* kHeaderStyle = "\x1b[1;38;5;195;48;5;24m";
constexpr const char* kSectionStyle = "\x1b[1;38;5;87m";
constexpr const char* kKeyStyle = "\x1b[1;38;5;220m";
constexpr const char* kHintStyle = "\x1b[38;5;245m";
constexpr const char* kHelpTextStyle = "\x1b[38;5;252m";
constexpr const char* kInputStyle = "\x1b[1;38;5;230m";
constexpr const char* kWarningStyle = "\x1b[1;38;5;203m";
constexpr const char* kCopyStyle = "\x1b[1;38;5;119m";
constexpr const char* kSelectedBgStyle = "\x1b[48;5;24m";
constexpr const char* kSelectedMarkStyle = "\x1b[1;38;5;229m";

std::string colorText(const std::string& text, const char* style) {
    if (text.empty()) {
        return text;
    }
    return std::string(style) + text + kResetStyle;
}

std::string colorCellPart(const std::string& text, const char* style, bool selected) {
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

std::string singleBorder(std::size_t width) {
    if (width < 2) {
        return "";
    }
    return "+" + std::string(width - 2, '-') + "+";
}

std::string border(std::size_t leftWidth, std::size_t rightWidth) {
    return "+" + std::string(leftWidth, '-') + "+" + std::string(rightWidth, '-') + "+";
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

std::size_t adminCount(const DataManager& dataManager) {
    std::size_t count = 0;
    for (const auto& user : dataManager.GetAllUsers()) {
        if (toLowerCopy(user.type) == "admin") {
            ++count;
        }
    }
    return count;
}

bool nameExists(const DataManager& dataManager, const std::string& name, const std::string& exceptName = "") {
    for (const auto& user : dataManager.GetAllUsers()) {
        if (user.name == name && user.name != exceptName) {
            return true;
        }
    }
    return false;
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

    const std::string name = fitText(user->name, width - fixedWidth);
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
    const COORD size = consoleSize();
    const std::size_t width = std::max<int>(size.X, 90);
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
    const COORD size = consoleSize();
    const std::size_t width = std::max<int>(size.X, 90);
    const std::size_t height = std::max<int>(size.Y, 18);
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
    std::cout << colorText(border(usersWidth, detailsWidth), kBorderStyle) << "\n";
    std::cout << colorText("|", kBorderStyle)
              << headerCell("Users", usersWidth)
              << colorText("|", kBorderStyle)
              << headerCell("Details", detailsWidth)
              << colorText("|", kBorderStyle)
              << "\n";
    std::cout << colorText(border(usersWidth, detailsWidth), kBorderStyle) << "\n";

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

    std::cout << colorText(border(usersWidth, detailsWidth), kBorderStyle) << "\n";
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

KeyPress readKey() {
    const int ch = _getch();
    if (ch == 0 || ch == 224) {
        const int ext = _getch();
        switch (ext) {
            case 72: return {Key::Up, '\0'};
            case 80: return {Key::Down, '\0'};
            case 75: return {Key::Left, '\0'};
            case 77: return {Key::Right, '\0'};
            case 71: return {Key::Home, '\0'};
            case 79: return {Key::End, '\0'};
            case 83: return {Key::Delete, '\0'};
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
    if (!user.password_hint.empty() && user.password_hint == user.password) {
        return "Password hint cannot equal the password.";
    }

    if (!parseCount(form.count, user.count)) {
        return "Failed count must be a number.";
    }
    if (user.count < 0 || user.count > 7) {
        return "Failed count must be between 0 and 7.";
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
            user.type != "admin" &&
            adminCount(dataManager) <= 1) {
            return "At least one admin user is required.";
        }
    }

    return "";
}

void saveForm(UserManagerState& state, DataManager& dataManager) {
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

void handleFormKey(UserManagerState& state, DataManager& dataManager, const KeyPress& key) {
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
            saveForm(state, dataManager);
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

void deleteSelected(UserManagerState& state, DataManager& dataManager) {
    const USER* user = selectedUser(dataManager, state);
    if (user == nullptr) {
        state.message = "No user selected.";
        state.confirmDelete = false;
        return;
    }

    const std::string name = user->name;
    if (toLowerCopy(user->type) == "admin" && adminCount(dataManager) <= 1) {
        state.message = "At least one admin user is required.";
        state.confirmDelete = false;
        return;
    }

    if (dataManager.RemoveUser(name)) {
        state.message = "User deleted: " + name;
        clampCursor(state, dataManager);
    } else {
        state.message = "Failed to delete user.";
    }
    state.confirmDelete = false;
}

void resetSelected(UserManagerState& state, DataManager& dataManager) {
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
    } else {
        state.message = "Failed to reset count.";
    }
    selectUserByName(state, dataManager, name);
}

bool handleMainKey(UserManagerState& state, DataManager& dataManager, const KeyPress& key) {
    if (state.showHelp) {
        if (key.key == Key::Escape || key.key == Key::Enter ||
            (key.key == Key::Character &&
             (key.character == 'h' || key.character == 'H' ||
              key.character == 'q' || key.character == 'Q'))) {
            state.showHelp = false;
        }
        return true;
    }

    if (state.formOpen) {
        handleFormKey(state, dataManager, key);
        return true;
    }

    if (state.confirmDelete) {
        if (key.key == Key::Escape ||
            (key.key == Key::Character && (key.character == 'n' || key.character == 'N'))) {
            state.confirmDelete = false;
            state.message = "Delete cancelled.";
            return true;
        }
        if (key.key == Key::Enter ||
            (key.key == Key::Character && (key.character == 'y' || key.character == 'Y'))) {
            deleteSelected(state, dataManager);
            return true;
        }
        return true;
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
                    resetSelected(state, dataManager);
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

    return true;
}

} // namespace

void manage_users() {
    set_title("User Management");

    std::ifstream check("user_data.dat");
    if (!check.good()) {
        colorcout("red", "Error: user_data.dat not found.\n");
        return;
    }
    check.close();

    DataManager dataManager("user_data.dat");
    ConsoleScreenGuard screenGuard;
    UserManagerState state;

    bool running = true;
    while (running) {
        const COORD size = consoleSize();
        const std::size_t rows = std::max<int>(size.Y, 18) > 8
            ? static_cast<std::size_t>(std::max<int>(size.Y, 18) - 8)
            : 10;
        keepCursorVisible(state, dataManager, rows);

        if (state.showHelp) {
            renderHelp();
        } else if (state.formOpen) {
            renderForm(state.form);
        } else {
            renderMain(dataManager, state);
        }

        running = handleMainKey(state, dataManager, readKey());
    }
}
