# TUI Color Requirements

This document defines how TundraUX console UI code should use colors and styles.
New or changed TUI code must use semantic styles instead of adding ad hoc ANSI
escape strings in business logic.

## Canonical Sources

- Named linear-output colors live in `TundraTUI/include/TundraTUI/color.hpp` and
  `TundraTUI/src/color.cpp`.
  - Use `colorcout(color, text)` and `rollcout(color, text)` for shell output,
    startup messages, debug commands, prompts, and other linear console text.
- Semantic full-screen TUI styles live in
  `TundraTUI/include/TundraTUI/style.hpp`.
  - Use `kTitleStyle`, `kWarningStyle`, `colorText()`, `colorCellPart()`, and
    related semantic constants for tables, forms, help screens, detail panes,
    selected rows, and status lines.
- Legacy headers such as `SYSTEM/console/color.hpp`,
  `SYSTEM/console/console_screen.hpp`, and `APP/explorer/explorer_style.hpp` are
  compatibility shims. New code should include `TundraTUI/...` headers directly.

## Named Output Colors

The following names are provided by `TundraTUI/color.hpp`:

| Color name | ANSI color | Use |
| --- | --- | --- |
| `red` / `RED` / `ERROR` | bright red, 203 | Errors, dangerous actions, access denied, corrupted files, delete confirmations |
| `green` / `GREEN` | bright green, 119 | Success, saved, created, copied, restored, ready |
| `yellow` / `YELLOW` | bright yellow, 220 | User prompts, next-step instructions, recoverable warnings, command suggestions |
| `blue` / `BLUE` | blue, 39 | Informational text |
| `magenta` / `MAGENTA` | bright magenta, 213 | Shell prompts, debug mode prompts, emphasized labels |
| `cyan` / `CYAN` | bright cyan, 51 | Program titles, page titles, major headings |
| `white` / `WHITE` | white, 254 | Default body text |
| `grey` / `GREY` | grey, 245 | Secondary hints, disabled-looking text, less important notes |

## Semantic TUI Styles

Complex TUI pages must use semantic style constants rather than manually
combining `colorcout()` calls.

| Meaning | Style constant | ANSI color | Use |
| --- | --- | --- | --- |
| Page title | `kTitleStyle` | bold cyan, 51 | Top-level TUI titles such as Explorer, User Management, Help, Properties |
| Role or privilege | `kRoleStyle` | bold magenta, 213 | `admin`, `debug`, role badges, privileged labels |
| Current user | `kUserStyle` | bold yellow, 220 | Current user names and selected user names |
| Path or resource | `kPathStyle` | light blue, 117 | Current path, data file name, selected file name |
| Border | `kBorderStyle` | blue, 39 | Table borders, panel borders, separators |
| Header | `kHeaderStyle` | fg 195 + bg 24, bold | Table headers and detail section headers |
| Section title | `kSectionStyle` | bold cyan-green, 87 | Help groups, status labels, input labels |
| Shortcut key | `kKeyStyle` | bold yellow, 220 | `Enter`, `Esc`, `h`, `q`, and other key hints |
| Hint | `kHintStyle` | grey, 245 | Separators, secondary descriptions, empty values, cancel hints |
| Help text | `kHelpTextStyle` | light grey, 252 | Help descriptions and detail body text |
| Directory | `kDirStyle` | bold cyan-green, 87 | Directory names, `[D]`, `<DIR>` |
| Plain file | `kFileStyle` | white, 254 | Unclassified files and cell padding |
| Text file | `kTextFileStyle` | pale yellow, 229 | `.txt`, `.md` files |
| TUX file | `kTuxFileStyle` | bold magenta, 213 | `.tux` files and TUX-specific prompts |
| Data file | `kDataFileStyle` | red, 203 | `.dat` files and user data files |
| Hidden item | `kHiddenStyle` | dark grey, 244 | Hidden files, hidden directories, hidden markers |
| File size | `kSizeStyle` | grey, 250 | File sizes and capacity information |
| Copy state | `kCopyStyle` | bright green, 119 | Copied items, success status, executable state |
| Cut state | `kCutStyle` | grey, 246 | Cut items and pending move state |
| Selected background | `kSelectedBgStyle` | bg 24 | Current row, current cell, active form field |
| Selected marker | `kSelectedMarkStyle` | bold pale yellow, 229 | `>`, `->`, current selection indicators |
| Input content | `kInputStyle` | bold pale yellow, 230 | Form input values and active text entry |
| Warning or error | `kWarningStyle` | bold red, 203 | Access denied, delete confirmation, errors, locked accounts |

## Development Rules

1. Reuse existing semantic styles before adding new constants.
2. Do not write raw `"\x1b["` or `"\033["` ANSI strings in business logic.
   ANSI sequences belong in TundraTUI style, color, screen, or render-engine
   code.
3. Lists, tables, detail pages, forms, and help pages should use `colorText()`
   or `colorCellPart()` so selected backgrounds and reset behavior stay
   consistent.
4. Errors and dangerous actions must be red; success must be green; prompts and
   shortcut keys must be yellow; paths must be light blue.
5. `.tux`, `.dat`, directories, and hidden files must use the semantic file-type
   styles instead of plain white text.
6. New full-screen TUI files should include `TundraTUI/style.hpp`,
   `TundraTUI/text.hpp`, `TundraTUI/input.hpp`, `TundraTUI/render_engine.hpp`,
   and `TundraTUI/screen.hpp` as needed instead of defining local copies.

## Current Migration State

- `CORE/startup/hello.cpp`, `APP/account_settings/account_settings.cpp`,
  `APP/explorer/*`, and `USER/account/manageusers.cpp` use TundraTUI primitives
  for shared style, input, terminal sizing, and text helpers.
- Shell, file-manager, debug, and user-data code may continue using
  `colorcout()` for linear console output.
- The next extraction step is to move reusable widgets out of application files
  only when repeated behavior is clear.
