# TundraUX Backend Phase 2 Design

Date: 2026-05-31

## Purpose

Phase 1 created an independent stdio JSON-RPC backend process and moved the first session/user services behind it. Phase 2 makes that backend boundary part of the normal local `TundraUX2` runtime.

The goal is not a full remote backend. Phase 2 completes the local process split enough that the frontend no longer owns the primary session/auth boundary, and basic managed-file access can be served through backend RPC.

## Scope

Phase 2 includes:

- A frontend-side backend process manager that starts `tundraux_backend_stdio` when local stdio mode is selected.
- A frontend-side JSON-RPC client for line-delimited stdin/stdout requests.
- Startup/session initialization through `session.startGuestSession`.
- Frontend shell migration for `login`, `logout`, `whoami`, and `listuser`.
- Backend file API support for:
  - `file.listDirectory`
  - `file.readFile`
  - `file.writeFile`
- Local managed-file path containment under `Files`.
- Permission checks based on backend session identity.
- Tests for backend core behavior, JSON-RPC serialization, stdio process flow, and frontend client behavior.
- README updates describing Phase 2 and the remaining legacy areas.

Phase 2 excludes:

- HTTP transport.
- Shared daemon mode.
- Remote access.
- Full TUX command migration, including import/export/metadata.
- Audit API backend migration.
- Complete Explorer mutation migration.
- Replacement of the current XOR encryption scheme.
- OS CSPRNG session-token replacement unless it is required by local code touched during this phase.

## Architecture

Phase 2 keeps the existing layered backend shape:

- `tundraux_backend_core` owns domain services, DTOs, error mapping, and JSON-RPC dispatch.
- `tundraux_backend_adapters` wraps existing storage or filesystem implementations without leaking legacy public types into backend core APIs.
- `tundraux_backend_stdio` remains a narrow process wrapper over backend core and adapters.
- `TundraUX2` gains a frontend backend client layer and uses it from selected shell handlers.

The frontend client layer should be separate from shell command handlers. Shell code should not write raw JSON strings or manage child-process handles directly. It should call typed methods such as `login`, `logout`, `whoami`, `listUsers`, `listDirectory`, `readFile`, and `writeFile`.

The local runtime mode for Phase 2 is stdio. Phase 2 adds exactly these runtime choices:

- default: auto-start local stdio backend
- `--legacy-direct`: bypass the frontend backend client for development and regression comparison

## Backend Services

### Session And User

Existing Phase 1 methods remain protocol-compatible:

- `session.startGuestSession`
- `session.login`
- `session.logout`
- `session.whoami`
- `user.listUsers`

Frontend handlers must trust backend session state as the source of truth. The legacy `USER currentUser` value may still exist as a UI compatibility snapshot, but it should be refreshed from RPC results rather than independently authenticated through `DataManager`.

`login` continues to accept the username from the shell command and prompts locally for a password. The password is sent only in the RPC request and is not logged or echoed.

### File API

The new file service should expose regular managed-file operations under `Files`, not the full `.TUX` domain model.

`file.listDirectory` parameters:

- `sessionId`: string
- `path`: string, relative path under `Files`; empty string means root

Result:

- `entries`: array of objects with `name`, `path`, `type`, and optionally `size`

`file.readFile` parameters:

- `sessionId`: string
- `path`: string, relative path under `Files`

Result:

- `content`: string

`file.writeFile` parameters:

- `sessionId`: string
- `path`: string, relative path under `Files`
- `content`: string

Result:

- `ok`: true

Permission model:

- `guest` cannot list, read, or write managed files.
- `user`, `admin`, and `debug` can use regular managed-file APIs.
- `.TUX` files are not read or written through these regular text APIs in Phase 2.
- `.tlog`, `user_data.dat`, and paths outside `Files` are denied.
- Admin/debug-only TUX import/export/metadata remain in legacy code for this phase.

Path rules:

- Requests use relative paths only.
- Absolute paths are rejected.
- `.` and `..` path components are rejected.
- Resolved paths must stay inside the canonical `Files` root.
- Directory listing skips transient implementation directories such as `Files/temp`.

Content rules:

