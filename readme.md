# TundraUX 2.0

TundraUX 2.0 is a C++17 console application that combines a small interactive shell, user
account management, a file explorer, an inline text editor for plain and encrypted `.TUX`
files, and an encrypted audit log.

The project is split into a **Windows console frontend** and a **cross-platform backend**.
The frontend handles screen control, colored output, hidden password input, command history,
and all full-screen TUI screens. The backend owns authentication, the user store, file and
`.TUX` operations, audit logging, and strict-mode state, and is intended to stay portable.

The two halves communicate over **line-delimited JSON-RPC**. On Windows the frontend
launches a local backend process (`tundraux_backend_stdio`) and talks to it over
stdin/stdout. There is no longer a "direct" in-process path: every privileged operation is
served through the backend.

## Features

- Interactive shell with command history, fuzzy command suggestions, and a CMD passthrough
- User login, logout, account-settings editing, and account-lockout tracking
- Admin user-management TUI for listing, adding, editing, deleting, and unlocking users
- Explorer for browsing managed files and folders under the `Files` directory
- Explorer file operations: create folders, open, edit, delete, rename, copy, move, paste, and search
- Inline "Tundra Editor" for plain text files and creator-gated `.TUX` files
- Encrypted `.TUX` file format with creator / last-editor metadata
- Encrypted `.tlog` audit logging with a strict mode and a plaintext export command
- Reusable terminal-UI library, **TundraTUI**, that backs all full-screen screens

## Architecture

```
+-------------------------------+         line-delimited JSON-RPC        +-----------------------------+
|        TundraUX2 (frontend)   |  <----------- stdin/stdout ----------> |   tundraux_backend_stdio    |
|  shell, explorer, editor,     |                                        |  sessions, users, files,    |
|  account settings, manageuser |                                        |  TUX, audit, strict mode    |
|  (Windows console + TundraTUI)|                                        |  (cross-platform core)      |
+-------------------------------+                                        +-----------------------------+
```

| Target | Kind | Role |
| --- | --- | --- |
| `TundraTUI::TundraTUI` | library | Console screen handling, ANSI styles, key input, layout/render helpers |
| `tundraux_protocol` | library | Shared JSON / protocol code used on both sides of the boundary |
| `tundraux_backend_core` | library | Session, user, file, TUX, and audit services + JSON-RPC dispatcher |
| `tundraux_backend_adapters` | library | Adapters for the legacy user-data store and the filesystem stores |
| `tundraux_backend_stdio` | executable | The backend process: JSON-RPC over stdin/stdout |
| `TundraUX2` | executable | The Windows console frontend |

When the frontend is built, `tundraux_backend_stdio` is copied next to it as a post-build
step, so `TundraUX2` can find and start it automatically.

## Requirements

- Windows for the full frontend (uses Windows console APIs)
- CMake 3.15 or newer
- A C++17-capable compiler, such as MSVC or MinGW-w64

On non-Windows systems the frontend is unavailable; CMake automatically configures a
backend-and-tests-only build (`TUNDRAUX_BUILD_FRONTEND` defaults to `OFF` off Windows).

## Build

```powershell
cmake -B build
cmake --build build
```

This builds the TundraTUI library, the shared protocol library, the backend libraries and
`tundraux_backend_stdio`, the test executables, and (on Windows) the `TundraUX2` frontend.

For a backend-and-tests-only build (forced even on Windows):

```powershell
cmake -B build-backend -DTUNDRAUX_BUILD_FRONTEND=OFF
cmake --build build-backend
```

### CMake Options

| Option | Default | Purpose |
| --- | --- | --- |
| `TUNDRAUX_BUILD_FRONTEND` | `ON` on Windows, `OFF` elsewhere | Build the Windows console frontend and frontend tests |
| `TUNDRAUX_BUILD_TESTS` | `ON` | Build the test executables and enable CTest |
| `TUNDRATUI_BUILD_EXAMPLES` | `ON` | Build the TundraTUI example programs |

