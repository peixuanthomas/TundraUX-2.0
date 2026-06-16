# TundraUX 2.0

TundraUX 2.0 is a C++ console application that combines a small interactive shell, user account management, an explorer, and an inline editor for plain and encrypted `.TUX` files.

The project is built with CMake and C++17. The full frontend uses Windows console APIs for screen control, colored output, hidden password input, command history, and the inline editor. The local backend is built separately and is intended to stay cross-platform.

## Features

- Interactive shell with command history and fuzzy command suggestions
- User login, logout, password modification, and account lockout tracking
- Admin user management interface for listing, adding, updating, and deleting users
- Explorer for browsing managed files under the `Files` directory
- Explorer file operations: create folders, open, edit, delete, rename, copy, move, paste, and search
- `.TUX` files open from Explorer into the editor with creator-based access checks
- Inline text editor for the Windows console frontend
- CMD passthrough from the main shell with the `/` prefix

## Requirements

- Windows for the full frontend
- CMake 3.15 or newer
- A C++17-capable compiler, such as MSVC or MinGW-w64

On non-Windows systems, configure a backend-only build with `TUNDRAUX_BUILD_FRONTEND=OFF`; this is also the default outside Windows.

## Build

```powershell
cmake -B build
cmake --build build
```

The main frontend executable is named `TundraUX2`.

For a backend-only build:

```powershell
cmake -B build-backend -DTUNDRAUX_BUILD_FRONTEND=OFF
cmake --build build-backend
```

### Backend Split Status

The project also builds `tundraux_backend_stdio`, the local backend process boundary for the frontend/backend split. It exposes line-delimited JSON-RPC over stdin/stdout and supports sessions, login/logout, current-profile lookup, user listing and account mutations, strict-mode state, plain file operations, and TUX file operations.

`TundraUX2` now starts the local stdio backend by default. In default backend mode, `login`, `logout`, `whoami`, `listuser`, `modify`, `strict`, `edit <filename>`, system command authorization for `/<cmd>`, and Explorer first-batch file operations are served through `tundraux_backend_stdio`.

Phase 3 first-batch file migration moves Explorer file operations through the local backend. Explorer refresh, folder creation, delete, copy/move paste, search, and file open/edit use backend APIs.

While the split is incomplete, backend mode intentionally disables local direct paths that would bypass backend authorization: `manageuser` and `export log`. User management TUI migration, audit API migration, HTTP transport, remote access, and shared daemon mode remain future work.

## Validation

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

`ctest --test-dir build --output-on-failure` runs the registered project tests, including backend core, JSON-RPC, stdio process, filesystem store, and frontend backend-client tests.

### Startup Mode

By default, the CMake option `TUNDRAUX_DEBUG_STARTUP` is enabled. The program starts in debug mode without login for local development, including access to debug/admin commands and `/` CMD passthrough.

To build with guest startup and require login before user, admin, or debug capabilities are available:

```powershell
cmake -B build -DTUNDRAUX_DEBUG_STARTUP=OFF
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
| `manageuser` | Open the user management interface; admin/debug only; disabled in backend mode until migrated |
| `edit [filename]` | Open the text editor for a plain file under `Files` |
| `explorer` | Open Explorer; use it to open `.TUX` files in the editor |
| `time` | Show local time and Unix timestamp |
| `license` | Display the license text |
| `info` | Show build information |
| `cls` | Clear the screen |
| `exit` | Exit the program |
| `/<cmd>` | Run a Windows CMD command; admin/debug only |

Debug-only commands are hidden from normal help output and include forced login, display color testing, and diagnostic utilities.

In the default backend mode, `login`, `logout`, `whoami`, `listuser`, `modify`, `strict`, `edit <filename>`, and Explorer first-batch file operations are served through `tundraux_backend_stdio`. `listuser` is available only to admin/debug users in backend mode.

## User Roles

| Role | Access |
| --- | --- |
| `guest` | Can log in and use public shell commands |
| `user` | Can use the editor and Explorer |
| `admin` | Can manage users and use privileged shell commands |
| `debug` | Has unrestricted development access |

Login failures are counted per user. After more than 7 failed attempts, the account is disabled until an admin or debug user resets the count through user management. In backend mode, the user-management TUI is currently disabled until its backend migration is complete.

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

## Editor

The editor can be opened from the main shell for plain files or from Explorer for plain and `.TUX` files. In backend mode, Explorer reads and writes file content through backend RPC before launching the local Windows editor.

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
| `APP/editor/` | Windows editor frontend and `.TUX` editor helpers |
| `APP/backend_client/` | Frontend JSON-RPC client and local stdio backend process runtime |
| `BACKEND/core/` | Backend service interfaces, JSON-RPC dispatcher, session/user/file services |
| `BACKEND/adapters/` | Backend adapters for legacy user data and filesystem storage |
| `BACKEND/stdio/` | Stdio JSON-RPC backend executable |
| `USER/account/` | User management interface |
| `USER/udata/` | User data persistence |
| `SYSTEM/console/` | Colored output and console screen guard utilities |
| `SYSTEM/crypto/` | XOR encryption helper |
| `SYSTEM/debug/` | Debug commands and diagnostics |

## License

MIT License. See `license` for details.
