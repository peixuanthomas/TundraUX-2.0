# TundraUX Backend Phase 3 First Batch Design

Date: 2026-06-01

## Purpose

Phase 2 made the local stdio backend the normal runtime boundary for session, user listing, and basic managed-file read/write access. Phase 3 moves file-centric APP behavior behind that backend boundary.

This first batch intentionally covers the smallest useful APP migration slice: Explorer and TUX File Manager should stop directly mutating managed files for their main file workflows. The frontend keeps interaction and rendering. The backend owns path validation, permission checks, filesystem mutation, and `.TUX` content persistence.

## Scope

Phase 3 first batch includes:

- Extending the backend file API beyond `listDirectory`, `readFile`, and `writeFile`.
- Adding backend support for directory creation/removal, delete, rename, copy, move, and search.
- Adding backend `.TUX` operations for list, create, read, write, delete, rename, copy, move, and search.
- Routing Explorer refresh, mkdir, delete, clipboard copy/move paste, and search through backend APIs.
- Routing TUX File Manager commands `ls`, `create`, `edit`, `view`, `delete`, `rename`, `copy`, `move`, `find`, `mkdir`, and `rmdir` through backend APIs.
- Preserving the existing `.TUX` binary format and XOR transform.
- Preserving existing user-facing command names and interaction flow where practical.
- Tests for backend core, filesystem adapters, JSON-RPC, stdio flow, frontend typed client mapping, and the migrated frontend operation layers.

Phase 3 first batch excludes:

- `.TUX` import from `.txt`.
- `.TUX` export to `.txt`.
- Standalone `.TUX` metadata command migration.
- Audit query/export APIs.
- Full backend-owned audit logging for all Explorer and TUX File Manager events.
- HTTP transport and shared daemon mode.
- Encryption replacement.
- Full removal of all frontend filesystem reads where they only support rendering or editor temporary-file handoff.

## Architecture

The existing Phase 2 target split remains:

- `tundraux_backend_core` owns service semantics, DTOs, authorization, error mapping, and JSON-RPC method dispatch.
- `tundraux_backend_adapters` owns concrete filesystem access under `Files`.
- `tundraux_backend_stdio` remains a narrow line-delimited JSON-RPC process wrapper.
- `APP/backend_client` exposes typed frontend calls and hides JSON-RPC details.
- `APP/explorer` and `APP/file_manager` keep UI state, command parsing, keyboard handling, confirmation prompts, editor handoff, and rendering.

Backend services should stay transport-independent. No Explorer or TUX File Manager UI concepts should enter backend core. The backend should expose file operations in terms of relative managed paths, operation options, and structured results.

## Backend API

Extend `file.*` for regular managed files and directories:

- `file.deleteFile`
- `file.renameFile`
- `file.copyFile`
- `file.moveFile`
- `file.createDirectory`
- `file.removeDirectory`
- `file.search`

Add `tux.*` for `.TUX` domain operations:

- `tux.list`
- `tux.create`
- `tux.read`
- `tux.write`
- `tux.delete`
- `tux.rename`
- `tux.copy`
- `tux.move`
- `tux.search`

The `.TUX` API uses extensionless display paths at the API boundary, matching existing TUX File Manager command behavior. The backend appends and validates `.TUX` internally. Regular `file.*` APIs use explicit relative paths and continue denying `.TUX`, `.tlog`, `user_data.dat`, protected temp paths, absolute paths, traversal, and paths outside `Files`.

Batch copy/move remains frontend-orchestrated for this batch. The frontend calls backend operations once per selected source and reports per-item results. This preserves the current TUX File Manager behavior and avoids introducing transaction semantics before the storage layer has locks and rollback support.

## Permissions

Every file or TUX operation must receive a `sessionId`.

Regular file and directory APIs:

- `guest` is denied.
- `user`, `admin`, and `debug` are allowed, subject to path protections.

TUX APIs:

- `guest` is denied.
- `admin` and `debug` may read and modify all `.TUX` files.
- A regular `user` may read and modify `.TUX` files where the metadata creator matches the current backend session user.
- A regular `user` is denied for `.TUX` files owned by another user.

The backend is the authority for all of these decisions. Frontend checks may remain only to improve messages or disable obvious actions, but they must not be trusted for authorization.

## Data Flow

### Explorer

Opening Explorer receives the active backend runtime or typed client context.

Directory refresh calls `file.listDirectory` and maps backend entries into the existing Explorer list model. Navigation and selection remain local UI state.

Explorer mutations call backend APIs:

- create folder: `file.createDirectory`
- delete: `file.deleteFile` or `file.removeDirectory`
- paste copy: `file.copyFile`
- paste move: `file.moveFile`
- search: `file.search`

Opening a regular text file continues to use the Phase 2 backend read/write editor path. Opening `.TUX` content should use the new `tux.read` and `tux.write` flow instead of direct frontend `.TUX` file access. Opening audit logs remains outside this first batch.

### TUX File Manager

`file_editor` receives backend runtime context.

Command mapping:

