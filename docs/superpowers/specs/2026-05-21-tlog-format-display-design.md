# TLOG Formatted Editor Display Design

## Context

TundraUX stores encrypted audit logs as `.tlog` files. When a privileged user opens a
`.tlog` file from explorer, `SYSTEM/audit/audit_log.cpp` decrypts the records, writes
them to a temporary `Files/temp/*-view.log` file, and opens that file in the editor.

Plain decrypted records currently use this shape:

```text
YYYY-MM-DD HH:MM:SS | user=<user> | type=<type> | <category> | <detail>
```

Because `user`, `type`, and `category` values vary in length, records do not line up
well in the editor.

## Goal

Format `.tlog` records for editor viewing so that the same logical columns align while
preserving the existing single-line record style. Long lines are acceptable because the
editor already supports horizontal scrolling.

## Non-Goals

- Do not change the encrypted `.tlog` file format.
- Do not change `readTlogPlaintext()` to return formatted records.
- Do not change `export log <tlog-file>` output.
- Do not add special `.tlog` parsing logic to the generic editor renderer.
- Do not wrap or truncate detail text.

## Approach

Add an internal formatter in `SYSTEM/audit/audit_log.cpp` that is used only by
`openTlogInEditor()` before writing the temporary view file.

The formatter will:

1. Parse decrypted lines by splitting on the existing ` | ` separator.
2. Treat lines with exactly five fields as structured audit records:
   - timestamp
   - user field
   - type field
   - category
   - detail
3. Compute the maximum width for the user, type, and category fields across structured
   records.
4. Rebuild structured records with right-padding on those fields:

```text
2026-05-21 22:10:11 | user=admin  | type=debug | explorer | open Logs/audit.tlog
2026-05-21 22:10:12 | user=(none) | type=guest | key      | Character 'x'
```

Timestamp and detail are not padded. Detail remains at the end of each line unchanged.

## Fallback Behavior

If a decrypted line does not match the expected five-field structure, the formatter will
leave that line unchanged. This keeps old, partial, or unexpected records readable and
avoids hiding malformed data.

## Data Flow

The display-only flow becomes:

```text
.tlog file
  -> readTlogPlaintext()
  -> decrypted raw lines
  -> format lines for editor view
  -> Files/temp/<stem>-view.log
  -> run_editor()
```

The export flow remains unchanged:

```text
.tlog file
  -> readTlogPlaintext()
  -> exported .log
```

## Testing

Add focused coverage to `tests/audit_log_tests.cpp` through the public
`openTlogInEditor()` path. The test can use the existing `run_editor()` test stub to
capture the temporary view file path, read the temporary content while the function is
running, and assert that structured lines have aligned user, type, and category columns.

Existing tests for redaction and plaintext reading should remain unchanged, confirming
that raw log content is still preserved outside the editor view.