A larger set of tests (protocol, boundary, audit-log, build-info, and several frontend
tests) is enabled automatically for multi-config generators and `Debug` builds, and disabled
for single-config release builds.

### Startup Role

The startup identity is baked in at configure time based on the CMake build configuration:

- **Debug** builds start logged in as a `debug` user (no login required), with access to
  debug commands and the `/` CMD passthrough.
- **Non-Debug** builds (Release, etc.) start as an unauthenticated `guest`, and login is
  required before user, admin, or debug capabilities are available.

This is driven by the `TUNDRAUX_DEFAULT_USER_TYPE` / `TUNDRAUX_DEFAULT_USER_NAME` compile
definitions; select a configuration when building, e.g.:

```powershell
cmake -B build
cmake --build build --config Release
```

## Validation

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

CTest runs the registered tests, including the backend core, file service, TUX service,
audit service, JSON-RPC, and stdio-process tests; the shared-protocol and client/facade
boundary tests; the frontend command-registry, explorer, debug, audit-log, build-info, and
TundraTUI tests.

## Running

The built frontend executable is named `TundraUX2`:

```text
Usage: TundraUX2 [--backend-stdio <path>]
```

By default it launches the `tundraux_backend_stdio` copied alongside it; pass
`--backend-stdio <path>` to point at a specific backend executable.

The backend can also be run directly (useful for testing the protocol):

```text
Usage: tundraux_backend_stdio [--user-data PATH] [--files-root PATH] [--debug-session-token TOKEN]
```

It defaults to `user_data.dat` for the user store and `Files` for the managed-files root.

## First Run

On launch, TundraUX asks the backend whether setup is required (whether an administrator
account exists yet).

- If setup is required, the `license` file is displayed and accepted by pressing Enter,
  then an interactive **first-time setup** screen collects the first administrator account
  (username, password, confirmation, and an optional hint) with live validation.
- After setup, the application enters the main shell.
- User data is stored in `user_data.dat`; managed files live under the `Files` directory;
  encrypted audit logs are written under `Files/Logs`.

## Main Shell

The prompt reflects the current session:

- `GUEST>>` for unauthenticated guest mode
- `<username>>` for a logged-in user
- `DEBUG MODE ACTIVE>>` for debug mode

### Main Commands

| Command | Description |
| --- | --- |
| `help`, `?` | Show available commands for the current role |
| `login <username>` | Log in as a user |
| `logout` | Log out the current user |
| `whoami` | Show the current logged-in user |
| `modify` | Open account settings to change the current user's password or hint |
| `listuser` | List registered users (admin/debug only) |
| `manageuser` | Open the user-management TUI (admin/debug only) |
| `edit [filename]` | Open the editor for a plain file under `Files` (user/admin/debug) |
| `explorer` | Open Explorer; use it to open `.TUX` files in the editor (user/admin/debug) |
| `strict <status\|on\|off>` | View or change strict audit mode (admin/debug) |
| `export log <tlog-file>` | Export an encrypted audit log to plaintext (admin/debug) |
| `time` | Show local time and Unix timestamp |
| `license` | Display the license text |
| `info` | Show build information |
| `cls`, `clear` | Clear the screen |
| `exit`, `quit`, `q` | Exit the program |
| `/<cmd>` | Run a Windows CMD command (admin/debug only) |

Debug-only commands are hidden from normal help and are namespaced with a `dbg:` prefix
(`dbg:help`, `dbg:env`, `dbg:displaytest`, `dbg:forcelogin`, `dbg:createfile`,
`dbg:deletefile`, `dbg:structfile`, `dbg:hello()`). Use `dbg:help` to list them.

Unknown input gets a "did you mean" suggestion; if it looks like a Windows command, the
shell hints to use the `/` prefix instead.

## User Roles

| Role | Access |
| --- | --- |
| `guest` | Can log in and use public shell commands |
| `user` | Can use the editor and Explorer |
| `admin` | Can manage users, audit mode, and the CMD passthrough |
| `debug` | Has unrestricted development access |

Login failures are counted per user. After more than 7 failed attempts the account is
disabled until an admin or debug user resets the count from the user-management TUI.