- `file.readFile` and `file.writeFile` are text-oriented.
- Large content should be bounded. The existing 16 MiB TUX content limit is a reasonable upper bound for Phase 2 regular file content as well.
- Binary files are not a Phase 2 goal.

## Frontend Integration

`TundraUX2` startup initializes the backend client before entering the shell. In local stdio mode it finds and launches `tundraux_backend_stdio` from the frontend executable directory. An explicit `--backend-stdio <path>` argument may override that path for tests and development.

Startup flow:

1. Preserve existing license and first-run user-data setup behavior.
2. Start or connect to the local stdio backend.
3. Call `session.startGuestSession`.
4. Initialize the shell compatibility `USER` snapshot from the returned user.
5. Enter the shell loop.
6. On process exit, close backend stdin and wait for backend termination where possible.

Migrated shell commands:

- `login <username>` prompts for the password and calls `session.login`.
- `logout` calls `session.logout` and refreshes the local snapshot to guest.
- `whoami` calls `session.whoami` or uses a verified cached snapshot after successful RPC.
- `listuser` calls `user.listUsers`, preserving admin/debug authorization from backend errors.

Editor and file access:

- Plain managed-file open/save calls `file.readFile` and `file.writeFile`.
- If the existing editor requires paths rather than content callbacks, Phase 2 introduces a small adapter that reads from backend into a controlled temp file and writes back through backend on save. The temp path must stay under `Files/temp` and must be removed after editor exit when possible.
- `.TUX` files remain handled by the existing TUX File Manager implementation in Phase 2.

Explorer:

- Explorer keeps existing rendering and local navigation behavior in Phase 2.
- Any file reads/writes migrated in this phase must use the same backend file client methods and permission semantics.
- Full Explorer mutation migration is explicitly Phase 3 work.

## Error Handling

Backend errors remain structured with stable error codes. Phase 2 should avoid exposing storage internals or filesystem details beyond useful user-facing messages.

Expected mappings:

- Missing or invalid params: `InvalidParams`
- Unknown method: `UnknownMethod`
- Missing or expired session: `SessionExpired`
- Guest or insufficient privilege: `PermissionDenied`
- Invalid path syntax: `InvalidPath`
- Denied path traversal or protected target: `PermissionDenied`
- Missing file: `NotFound`
- Corrupt storage or filesystem failure: `StorageError`
- Unexpected failure: `InternalError`

Frontend command handlers translate backend errors into concise console messages. Passwords and full raw JSON requests must not be logged.

## Testing

Backend tests:

- Session and user tests remain passing.
- Add file service tests for guest denial, user read/write/list success, path traversal denial, `.TUX` denial for text APIs, and missing-file behavior.
- Add JSON-RPC tests for file methods and parameter validation.
- Add stdio process tests for at least one file read/write/list sequence.

Frontend/client tests:

- Test JSON-RPC request construction and response parsing without launching the full TUI.
- Test client handling for success, backend error responses, malformed responses, and child-process startup failure.

End-to-end manual validation:

- Build succeeds.
- `ctest --test-dir build --output-on-failure` passes.
- Running `TundraUX2` starts in guest or debug according to the configured startup mode.
- `login`, `logout`, `whoami`, and `listuser` use backend responses.
- Plain file read/write through the migrated path works for a non-guest user.
- Existing TUX commands still work through legacy implementation.

## Compatibility And Migration

Existing `user_data.dat` remains the source of user storage through `DataManagerUserStore`. Existing `Files` content remains in place.

The frontend can keep the legacy `USER` struct internally during Phase 2 as a compatibility snapshot. New backend-facing code must use backend DTOs or frontend client DTOs, not legacy `USER`, at API boundaries.

The README should state that Phase 2 has a local stdio backend attached to the frontend, while TUX advanced operations, audit APIs, HTTP, remote access, and shared daemon mode remain future work.

## Completion Criteria

Phase 2 is complete when:

- `TundraUX2` initializes a local stdio backend client in normal local mode.
- Shell session/user commands listed in scope are served through backend RPC.
- Backend file list/read/write APIs exist and are covered by tests.
- The frontend has a typed RPC client layer rather than raw JSON in shell handlers.
- All registered tests pass.
- Documentation accurately distinguishes completed Phase 2 work from remaining Phase 3+ work.
