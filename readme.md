# TundraUX 2.0

A secure, modular file management system with encrypted storage, multi-user access control, and a console-based text editor.

## Overview

TundraUX 2.0 is a Windows console application built around a two-layer design: an interactive shell for user and session management, and a dedicated TUX File Manager for working with encrypted files. It is a complete rewrite of the original TundraUX, featuring a modular C++ architecture, role-based permissions, and a persistent user account system.

All files managed by the TUX File Manager are stored in the proprietary `.TUX` binary format with XOR encryption, embedded metadata, and creator-based access control.

## Key Features

- **Encrypted File Storage** - All files use the `.TUX` binary format with XOR encryption
- **Advanced Text Editor** - Multi-line inline editor with dynamic text wrapping and real-time rendering
- **Multi-User System** - Persistent user accounts with password authentication and lockout protection
- **Access Control** - Role-based permissions (Guest, Regular User, Admin, Debug) with creator-based file editing
- **Metadata Tracking** - Full creation and modification history with timestamps and user attribution
- **Colored Output** - Enhanced console experience with color-coded messages
- **Command History** - Navigate previous commands with arrow keys
- **Import/Export** - Convert between encrypted `.TUX` and plain text `.txt` formats
- **Shell Passthrough** - Run CMD commands directly from the shell using the `/` prefix
- **Fuzzy Command Suggestions** - Typo-tolerant command matching using Levenshtein distance

## Getting Started

### Requirements

- **OS**: Windows (uses Windows Console API)
- **Compiler**: C++17 or later
- **Build System**: CMake

### Building

```bash
cmake -B build
cmake --build build
```

### First Run

On first launch, the program displays the license agreement. After accepting, a `user_data.dat` file is created to store user accounts. Subsequent launches skip the license screen and go directly to the shell.

## Shell Command Reference

These commands are available at the main `>>` prompt.

| Command | Usage | Description |
|---------|-------|-------------|
| `help` | `help` | Display available commands |
| `info` | `info` | Show program build information |
| `login` | `login <username>` | Log in as a registered user |
| `logout` | `logout` | Log out the current user |
| `modify` | `modify` | Change the current user's password or password hint |
| `listuser` | `listuser` | List all registered user accounts |
| `manageuser` | `manageuser` | Open the user management interface (Admin/Debug) |
| `TUXfile` | `TUXfile` | Open the TUX File Manager |
| `importdata` | `importdata` | Import user data from older versions (Admin/Debug) |
| `time` | `time` | Display current system time and Unix timestamp |
| `license` | `license` | Show the terms of use license |
| `displaytest` | `displaytest` | Run a console display test |
| `cls` | `cls` | Clear the screen |
| `exit` | `exit` | Exit the program |
| `/<cmd>` | `/<cmd>` | Pass a command directly to the Windows shell |

## TUX File Manager Command Reference

These commands are available inside the `TUXfile` interface. Requires an active logged-in user (Admin or higher).

### File Operations

| Command | Usage | Description |
|---------|-------|-------------|
| `ls`, `list` | `ls` | List all files and directories |
| `c`, `create` | `c <filename>` | Create a new encrypted file |
| `v`, `view` | `v <filename>` | View file contents |
| `e`, `edit` | `e <filename>` | Open file in the inline editor |
| `d`, `delete` | `d <filename>` | Delete a file (with confirmation) |
| `rn`, `rename` | `rn <old> <new>` | Rename a file |
| `h`, `help` | `h` | Display the help menu |
| `i`, `info` | `i` | Show program information |
| `q`, `quit` | `q` | Exit the file manager |

### Privileged Commands (Admin/Debug Only)

| Command | Usage | Description |
|---------|-------|-------------|
| `ex`, `export` | `ex <filename>` | Export a `.TUX` file to plain text (`.txt`) |
| `im`, `import` | `im <filename>` | Import a plain text `.txt` file as `.TUX` |
| `m`, `metadata` | `m <filename>` | View a file's creation and modification history |

## Editor Controls

The inline editor is opened via the `edit` command in the TUX File Manager.

### Navigation & Editing

| Key | Action |
|-----|--------|
| Arrow Keys | Move cursor (left, right, up, down) |
| Enter | Insert line break at cursor position |
| Backspace | Delete character or merge lines |
| Character Keys | Insert character at cursor |
| Tab | Enter command mode |

### Command Mode

Press **Tab** while editing to access quick commands:

- `/s` - Save file and exit editor
- `/q` - Discard changes and exit editor

## Access Control

### User Roles & Permissions

**Guest**
- Log in to an existing account

**Regular User**
- Create new files
- View files
- Edit own files
- Delete own files

**Admin**
- All regular user permissions
- Manage users
- Export files to plain text
- Import plain text as encrypted files
- View file metadata

**Debug**
- All operations without restrictions

### Account Lockout

After 8 failed login attempts, an account is locked out and cannot be used until re-enabled by an admin. A password hint is displayed after each failed attempt if one is configured.

### Password Requirements

- Minimum 6 characters
- At least one uppercase letter
- At least one lowercase letter
- At least one digit
- Cannot be the same as the password hint

## File Format Specification

### .TUX Binary Format

```
[Version (4 bytes)]
[Creator Length (8 bytes)][Creator (encrypted)]
[Last Editor Length (8 bytes)][Last Editor (encrypted)]
[Create Time (8 bytes, Unix timestamp)]
[Modify Time (8 bytes, Unix timestamp)]
[Content Length (8 bytes)][Content (encrypted)]
```

### Constraints

- **Version**: Currently 1
- **Max String Length**: 1024 bytes per field
- **Max File Size**: 16 MB per file
- **Command History**: 100 entries

### Filename Rules

- Allowed characters: A–Z, a–z, 0–9, hyphens (`-`), underscores (`_`)
- Must not be empty
- Case-sensitive

### Timestamps

- Stored as Unix timestamps (seconds since epoch)
- Displayed in local time as `YYYY-MM-DD HH:MM:SS`
- Automatically updated on file modification

## Architecture

TundraUX 2.0 follows a modular design with clear separation of concerns:

| File | Responsibility |
|------|---------------|
| `main.cpp` | Entry point, license check, first-run setup |
| `command.cpp` | Main shell loop, command dispatch, fuzzy matching |
| `TUXfile.cpp` | TUX File Manager and `.TUX` format I/O |
| `editor_win.cpp` | Inline text editor (Windows Console API) |
| `crypto.cpp` | XOR encryption and decryption |
| `udata.cpp` | User data management and persistence |
| `manageusers.cpp` | User creation and administration interface |
| `color.cpp` | Colored console output utilities |
| `hello.cpp` | Startup messages and display helpers |
| `debug.cpp` | Debug utilities and file structure inspection |

## Security Notice

**Important**: The current encryption uses a simple XOR cipher with a fixed key. This is **not cryptographically secure** and should not be used to protect sensitive data.

To use stronger encryption, replace the implementation in `crypto.cpp`. Note that changing the encryption algorithm will break compatibility with existing `.TUX` files.

## Version History

### 2.0 (Current)
- Complete architecture redesign with modular codebase
- Two-layer design: interactive shell + TUX File Manager
- Persistent multi-user account system with role-based permissions
- Account lockout after excessive failed login attempts
- Advanced inline text editor with dynamic wrapping
- Command history with arrow key navigation
- Fuzzy command suggestion using Levenshtein distance
- Windows shell passthrough via `/` prefix
- Metadata tracking and file import/export
- Colored console output

### 1.0 (Legacy)
- Basic file management system
- Simple text editor
- Single-user support
