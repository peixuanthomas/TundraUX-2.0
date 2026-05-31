# Backend and Frontend Process Split Design

Date: 2026-05-31

## Scope

Split TundraUX 2.0 from a single-process console application into a frontend client process and a backend service process.

The backend becomes the exclusive owner of user control, authorization, audit logging, managed file access, `.TUX` handling, and persistent storage writes. The frontend keeps shell interaction, TUI rendering, input history, form state, and application presentation logic.

The first transport is JSON-RPC over stdin/stdout. The backend service core must not depend on stdio so a future HTTP or remote adapter can reuse the same business logic.

## Current Context

The current application is a C++17/CMake Windows console program built as `TundraUX2`.

The main coupling points are:

- `CORE/main/main.cpp` launches `task_main()`.
- `APP/shell/command.cpp` owns the shell loop, mutable `USER currentUser`, command history, prompt rendering, command execution, and `/` system-command passthrough.
- `APP/shell/commandRegistry.cpp` registers commands and binds handlers directly to `currentUser`.
- `APP/shell/commandHandlers.cpp` calls user, audit, editor, explorer, account-settings, and TUX file functions directly.
- `USER/udata` owns `DataManager` and `user_data.dat`.
- `USER/account/manageusers.cpp`, `APP/account_settings`, `APP/explorer`, `APP/editor`, and `APP/file_manager` mix UI behavior with direct data or file operations.
- `SYSTEM/audit` and `SYSTEM/crypto` are linked into the executable and can be called from multiple modules.

This design keeps the visible user workflow close to the existing shell/TUI experience while moving authority and persistence behind a process boundary.

## Goals

- Build a backend process that can be launched independently for development and automatically by the frontend for normal use.
- Keep the first backend transport local and simple with JSON-RPC over stdio.
- Design the backend core for multiple sessions and future remote clients.
- Keep the backend core as cross-platform as practical.
- Keep the frontend/TUI Windows-only in the first split.
- Preserve existing file formats and data locations in the first version.
- Prevent frontend code from directly mutating `user_data.dat`, `.TUX`, `.tlog`, or managed `Files/` content.

## Non-Goals

- Do not implement HTTP in the first split.
- Do not migrate storage to SQLite or another database in the first split.
- Do not redesign the user interface.
- Do not change `.TUX`, `.tlog`, or `user_data.dat` formats unless a small compatibility change is required for safe writes.
- Do not make the frontend cross-platform in the first split.

## Architecture

Introduce three primary build targets:

- `tundraux_backend_core`: a library containing transport-independent backend services.
- `tundraux_backend_stdio`: an executable that exposes backend core methods through JSON-RPC over stdin/stdout.
- `TundraUX2`: the frontend executable. It can connect to an existing backend in development mode or auto-launch `tundraux_backend_stdio` in normal mode.

The frontend process owns:

- shell prompt rendering
- command input and history
- TUI screen rendering
- local form state, cursor state, and keyboard navigation
- thin APP clients for account settings, user management, Explorer, editor, and TUX File Manager
- backend process launch and connection handling
- JSON-RPC client request/response mapping

The backend process owns:

- sessions and login state
- role and permission checks
- user data operations
- strict-mode state
- managed file operations
- `.TUX` import, export, metadata, encryption, and content persistence
- audit log creation and writing
- storage locking and transaction-style file writes

`SYSTEM/console` and `TundraTUI` remain frontend-side dependencies. `SYSTEM/crypto` and `SYSTEM/audit` should either move into backend-owned code paths or become backend-only dependencies. The frontend may report client events to the backend, but it must not write audit files directly.

## Backend Services

The backend core should be organized around explicit services:

- `SessionService`: creates guest sessions, logs users in and out, returns current user context, and validates session IDs.
- `UserService`: lists, creates, updates, deletes, disables, and resets users; reads and writes strict mode.
- `AuthorizationService`: centralizes role checks and command/app permissions.
- `FileService`: lists directories, reads content, writes content, deletes, renames, copies, moves, creates directories, removes directories, searches, and handles TUX import/export/metadata.
- `AuditService`: records business events, authorization failures, storage failures, and approved frontend client events.
- `StorageLockManager`: provides read/write locks for user data, audit logs, and canonical managed paths.
- `SafeFileWriter`: writes temporary files, flushes/closes them, and atomically replaces the target where the platform supports it.

Services must receive an explicit session context or `sessionId`. They must not rely on a process-global `currentUser`.

## API Design

The first protocol is line-delimited JSON-RPC-style messages over stdio.

Each request contains:

```json
{"id":"1","method":"session.login","params":{"sessionId":"...","username":"alice","password":"..."}}
```

Each successful response contains:

```json
{"id":"1","result":{"sessionId":"...","user":{"name":"alice","type":"admin"}}}
```

Each failure contains:

```json
{"id":"1","error":{"code":"PermissionDenied","message":"Access Denied."}}
```

Core method groups:

- `session.startGuestSession`
- `session.login`
- `session.logout`
- `session.whoami`
- `session.refreshSession`
- `user.listUsers`
- `user.createUser`
- `user.updateUser`
- `user.deleteUser`
- `user.resetFailedCount`
- `user.setStrictMode`
- `user.getStrictMode`
- `file.listDirectory`
- `file.readFile`
- `file.writeFile`
- `file.deleteFile`
- `file.renameFile`
- `file.copyFile`
- `file.moveFile`
- `file.createDirectory`
- `file.removeDirectory`
- `file.search`
- `file.metadata`
- `file.importText`
- `file.exportText`
- `audit.queryLogs`
- `audit.exportLog`
- `audit.recordClientEvent`

