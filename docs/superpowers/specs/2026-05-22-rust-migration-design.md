# TundraUX Rust Migration Design

## Context

TundraUX 2.0 is currently a Windows-only C++17 console application built with
CMake. The codebase is organized around startup, shell commands, user account
management, encrypted `.TUX` files, explorer actions, an inline editor, audit
logs, and a small custom TUI layer.

The Rust migration is a full replacement. The final Rust project will not keep
the C++ source, CMake build, or C++ runtime path as part of the supported
product. The old C++ data formats still matter only as legacy import sources.

## Goals

- Rebuild the project in Rust for Windows first.
- Replace the old command structure with a cleaner Rust command model.
- Keep the business logic independent from the terminal UI so a future full TUI
  redesign can reuse the same core services.
- Use a new encrypted, program-specific data format instead of SQLite, JSON, or
  other generic storage formats.
- Provide an explicit legacy import interface for old C++ data.
- Avoid long-term dependencies on old C++ data formats after import.

## Non-Goals

- Do not translate C++ files one by one into Rust modules.
- Do not preserve CMake or the C++ build as a supported runtime path.
- Do not write to legacy `user_data.dat`, `.TUX`, or `.tlog` formats.
- Do not implement the full text editor in the first migration stage.
- Do not implement the full future ratatui interface in the first migration
  stage.
- Do not solve cross-platform support in this design. Windows is the target.

## Recommended Approach

Build a new Rust architecture first, then implement a legacy import path into
the new data model.

The first useful milestone is not a full UI clone. It is a working Rust
executable that can initialize encrypted storage, import old data, authenticate
a user, list users, list files, and list audit events.

Example milestone commands:

```powershell
tundraux legacy import --from "C:\old\TundraUX"
tundraux login <name>
tundraux user list
tundraux file list
tundraux audit list
```

This validates the new storage format, password-derived encryption, legacy
readers, and new command structure before rebuilding the larger user interface.

## Architecture

Use a Rust workspace with small crates that have clear ownership:

```text
tundraux/
  crates/
    tundra-core/       Business model: users, roles, files, audit events
    tundra-storage/    Encrypted data files, versioning, integrity checks
    tundra-legacy/     Read-only import support for old C++ data
    tundra-cli/        New command structure and interactive shell
    tundra-ui/         Terminal UI abstraction, thin in stage one
```

The important boundaries are:

- `tundra-core` does not know about terminal rendering, file paths, or Windows
  console APIs. It owns business rules and data models.
- `tundra-storage` does not know command syntax. It owns encrypted persistence,
  format versions, and validation.
- `tundra-legacy` never writes old formats. It reads old C++ data and converts
  it into `tundra-core` models.
- `tundra-cli` calls services from `core`, `storage`, and `legacy`.
- `tundra-ui` starts thin and can later become a full ratatui-based interface
  without rewriting storage or business logic.

## Dependencies

Use mature Rust crates where they reduce risk:

- `clap` for command parsing.
- `serde` or a binary codec such as `bincode`/`postcard` for internal payload
  encoding before encryption.
- `argon2` for password-based key derivation.
- `chacha20poly1305` or `aes-gcm` for authenticated encryption.
- `rand` for nonce and salt generation.
- `thiserror` or `anyhow` for error handling, depending on crate boundary.
- `ratatui` and `crossterm` later when the full TUI is rebuilt.

The exact crate list can be finalized in the implementation plan, but the design
assumes mature third-party crates are allowed.

## New Data Format

Use multiple program-specific encrypted data files:

```text
data/
  users.tdb     Users, roles, password verification parameters, account state
  files.tdb     Virtual file tree, file metadata, encrypted content index
  audit.tdb     Audit event stream
  meta.tdb      Format version, instance ID, global config, import records
```

Each `.tdb` file is a custom binary container:

```text
magic bytes
format version
container type
kdf parameters
encrypted payload length
nonce
ciphertext
auth tag
```

The container must be versioned from the beginning. Future format migrations
should be explicit and recorded in `meta.tdb`.

## Encryption Model

User passwords are not stored directly. The login password is processed with
`argon2id` to derive a key or unlock a stored data key.

Encrypted containers use authenticated encryption, preferably
`ChaCha20-Poly1305` unless Windows-specific acceleration makes `AES-256-GCM`
clearly preferable during implementation.

The design requirements are:

- Every encrypted container has an independent salt or key material as needed.
- Every encryption operation uses a unique nonce.
- Authentication tags are verified before any payload is trusted.
- `files.tdb` and `audit.tdb` require an authenticated session to read.
- Tampered or corrupted containers fail closed with clear errors.

