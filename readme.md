# TundraUX 2.0

TundraUX 2.0 is a Windows-only C++ console application that combines a small interactive shell, user account management, and an encrypted `.TUX` file manager.

The project is built with CMake and C++17. It uses Windows console APIs for screen control, colored output, hidden password input, command history, and the inline editor.

## Features

- Interactive shell with command history and fuzzy command suggestions
- User login, logout, password modification, and account lockout tracking
- Admin user management interface for listing, adding, updating, and deleting users
- TUX File Manager for encrypted `.TUX` files under the `Files` directory
- File operations: create, view, edit, delete, rename, copy, move, search, and directory management
- Metadata support for creator, last editor, creation time, and modification time
- Import and export between `.TUX` files and metadata-bearing `.txt` files
- Inline text editor with Windows and portable backends
- CMD passthrough from the main shell with the `/` prefix

## Requirements

- Windows
- CMake 3.15 or newer
- A C++17-capable compiler, such as MSVC or MinGW-w64

This project currently stops configuration on non-Windows systems because it depends on Windows-specific console APIs.

## Build

```powershell
cmake -B build
cmake --build build
```

The generated executable is named `TundraUX2`.

### Startup Mode

By default, the CMake option `TUNDRAUX_DEBUG_STARTUP` is disabled. The program starts in guest mode and requires login before user, admin, or debug capabilities are available.

To build with debug startup for local development:

```powershell
cmake -B build -DTUNDRAUX_DEBUG_STARTUP=ON
cmake --build build
```

## First Run

On first launch, TundraUX checks for `user_data.dat`.

- If `user_data.dat` does not exist and the `license` file is present, the license is displayed and accepted by pressing Enter.
- After setup, the application enters the main shell.
- User data is stored in `user_data.dat`.
- Managed files are stored under the `Files` directory.

## Main Shell

The main shell prompt changes according to the current session:

- `GUEST>>` for unauthenticated guest mode
- `<username>>` for a logged-in user
- `DEBUG MODE ACTIVE>>` for debug mode

### Main Commands

| Command | Description |
| --- | --- |
| `help`, `?` | Show available commands for the current user role |
| `login <username>` | Log in as a user |
| `logout` | Log out the current user |
| `modify` | Change the current user's password or password hint |
| `listuser` | List registered users |
| `manageuser` | Open the user management interface; admin/debug only |
| `TUXfile` | Open the TUX File Manager; user/admin/debug only |
| `edit [filename]` | Open the text editor for a plain file under `Files` |
| `importdata` | Import legacy user data; admin/debug only |
| `time` | Show local time and Unix timestamp |
| `license` | Display the license text |
| `displaytest` | Run a console display test |
| `info` | Show build information |
| `cls` | Clear the screen |
| `exit` | Exit the program |
| `/<cmd>` | Run a Windows CMD command; admin/debug only |

Debug-only commands are hidden from normal help output and include editor backend inspection, forced login, and diagnostic utilities.

## User Roles

| Role | Access |
| --- | --- |
| `guest` | Can log in and use public shell commands |
| `user` | Can use the editor and TUX File Manager |
| `admin` | Can manage users and use privileged TUX import/export/metadata commands |
| `debug` | Has unrestricted development access |

Login failures are counted per user. After more than 7 failed attempts, the account is disabled until an admin or debug user resets the count through user management.

Password changes made through `modify` require:

- At least 6 characters
- At least one uppercase letter
- At least one lowercase letter
- At least one digit
- A password hint that is not identical to the password

## User Management

Open it from the main shell:

```text
manageuser
```

User management opens as a keyboard-driven TUI.

| Key | Action |
| --- | --- |
| `Up`/`Down`, `j`/`k` | Select a user |
| `Home`/`End` | Jump to first or last user |
| `Enter`, `e` | Edit the selected user |
| `a` | Add a user with a form |
| `d` | Delete the selected user after confirmation |
| `r` | Reset failed login attempts |
| `p` | Show or hide the password in details |
| `h` | Show TUI help |
| `q`, `Esc` | Return to the main shell |