Password changes (in account settings and first-time setup) require:

- At least 6 characters
- At least one uppercase letter
- At least one lowercase letter
- At least one digit
- A password hint that is not identical to the password

## Account Settings

`modify` opens a keyboard-driven account-settings screen for the current user. Type into the
highlighted field, move between fields with `Up`/`Down` or `Tab`, toggle password visibility,
press `Enter` to save changes through the backend, and `Esc` to cancel.

## User Management

Open it from the main shell:

```text
manageuser
```

User management is a keyboard-driven TUI.

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

In the add/edit form, type directly into the highlighted field. Use `Up`/`Down` or `Tab` to
move fields, `Left`/`Right`/`Space` to toggle the account type, `Enter` to save, and `Esc`
to cancel.

## Editor

The "Tundra Editor" opens from the main shell for plain files (`edit`) or from Explorer for
plain and `.TUX` files. For `.TUX` files the frontend reads and writes content through the
backend before and after the editing session.

| Key | Action |
| --- | --- |
| Arrow keys | Move the cursor |
| Home / End | Jump to line start / end |
| PgUp / PgDown | Page up / down |
| `Ctrl+O` | Save file |
| `Ctrl+X` | Exit editor |
| `Ctrl+R` | Open a file |
| `Ctrl+N` | New file |
| `Ctrl+K` | Cut the current line |
| `Ctrl+U` | Paste cut content |
| `Ctrl+W` | Search text |
| `Ctrl+G` | Show help |

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

## Audit Log

The backend records shell input, command execution, key presses, and management actions to
encrypted `.tlog` files (header `TLOG1`) under `Files/Logs`.

- `strict <status|on|off>` views or changes strict audit mode, which is persisted with the
  user data.
- `export log <tlog-file>` decrypts a `.tlog` file to plaintext for review (admin/debug only).

## Security Notice

The current encryption is a simple XOR transform (`encryptDecrypt` in
`SYSTEM/crypto/crypto.cpp`), used for both `.TUX` content/metadata and `.tlog` records. It is
useful for demonstrating file-format handling, but it is **not** cryptographically secure. Do
not use this project to protect sensitive data without replacing the implementation with a
real authenticated encryption scheme.

Changing the encryption will likely break compatibility with existing `.TUX` and `.tlog`
files unless a migration path is added.

## Project Layout

| Path | Purpose |
| --- | --- |
| `CMakeLists.txt` | Top-level build configuration |
| `cmake/` | Build helpers (e.g. build-info generation) |
| `CORE/main/` | Startup, setup probe, license check, shell entry |
| `CORE/startup/` | First-time admin setup screen |
| `APP/shell/` | Main shell loop, command registry, and command handlers |
| `APP/explorer/` | Explorer app (navigation, rendering, file operations, search) |
| `APP/editor/` | Inline editor and `.TUX` editor helpers |
| `APP/account_settings/` | Account-settings TUI |
| `APP/backend_client/` | Frontend JSON-RPC client, facade, and backend process runtime |
| `BACKEND/core/` | Backend service interfaces, JSON-RPC dispatcher, session/user/file/TUX/audit services |
| `BACKEND/adapters/` | Adapters for the legacy user store and filesystem stores |
| `BACKEND/stdio/` | Stdio JSON-RPC backend executable |
| `SHARED/protocol/` | Shared JSON / protocol code |
| `USER/account/` | User-management TUI |
| `USER/udata/` | User-data persistence and strict-mode flag |
| `SYSTEM/audit/` | Audit-log writer, reader, and exporter |
| `SYSTEM/build_info/` | Build information used by `info` |
| `SYSTEM/console/` | Compatibility shims forwarding to TundraTUI |
| `SYSTEM/crypto/` | XOR encryption helper |
| `SYSTEM/debug/` | Debug commands and diagnostics |
| `TundraTUI/` | Reusable terminal-UI library (own CMake target, README, and examples) |
| `tests/` | Backend, frontend, protocol, and TundraTUI tests |

## License

MIT License. See `license` for details.
</content>
</invoke>
