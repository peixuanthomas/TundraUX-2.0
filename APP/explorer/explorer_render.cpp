#include "explorer_render.hpp"

#include "explorer_directory.hpp"
#include "explorer_permissions.hpp"
#include "explorer_style.hpp"
#include "explorer_text.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace tundraux::explorer {
namespace {

bool clipboardMatches(const ExplorerState& state, const FileEntry& entry) {
    return state.clipboard.mode != ClipboardMode::None &&
           isSamePath(state.clipboard.path, entry.path);
}

const char* entryNameStyle(const FileEntry& entry) {
    if (entry.isHidden) {
        return kHiddenStyle;
    }
    if (entry.isDirectory) {
        return kDirStyle;
    }

    const std::string extension = extensionOf(entry.path);
    if (extension == ".tux") {
        return kTuxFileStyle;
    }
    if (extension == ".md" || extension == ".txt") {
        return kTextFileStyle;
    }
    if (extension == ".dat") {
        return kDataFileStyle;
    }
    return kFileStyle;
}

const char* previewLineStyle(const std::string& line) {
    const std::string normalized = toLowerCopy(line);
    if (normalized.find("cannot") != std::string::npos ||
        normalized.find("failed") != std::string::npos ||
        normalized.find("corrupted") != std::string::npos) {
        return kWarningStyle;
    }
    if (normalized.find("empty") != std::string::npos ||
        normalized.find("too large") != std::string::npos ||
        normalized.find("binary") != std::string::npos ||
        normalized.find("encrypted") != std::string::npos) {
        return kHintStyle;
    }
    if (line.rfind("[D] ", 0) == 0) {
        return kDirStyle;
    }
    return kHelpTextStyle;
}

std::string headerCell(const std::string& title, std::size_t width) {
    return colorText(trimToWidth(" " + title, width), kHeaderStyle);
}

std::string previewCell(const std::string& text, std::size_t width) {
    return colorText(trimToWidth(text, width), previewLineStyle(text));
}

std::string border(std::size_t parentWidth, std::size_t currentWidth, std::size_t previewWidth) {
    return "+" + std::string(parentWidth, '-') +
           "+" + std::string(currentWidth, '-') +
           "+" + std::string(previewWidth, '-') + "+";
}

std::size_t findCurrentInParent(const ExplorerState& state) {
    for (std::size_t index = 0; index < state.parentEntries.size(); ++index) {
        if (isSamePath(state.parentEntries[index].path, state.currentPath)) {
            return index;
        }
    }
    return state.parentEntries.size();
}

std::size_t parentScrollForCurrent(std::size_t currentIndex, std::size_t totalEntries, std::size_t rows) {
    if (rows == 0 || currentIndex >= totalEntries || currentIndex < rows) {
        return 0;
    }
    return currentIndex - rows + 1;
}

std::string formatParentEntry(const FileEntry& entry, bool current) {
    return std::string(current ? "-> " : "   ") +
           std::string(entry.isDirectory ? "[D] " : "    ") +
           entry.name;
}

std::string fitText(const std::string& value, std::size_t width) {
    if (value.size() <= width) {
        return value;
    }
    if (width == 0) {
        return "";
    }

    std::string fitted = value.substr(0, width);
    fitted.back() = '.';
    return fitted;
}

std::string formatParentCell(const FileEntry* entry, bool current, std::size_t width) {
    if (entry == nullptr) {
        return std::string(width, ' ');
    }

    const std::string marker = current ? "-> " : "   ";
    const std::string badge = entry->isDirectory ? "[D] " : "    ";
    const std::size_t fixedWidth = marker.size() + badge.size();
    if (fixedWidth >= width) {
        return colorCellPart(trimToWidth(formatParentEntry(*entry, current), width), entryNameStyle(*entry), current);
    }

    const std::string name = fitText(entry->name, width - fixedWidth);
    const std::size_t visibleWidth = fixedWidth + name.size();
    const std::string padding = visibleWidth < width ? std::string(width - visibleWidth, ' ') : "";

    return colorCellPart(marker, current ? kSelectedMarkStyle : kHintStyle, current) +
           colorCellPart(badge, entry->isDirectory ? kDirStyle : kHintStyle, current) +
           colorCellPart(name, entryNameStyle(*entry), current) +
           colorCellPart(padding, kFileStyle, current);
}

std::string formatCurrentEntry(const FileEntry& entry, bool selected) {
    std::ostringstream out;
    out << (selected ? "> " : "  ")
        << (entry.isDirectory ? "[D]" : "   ")
        << (entry.isHidden ? "." : " ")
        << ' ' << entry.name << "  "
        << (entry.isDirectory ? "<DIR>" : formatSize(entry.size));
    return out.str();
}

std::string formatCurrentCell(
    const ExplorerState& state,
    const FileEntry* entry,
    bool selected,
    std::size_t width
) {
    if (entry == nullptr) {
        return std::string(width, ' ');
    }

    const std::string marker = selected ? "> " : "  ";
    const std::string badge = entry->isDirectory ? "[D]" : "   ";
    const std::string hiddenMarker = entry->isHidden ? "." : " ";
    const std::string gap = " ";
    const std::string suffix = "  " + std::string(entry->isDirectory ? "<DIR>" : formatSize(entry->size));
    const std::string prefix = marker + badge + hiddenMarker + gap;
    const std::size_t fixedWidth = prefix.size() + suffix.size();

    if (fixedWidth >= width) {
        return colorCellPart(trimToWidth(formatCurrentEntry(*entry, selected), width), entryNameStyle(*entry), selected);
    }

    const std::size_t nameWidth = width - fixedWidth;
    const std::string name = fitText(entry->name, nameWidth);
    const char* nameStyle = entryNameStyle(*entry);
    if (clipboardMatches(state, *entry)) {
        nameStyle = state.clipboard.mode == ClipboardMode::Copy ? kCopyStyle : kCutStyle;
    }

    std::string cell =
        colorCellPart(marker, selected ? kSelectedMarkStyle : kHintStyle, selected) +
        colorCellPart(badge, entry->isDirectory ? kDirStyle : kHintStyle, selected) +
        colorCellPart(hiddenMarker, entry->isHidden ? kHiddenStyle : kHintStyle, selected) +
        colorCellPart(gap, kFileStyle, selected) +
        colorCellPart(name, nameStyle, selected) +
        colorCellPart(suffix, entry->isDirectory ? kDirStyle : kSizeStyle, selected);

    const std::size_t visibleWidth = prefix.size() + name.size() + suffix.size();
    if (visibleWidth < width) {
        cell += colorCellPart(std::string(width - visibleWidth, ' '), kFileStyle, selected);
    }
    return cell;
}

void renderHelpSection(const std::string& title) {
    std::cout << colorText(title, kSectionStyle) << "\n";
}

void renderHelpBinding(const std::string& keys, const std::string& description) {
    std::cout << "  "
              << colorText(trimToWidth(keys, 22), kKeyStyle)
              << colorText(description, kHelpTextStyle)
              << "\n";
}

void renderHelp(const ExplorerState& state, const std::string& username, const std::string& usertype) {
    std::cout << "\x1b[0m\x1b[2J\x1b[H\x1b[?25l";
    std::cout << colorText("TundraUX Explorer Help", kTitleStyle)
              << colorText(" - ", kHintStyle)
              << colorText(usertype, kRoleStyle)
              << colorText(": ", kHintStyle)
              << colorText(username, kUserStyle)
              << "\n";
    std::cout << colorText(pathToDisplayString(state.currentPath), kPathStyle) << "\n\n";
    renderHelpSection("Navigation");
    renderHelpBinding("Up/Down or j/k", "Move cursor");
    renderHelpBinding("Enter or o", "Open file or enter directory");
    renderHelpBinding("Left, Backspace, b", "Go to parent directory");
    renderHelpBinding("g / G", "Jump to top / bottom");
    std::cout << "  "
              << colorText("Pro tip", kTuxFileStyle)
              << colorText(": Arrow keys cover navigation: Up/Down select, Right open, Left parent.", kHelpTextStyle)
              << "\n";
    std::cout << "\n";

    renderHelpSection("File operations");
    renderHelpBinding("c", "Copy selected file or folder, shown in green until paste");
    renderHelpBinding("x", "Cut selected file or folder, shown in gray until paste");
    renderHelpBinding("p", "Paste into current directory");
    std::cout << "\n";
    renderHelpBinding("n", "Create a new folder in the current directory");
    renderHelpBinding("d", "Request delete for selected item");
    renderHelpBinding("D", "Confirm pending delete");
    std::cout << "\n";

    renderHelpSection("View");
    renderHelpBinding(".", "Show or hide hidden files");
    renderHelpBinding("r", "Refresh current directory");
    renderHelpBinding("i", "Show detailed properties for the selected item");
    renderHelpBinding("h", "Toggle this help menu");
    renderHelpBinding("q or Esc", "Quit explorer from main view, close help from here");
    std::cout << "\n";
    std::cout << colorText("Press h, q, Esc, or Enter to return.", kHintStyle) << std::flush;
}

std::string singleBorder(std::size_t width) {
    if (width < 2) {
        return "";
    }
    return "+" + std::string(width - 2, '-') + "+";
}

const char* detailValueStyle(const DetailLine& line) {
    const std::string normalized = toLowerCopy(line.value);
    if (normalized.rfind("no -", 0) == 0 ||
        normalized.find("unavailable") != std::string::npos ||
        normalized.find("error") != std::string::npos) {
        return kWarningStyle;
    }
    if (normalized == "yes" || normalized == "edit") {
        return kCopyStyle;
    }
    if (normalized == "read-only" || normalized == "none" || normalized == "(none)") {
        return kHintStyle;
    }
    return kHelpTextStyle;
}

std::string detailLineText(const DetailLine& line, std::size_t width) {
    if (line.section) {
        return colorText(trimToWidth(" " + line.label, width), kHeaderStyle);
    }

    const std::size_t labelWidth = std::min<std::size_t>(24, std::max<std::size_t>(12, width / 3));
    const std::size_t valueWidth = width > labelWidth + 1 ? width - labelWidth - 1 : 0;
    return colorText(trimToWidth(line.label + ":", labelWidth), kKeyStyle) +
           " " +
           colorText(trimToWidth(line.value, valueWidth), detailValueStyle(line));
}

void renderDetails(const ExplorerState& state, const std::string& username, const std::string& usertype) {
    const COORD size = consoleSize();
    const std::size_t width = std::max<int>(size.X, 90);
    const std::size_t height = std::max<int>(size.Y, 18);
    const std::size_t contentWidth = width > 2 ? width - 2 : width;
    const std::size_t rows = detailVisibleRows(height);
    const std::size_t totalLines = state.detailLines.size();
    const std::size_t maxScroll = totalLines > rows ? totalLines - rows : 0;
    const std::size_t scroll = std::min(state.detailScroll, maxScroll);

    std::cout << "\x1b[0m\x1b[2J\x1b[H\x1b[?25l";
    std::cout << colorText("TundraUX Explorer Properties", kTitleStyle)
              << colorText(" - ", kHintStyle)
              << colorText(usertype, kRoleStyle)
              << colorText(": ", kHintStyle)
              << colorText(username, kUserStyle)
              << "\n";
    std::cout << colorText(state.detailName.empty() ? "(no selection)" : state.detailName, kPathStyle) << "\n";
    std::cout << colorText(singleBorder(width), kBorderStyle) << "\n";

    for (std::size_t rowIndex = 0; rowIndex < rows; ++rowIndex) {
        const std::size_t detailIndex = scroll + rowIndex;
        const std::string content = detailIndex < totalLines
            ? detailLineText(state.detailLines[detailIndex], contentWidth)
            : std::string(contentWidth, ' ');
        std::cout << colorText("|", kBorderStyle)
                  << content
                  << colorText("|", kBorderStyle)
                  << "\n";
    }

    std::cout << colorText(singleBorder(width), kBorderStyle) << "\n";
    const std::string lineStatus = totalLines == 0
        ? "No detail lines"
        : "Lines " + std::to_string(scroll + 1) + "-" +
          std::to_string(std::min(totalLines, scroll + rows)) +
          " of " + std::to_string(totalLines);
    std::cout << colorText("Up/Down", kKeyStyle)
              << colorText(" scroll | ", kHintStyle)
              << colorText("i/Esc/Enter/q", kKeyStyle)
              << colorText(" return | ", kHintStyle)
              << colorText(lineStatus, kSectionStyle)
              << std::flush;
}

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

std::size_t detailVisibleRows(std::size_t height) {
    return height > 7 ? height - 7 : 8;
}

void render(const ExplorerState& state, const std::string& username, const std::string& usertype) {
    if (state.showHelp) {
        renderHelp(state, username, usertype);
        return;
    }
    if (state.showDetails) {
        renderDetails(state, username, usertype);
        return;
    }

    const COORD size = consoleSize();
    const std::size_t width = std::max<int>(size.X, 90);
    const std::size_t height = std::max<int>(size.Y, 18);
    const std::size_t rows = height > 8 ? height - 8 : 10;
    const std::size_t usableWidth = width - 4;
    const std::size_t parentWidth = std::max<std::size_t>(18, usableWidth * 24 / 100);
    const std::size_t currentWidth = std::max<std::size_t>(30, usableWidth * 42 / 100);
    const std::size_t previewWidth = usableWidth - parentWidth - currentWidth;
    const auto previewLines = previewSelected(state);
    const std::size_t parentCurrentIndex = findCurrentInParent(state);
    const std::size_t parentScroll = parentScrollForCurrent(
        parentCurrentIndex,
        state.parentEntries.size(),
        rows
    );

    std::cout << "\x1b[0m\x1b[2J\x1b[H\x1b[?25l";
    std::cout << colorText("TundraUX Explorer", kTitleStyle)
              << colorText(" - ", kHintStyle)
              << colorText(usertype, kRoleStyle)
              << colorText(": ", kHintStyle)
              << colorText(username, kUserStyle)
              << "\n";
    std::cout << colorText(pathToDisplayString(state.currentPath), kPathStyle) << "\n";
    std::cout << colorText(border(parentWidth, currentWidth, previewWidth), kBorderStyle) << "\n";
    std::cout << colorText("|", kBorderStyle)
              << headerCell("Parent", parentWidth)
              << colorText("|", kBorderStyle)
              << headerCell("Current", currentWidth)
              << colorText("|", kBorderStyle)
              << headerCell("Preview", previewWidth)
              << colorText("|", kBorderStyle)
              << "\n";
    std::cout << colorText(border(parentWidth, currentWidth, previewWidth), kBorderStyle) << "\n";

    for (std::size_t rowIndex = 0; rowIndex < rows; ++rowIndex) {
        const std::size_t parentIndex = parentScroll + rowIndex;
        const FileEntry* parentEntry = parentIndex < state.parentEntries.size()
            ? &state.parentEntries[parentIndex]
            : nullptr;
        const std::string parentText = formatParentCell(
            parentEntry,
            parentIndex == parentCurrentIndex,
            parentWidth
        );
        const std::size_t entryIndex = state.scroll + rowIndex;
        const FileEntry* currentEntry = entryIndex < state.entries.size()
            ? &state.entries[entryIndex]
            : nullptr;
        const std::string currentText = formatCurrentCell(
            state,
            currentEntry,
            entryIndex == state.cursor,
            currentWidth
        );
        const std::string previewText = rowIndex < previewLines.size() ? previewLines[rowIndex] : "";
        std::cout << colorText("|", kBorderStyle)
                  << parentText
                  << colorText("|", kBorderStyle)
                  << currentText
                  << colorText("|", kBorderStyle)
                  << previewCell(previewText, previewWidth)
                  << colorText("|", kBorderStyle)
                  << "\n";
    }

    std::cout << colorText(border(parentWidth, currentWidth, previewWidth), kBorderStyle) << "\n";
    if (state.creatingFolder) {
        std::cout << colorText("Enter", kKeyStyle)
                  << colorText(" create | ", kHintStyle)
                  << colorText("Backspace", kKeyStyle)
                  << colorText(" edit | ", kHintStyle)
                  << colorText("Esc", kKeyStyle)
                  << colorText(" cancel", kHintStyle)
                  << "\n";
        std::cout << colorText("New folder name: ", kSectionStyle)
                  << colorText(state.newFolderName, kInputStyle)
                  << std::flush;
    } else {
        std::cout << colorText("Enter", kKeyStyle)
                  << colorText(" open | ", kHintStyle)
                  << colorText("Backspace", kKeyStyle)
                  << colorText(" parent | ", kHintStyle)
                  << colorText("n", kKeyStyle)
                  << colorText(" mkdir | ", kHintStyle)
                  << colorText("c", kKeyStyle)
                  << colorText(" copy | ", kHintStyle)
                  << colorText("x", kKeyStyle)
                  << colorText(" cut | ", kHintStyle)
                  << colorText("p", kKeyStyle)
                  << colorText(" paste | ", kHintStyle)
                  << colorText("d", kKeyStyle)
                  << colorText(" delete | ", kHintStyle)
                  << colorText("i", kKeyStyle)
                  << colorText(" info | ", kHintStyle)
                  << colorText("h", kKeyStyle)
                  << colorText(" help | ", kHintStyle)
                  << colorText("q", kKeyStyle)
                  << colorText(" quit", kHintStyle)
                  << "\n";
        const std::string selectedPermissionStatus = selectedTuxPermissionStatus(state);
        const std::string statusMessage = selectedPermissionStatus.empty()
            ? state.message
            : selectedPermissionStatus;
        std::cout << colorText("Status: ", kSectionStyle)
                  << statusMessage
                  << std::flush;
    }
}

}