In the add/edit form, type directly into the highlighted field. Use `Up`/`Down` or `Tab` to move fields, `Left`/`Right`/`Space` to toggle the account type, `Enter` to save, and `Esc` to cancel.

## TUX File Manager

Open it from the main shell:

```text
TUXfile
```

Files are stored under `Files`. File names and path components may contain letters, digits, hyphens, and underscores. Use `/` for subdirectories, for example:

```text
touch docs/notes
edit docs/notes
```

### File Commands

| Command | Description |
| --- | --- |
| `help`, `h`, `?` | Show TUX File Manager help |
| `ls`, `list`, `ll` | List files and directories |
| `create`, `touch`, `new`, `c <file>` | Create an empty `.TUX` file |
| `edit`, `open`, `e <file>` | Edit a `.TUX` file |
| `view`, `cat`, `read`, `v <file>` | View file contents |
| `delete`, `remove`, `rm`, `del`, `d <file>` | Delete a file |
| `rename`, `rn <old> <new>` | Rename a file |
| `cp`, `copy <src> <dst>` | Copy a file |
| `cp <file1> [file2..] <dir>` | Copy multiple files into an existing directory |
| `mv`, `move <src> <dst>` | Move or rename a file |
| `mv <file1> [file2..] <dir>` | Move multiple files into an existing directory |
| `find`, `search <pattern>` | Search files by name |
| `mkdir`, `md <dir>` | Create a directory |
| `rmdir`, `rd <dir>` | Remove a directory |
| `quit`, `q`, `exit` | Return to the main shell |

### Privileged TUX Commands

These commands require `admin` or `debug`.

| Command | Description |
| --- | --- |
| `metadata`, `meta`, `m`, `info <file>` | Show file metadata |
| `export`, `ex <file>` | Export a `.TUX` file to `.txt` |
| `import`, `im <file>` | Import a `.txt` file as `.TUX` |

## Editor

The editor can be opened from the main shell for plain files or from the TUX File Manager for `.TUX` files.

| Key | Action |
| --- | --- |
| Arrow keys | Move the cursor |
| Enter | Insert a line break |
| Backspace | Delete a character or merge lines |
| Character keys | Insert text |
| Tab | Enter editor command mode |

Editor command mode:

| Command | Action |
| --- | --- |
| `/s` | Save and exit |
| `/q` | Discard changes and exit |

## `.TUX` File Format

Current format version: `1`.

```text
[Version: unsigned int]
[Creator length: size_t][Encrypted creator]
[Last editor length: size_t][Encrypted last editor]
[Create time: time_t]
[Modify time: time_t]
[Content length: size_t][Encrypted content]
```

Implementation limits:

- Maximum metadata string length: 1024 bytes
- Maximum content length: 16 MiB
- Command history length: 100 entries

## Security Notice

The current encryption is a simple XOR transform. It is useful for demonstrating file format handling, but it is not cryptographically secure. Do not use this project to protect sensitive data without replacing the implementation in `SYSTEM/crypto/crypto.cpp` with a real authenticated encryption scheme.

Changing the encryption implementation will likely break compatibility with existing `.TUX` files unless a migration path is added.

## Project Layout

| Path | Purpose |
| --- | --- |
| `CMakeLists.txt` | Build configuration |
| `CORE/main/` | Startup, license check, shell entry |
| `CORE/startup/` | Login and welcome flow |
| `APP/shell/` | Main shell loop, command registry, and command handlers |
| `APP/explorer/` | Explorer app |
| `APP/file_manager/` | TUX File Manager and `.TUX` I/O |
| `APP/editor/` | Editor frontend and backend selection |
| `USER/account/` | User management interface |
| `USER/udata/` | User data persistence |
| `SYSTEM/console/` | Colored output and console screen guard utilities |
| `SYSTEM/crypto/` | XOR encryption helper |
| `SYSTEM/debug/` | Debug commands and diagnostics |

## License

MIT License. See `license` for details.