The exact request and response payloads should be specified during implementation planning for each migration phase. The method names above define the initial stable boundary.

## Data Flow

Normal frontend startup:

1. `TundraUX2` starts.
2. If configured to connect to an existing backend, it opens that connection.
3. Otherwise it launches `tundraux_backend_stdio` as a child process.
4. The frontend calls `session.startGuestSession`.
5. The shell renders the guest prompt from backend-returned session state.

Command execution:

1. User enters a shell command or navigates a TUI.
2. The frontend translates the action into a backend API request.
3. The backend validates the `sessionId`.
4. The backend checks authorization.
5. The backend performs the storage operation.
6. The backend writes an audit event for success or failure.
7. The backend returns structured data or a structured error.
8. The frontend renders the result.

Frontend code should treat backend user information as a display snapshot, not as the source of authorization truth.

## Concurrency Model

The backend core must be designed for multiple sessions. This is required even though the first stdio adapter may initially serve one frontend connection.

`SessionService` stores `sessionId -> user context`. Every privileged operation must include a `sessionId`. This makes future HTTP or shared-daemon adapters possible without changing service logic.

Storage concurrency in the first split keeps the existing files and directories:

- `user_data.dat` uses one read/write lock.
- Each canonical path under managed `Files/` uses a per-path read/write lock.
- Audit logs use a write queue or a mutex that preserves event order.
- Operations touching multiple paths acquire locks in canonical path sort order to avoid deadlocks.

Reads may run concurrently when they do not conflict with writes. Writes are exclusive for their affected resource.

## Safe Writes and Errors

Backend writes should use transaction-style file replacement:

1. Write to a temporary file in the same directory.
2. Flush and close the temporary file.
3. Atomically replace the target where the platform supports it.
4. Preserve the old target if writing fails before replacement.

Use this path for `user_data.dat`, `.TUX`, `.tlog`, and other managed file content.

Backend errors should be structured and stable. Initial error codes:

- `InvalidRequest`
- `UnknownMethod`
- `InvalidParams`
- `SessionExpired`
- `AuthenticationFailed`
- `PermissionDenied`
- `InvalidPath`
- `NotFound`
- `AlreadyExists`
- `Conflict`
- `StorageError`
- `InternalError`

The frontend maps these errors to existing user-facing messages where possible.

## Migration Plan

### Phase 1: Backend Skeleton

Add backend directories and CMake targets for `tundraux_backend_core` and `tundraux_backend_stdio`.

Implement:

- JSON-RPC request parsing and response formatting
- `session.startGuestSession`
- `session.login`
- `session.whoami`
- `session.logout`
- `user.listUsers`
- basic structured errors
- initial backend core tests
- protocol tests for valid requests, unknown methods, invalid JSON, and permission failures

### Phase 2: Shell and User Control

Move shell-facing user and permission flows behind the backend client:

- `login`
- `logout`
- `whoami`
- `listuser`
- `manageuser`
- `modify`
- `strict`
- privileged command authorization

`task_main()` should keep only `sessionId` and display-oriented user snapshots. It should not own mutable authorization truth.

### Phase 3: File and APP Migration

Move Explorer, TUX File Manager, editor file access, TUX metadata, import/export, and audit log export behind `file.*` and `audit.*` APIs.

Frontend APP modules keep UI state and rendering. Backend services own file system access, path validation, TUX encryption/decryption, metadata, and audit writes.

### Phase 4: Runtime Modes

Support both development and normal operation:

- `tundraux_backend_stdio` can run independently.
- `TundraUX2` can connect to an existing backend.
- `TundraUX2` can auto-launch the stdio backend for normal use.

CLI flags can be finalized during planning, but the design should include at least one explicit connect mode and one automatic mode.

## Testing Strategy

Backend core tests:

- login success and failure
- locked user rejection
- role authorization
- strict-mode reads and writes
- path normalization and traversal rejection
- TUX read/write round trip
- safe-write rollback on simulated failure
- audit event creation
- storage error mapping

Protocol tests:

- valid JSON-RPC request/response
- invalid JSON
- unknown method
- invalid params
- permission denied
- session expired

Concurrency tests:

- two sessions reading at the same time
- two sessions writing the same file
- one session reading while another writes the same file
- concurrent user-data update and file operation
- multi-path operations with stable lock ordering

Frontend tests:

- shell commands call the backend client instead of direct `DataManager` or file writes
- account UI renders backend-provided data and errors
- file UI renders backend-provided directory and content data
- permission errors from the backend are displayed correctly

Existing tests such as `audit_log_tests` and `explorer_*_tests` should be moved or split depending on whether they test backend behavior or frontend rendering.

## Acceptance Criteria

- The project builds `tundraux_backend_stdio` and `TundraUX2`.
- `TundraUX2` can auto-launch the backend for normal use.
- Developers can run the backend separately and connect the frontend to it.
- Login, logout, user management, strict mode, Explorer, TUX File Manager, and editor key paths keep the current user experience.
- Backend services enforce session and role authorization.
- Backend services are the only code paths that mutate `user_data.dat`, `.TUX`, `.tlog`, or managed files under `Files/`.
- Audit events are written by the backend for authorization failures, successful mutations, and storage failures.
- Concurrent backend calls do not corrupt user data, TUX files, audit logs, or managed files.
- Code search shows frontend modules no longer directly write user data, TUX files, audit logs, or managed file content.

## Future Work

- Add an HTTP or JSON-RPC-over-HTTP adapter on top of `tundraux_backend_core`.
- Add a shared local daemon mode that can accept multiple frontend clients at once.
- Evaluate a database-backed storage layer if file-locking complexity grows.
- Revisit cross-platform frontend support after the backend boundary is stable.