- `ls`, `list`, `ll`: `tux.list`
- `create`, `touch`, `new`: `tux.create`
- `view`, `cat`, `read`: `tux.read`
- `edit`, `open`: `tux.read`, local editor handoff, then `tux.write`
- `delete`, `remove`, `rm`, `del`: `tux.delete`
- `rename`: `tux.rename`
- `copy`, `cp`: `tux.copy`
- `move`, `mv`: `tux.move`
- `find`, `search`: `tux.search`
- `mkdir`, `md`: `file.createDirectory`
- `rmdir`, `rd`: `file.removeDirectory`

Overwrite confirmations, delete confirmations, batch command expansion, command history, and help text remain frontend responsibilities.

The editor may continue using controlled temporary files because the current editor operates on file paths. Temporary files must stay under a backend-denied temp area and be removed after use where possible. The durable `.TUX` read/write remains backend-owned.

## Path And Storage Rules

Requests use relative paths only.

The backend rejects:

- absolute paths
- `.` and `..` traversal
- paths outside the canonical `Files` root
- symlink or reparse-point traversal
- protected names such as `user_data.dat`
- `.tlog` through regular file APIs
- `.TUX` through regular file APIs
- `Files/temp` as a user-visible operation target

Directory removal in this batch should reject non-empty directories unless an explicit recursive option is introduced. Explorer and TUX File Manager currently do user confirmation before destructive operations; backend should still enforce safety and return `Conflict` for non-empty directories when recursive removal is not requested.

Copy and move operations must validate both source and destination before mutation. Same-source-and-destination operations return `Conflict`.

TUX writes preserve the current format version `1` and the existing metadata fields. On create, creator and last editor are the current backend user. On write, last editor and modify time update to the current backend user and current time. On copy, creator and last editor become the current backend user, preserving the current frontend behavior.

## Error Handling

Backend errors remain structured and stable:

- `InvalidParams`: parameters are missing or have the wrong type.
- `InvalidPath`: path syntax is invalid.
- `PermissionDenied`: session role is insufficient, path is protected, or canonical path containment fails.
- `NotFound`: source file or directory does not exist.
- `AlreadyExists`: destination exists and overwrite is not allowed.
- `Conflict`: directory is non-empty, source equals destination, or an operation conflicts with current state.
- `StorageError`: filesystem failure, corrupt `.TUX` content, or unsupported `.TUX` format.
- `SessionExpired`: session is missing or invalid.
- `InternalError`: unexpected failure.

Corrupt or unsupported `.TUX` files return `StorageError` with a concise message. Path traversal must not be masked as `NotFound`; it must return either `InvalidPath` or `PermissionDenied`.

Frontend handlers map these errors to existing concise console or Explorer status messages.

## Compatibility

Existing `Files` content and `.TUX` files remain valid. The first batch must not change `.TUX`, `.tlog`, or `user_data.dat` formats.

Legacy direct mode remains available for comparison where the existing command path still supports it. New backend-facing code should use backend DTOs or frontend client DTOs rather than legacy `USER` at API boundaries.

The README should be updated after implementation to state that Explorer and main TUX File Manager file operations have moved to the backend, while import/export/metadata and full audit APIs remain future Phase 3 work.

## Testing

Backend core and adapter tests:

- regular file delete, rename, copy, move, mkdir, rmdir, and search
- absolute path, traversal, protected target, `Files/temp`, symlink, and reparse-point rejection
- missing source and existing destination handling
- non-empty directory removal conflict
- same-source-and-destination conflict
- TUX create, list, read, write, delete, rename, copy, move, and search
- TUX guest denial
- TUX creator read/write success
- TUX non-creator denial
- TUX admin/debug access
- corrupt or unsupported TUX file handling

JSON-RPC and stdio tests:

- every new `file.*` method validates parameters and returns structured success or error responses
- every new `tux.*` method validates parameters and returns structured success or error responses
- one Explorer-style stdio flow covers list, mkdir, copy or move, search, and delete
- one TUX edit-style stdio flow covers create, read, write, copy or move, and delete

Frontend client tests:

- typed client builds the expected method names and params for every new API
- typed client parses success, backend error, malformed response, and transport failure for the new result types

Frontend APP tests:

- Explorer refresh maps backend entries into the existing list model
- Explorer delete confirmation calls the backend operation and refreshes on success
- Explorer mkdir and paste copy/move call backend APIs and surface failures
- Explorer search uses backend results while preserving UI navigation state
- TUX File Manager command dispatch maps first-batch commands to backend API calls
- TUX edit uses backend read/write around the local editor handoff
- TUX command failures produce concise messages without performing local file mutation

Full verification:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

## Completion Criteria

Phase 3 first batch is complete when:

- backend `file.*` mutation/search APIs exist and are covered by tests
- backend `tux.*` first-batch APIs exist and are covered by tests
- stdio JSON-RPC exposes those APIs
- frontend typed client exposes those APIs without raw JSON in APP modules
- Explorer first-batch file operations use backend APIs
- TUX File Manager first-batch commands use backend APIs
- regular frontend file mutation paths no longer directly mutate managed files for migrated operations
- all registered tests pass
- documentation distinguishes completed first-batch Phase 3 work from remaining import/export/metadata/audit work