`users.tdb` needs enough information to verify login and unlock the rest of the
data. The implementation plan should choose the precise key hierarchy, but the
security goal is that copied data files cannot be read without the correct
password.

## Command Structure

Stage one uses standalone commands first. This keeps behavior testable before
rebuilding a full interactive interface.

Initial command set:

```text
tundraux init
tundraux login <username>
tundraux user list
tundraux user add <username> --role user|admin
tundraux user disable <username>
tundraux file list [path]
tundraux file import <host-path> <vault-path>
tundraux file export <vault-path> <host-path>
tundraux file remove <vault-path>
tundraux audit list
tundraux audit export <host-path>
tundraux legacy scan --from <old-project-dir>
tundraux legacy import --from <old-project-dir>
```

An interactive shell can be added later:

```text
tundraux shell
```

The future shell should call the same service APIs as the standalone commands.
It should not depend on parsing command output.

## Legacy Import

Legacy import is explicit. The Rust program does not silently import old data at
startup.

Supported import sources:

- `user_data.dat`
- `Files/**/*.TUX`
- `Logs/**/*.tlog`

Import commands:

```text
tundraux legacy scan --from <old-project-dir>
tundraux legacy import --from <old-project-dir>
```

`legacy scan` checks the old project directory and reports:

- number of readable users
- number of readable legacy `.TUX` files
- number of readable `.tlog` files
- unreadable or malformed entries

`legacy import` requires an initialized new vault and an authenticated admin
session. It converts old records into the new `tundra-core` model and writes
them to the new `.tdb` containers.

Conflict policy:

- Username conflicts are skipped by default and recorded in the import report.
- File path conflicts are imported under `imported/<timestamp>/...`.
- Audit log imports append a new import batch and do not overwrite existing
  audit events.

Import completion records:

- `meta.tdb` stores the import batch metadata.
- `audit.tdb` stores an import summary event.
- A local text import report may be written for human review.

## Deferred Work

The following areas are intentionally left for later stages and should be
expanded before implementation reaches them:

- Full ratatui-based file manager and account management UI.
- Full Rust text editor replacement.
- Detailed key hierarchy, including password change and recovery behavior.
- Data format migration tooling for future `.tdb` versions.
- Backup and restore workflow.
- Secure delete behavior for exported or temporary files.
- Detailed audit event schema.
- Final naming and exact syntax of all CLI commands.
- Replacement documentation for end users.

## Testing Strategy

Stage one tests should focus on storage correctness and migration safety:

- Unit tests for `tundra-core` permission and path rules.
- Round-trip tests for every `.tdb` container type.
- Corruption tests that modify ciphertext, nonce, version, or auth tag and
  confirm the reader rejects the file.
- Password tests for correct login, wrong password, disabled users, and role
  checks.
- Legacy parser tests using small fixture files for old `user_data.dat`, `.TUX`,
  and `.tlog` data.
- Import tests for empty data, normal data, malformed records, duplicate users,
  duplicate paths, and repeated import attempts.
- CLI tests for command exit codes and human-readable error messages.

The old C++ test suite should be treated as behavioral reference material, not
as code to preserve. Where old tests describe still-valid behavior, equivalent
Rust tests should be written against the new service APIs.

## Execution Order

1. Create the Rust workspace and crate boundaries.
2. Define `tundra-core` models for users, roles, vault paths, files, and audit
   events.
3. Implement `tundra-storage` encrypted container read/write with tests.
4. Implement `tundra-cli init`, login verification, and session handling.
5. Implement `user list` and minimal user administration.
6. Implement `file list`, `file import`, `file export`, and `file remove`.
7. Implement `audit list` and `audit export`.
8. Implement `legacy scan`.
9. Implement `legacy import`.
10. Add import reports and audit summaries.
11. Expand into `tundraux shell`.
12. Plan the future ratatui UI and editor replacement as separate specs.

## Acceptance Criteria

The first migration stage is complete when:

- A clean Rust build produces a Windows executable.
- New encrypted `.tdb` files can be initialized and opened only with the correct
  password.
- Users, files, and audit events can be created, listed, and persisted.
- Legacy C++ data can be scanned and imported through explicit commands.
- Legacy import never writes old formats.
- C++ source and CMake are no longer required for the Rust executable.
- Tests cover successful storage, failed authentication, corrupted data, and
  representative legacy import cases.
