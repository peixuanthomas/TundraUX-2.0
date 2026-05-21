# TLOG Formatted Editor Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `.tlog` files opened in the editor display with aligned `user`, `type`, and `category` columns while preserving raw log storage and export behavior.

**Architecture:** Keep formatting inside the audit display path. `readTlogPlaintext()` continues to return raw decrypted records; `openTlogInEditor()` formats only the temporary `Files/temp/*-view.log` file that is passed to `run_editor()`.

**Tech Stack:** C++17, standard library strings/vectors/filesystem, existing CMake `audit_log_tests` target.

---

## File Structure

- Modify `SYSTEM/audit/audit_log.cpp`: add private parsing/formatting helpers in the anonymous namespace and call them from `openTlogInEditor()`.
- Modify `tests/audit_log_tests.cpp`: extend the existing `run_editor()` stub so tests can inspect the temporary view file, then assert display-only alignment.
- No header changes are needed because the formatter is internal to the audit implementation.

### Task 1: Add Failing Coverage for TLOG Editor View Alignment

**Files:**
- Modify: `tests/audit_log_tests.cpp`
- Test: `tests/audit_log_tests.cpp`

- [ ] **Step 1: Capture the temporary editor view file in the test stub**

Replace the current stub:

```cpp
int run_editor(const std::string&, const std::string&) {
    return 0;
}
```

with:

```cpp
namespace {

std::vector<std::string> g_editorViewLines;

std::vector<std::string> readTextLines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

} // namespace

int run_editor(const std::string& filepath, const std::string&) {
    g_editorViewLines = readTextLines(filepath);
    return 0;
}
```

- [ ] **Step 2: Add a helper to find a line containing text**

Inside the anonymous namespace in `tests/audit_log_tests.cpp`, after `containsLineFragment()`, add:

```cpp
std::string lineContaining(const std::vector<std::string>& lines, const std::string& fragment) {
    const auto found = std::find_if(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.find(fragment) != std::string::npos;
    });
    return found == lines.end() ? std::string{} : *found;
}
```

- [ ] **Step 3: Add assertions through `openTlogInEditor()`**

In `main()`, after the existing redaction assertions and before restoring the current directory, add:

```cpp
    tundraux::audit::logEvent("explorer", "open Logs/audit.tlog");
    tundraux::audit::logEvent("key", "Character 'z'");

    g_editorViewLines.clear();
    const int openResult = tundraux::audit::openTlogInEditor(
        tundraux::audit::startupLogPath().string(),
        "audit-test.tlog",
        "tester",
        "admin"
    );
    if (openResult != 0) {
        std::cerr << "failed to open audit log in editor: " << openResult << "\n";
        return 1;
    }

    const std::string explorerLine = lineContaining(g_editorViewLines, "explorer");
    const std::string keyLine = lineContaining(g_editorViewLines, "Character 'z'");
    if (explorerLine.empty() || keyLine.empty()) {
        std::cerr << "editor view did not include expected audit records\n";
        return 1;
    }

    const std::size_t explorerCategory = explorerLine.find("explorer");
    const std::size_t keyCategory = keyLine.find("key");
    const std::size_t explorerDetail = explorerLine.find("open Logs/audit.tlog");
    const std::size_t keyDetail = keyLine.find("Character 'z'");
    if (explorerCategory == std::string::npos || keyCategory == std::string::npos ||
        explorerDetail == std::string::npos || keyDetail == std::string::npos) {
        std::cerr << "editor view did not preserve category or detail text\n";
        return 1;
    }
    if (explorerCategory != keyCategory || explorerDetail != keyDetail) {
        std::cerr << "editor view columns are not aligned\n";
        return 1;
    }
```

- [ ] **Step 4: Run the focused test and verify it fails**

Run:

```bash
cmake --build build --target audit_log_tests
ctest --test-dir build -R audit_log_tests --output-on-failure
```

Expected: build succeeds, `audit_log_tests` fails with `editor view columns are not aligned`.

### Task 2: Implement Display-Only TLOG Formatting

**Files:**
- Modify: `SYSTEM/audit/audit_log.cpp`
- Test: `tests/audit_log_tests.cpp`

- [ ] **Step 1: Add a structured record type and splitter**

In the anonymous namespace of `SYSTEM/audit/audit_log.cpp`, after `userFieldValue()`, add:

```cpp
struct TlogDisplayRecord {
    bool structured = false;
    std::string original;
    std::string timestamp;
    std::string user;
    std::string type;
    std::string category;
    std::string detail;
};

std::vector<std::string> splitAuditFields(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t separator = line.find(" | ", start);
        if (separator == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, separator - start));
        start = separator + 3;
    }
    return fields;
}
```

- [ ] **Step 2: Add parser and padding helpers**

After `splitAuditFields()`, add:

```cpp
TlogDisplayRecord parseTlogDisplayRecord(const std::string& line) {
    TlogDisplayRecord record;
    record.original = line;

    const std::vector<std::string> fields = splitAuditFields(line);
    if (fields.size() != 5) {
        return record;
    }

    record.structured = true;
    record.timestamp = fields[0];
    record.user = fields[1];
    record.type = fields[2];
    record.category = fields[3];
    record.detail = fields[4];
    return record;
}

std::string rightPad(std::string value, std::size_t width) {
    if (value.size() < width) {
        value.append(width - value.size(), ' ');
    }
    return value;
}
```

- [ ] **Step 3: Add the editor-view formatter**

After `rightPad()`, add:

```cpp
std::vector<std::string> formatTlogLinesForEditor(const std::vector<std::string>& lines) {
    std::vector<TlogDisplayRecord> records;
    records.reserve(lines.size());

    std::size_t userWidth = 0;
    std::size_t typeWidth = 0;
    std::size_t categoryWidth = 0;

    for (const std::string& line : lines) {
        TlogDisplayRecord record = parseTlogDisplayRecord(line);
        if (record.structured) {
            userWidth = std::max(userWidth, record.user.size());
            typeWidth = std::max(typeWidth, record.type.size());
            categoryWidth = std::max(categoryWidth, record.category.size());
        }
        records.push_back(std::move(record));
    }

    std::vector<std::string> formatted;
    formatted.reserve(records.size());
    for (const TlogDisplayRecord& record : records) {
        if (!record.structured) {
            formatted.push_back(record.original);
            continue;
        }
        formatted.push_back(
            record.timestamp + " | " +
            rightPad(record.user, userWidth) + " | " +
            rightPad(record.type, typeWidth) + " | " +
            rightPad(record.category, categoryWidth) + " | " +
            record.detail
        );
    }
    return formatted;
}
```

- [ ] **Step 4: Use formatted lines only for the temporary editor view**

In `openTlogInEditor()`, replace:

```cpp
    const std::vector<std::string> lines = readTlogPlaintext(path, readError);
```

with:

```cpp
    const std::vector<std::string> rawLines = readTlogPlaintext(path, readError);
```

Then after the read error check, add:

```cpp
    const std::vector<std::string> lines = formatTlogLinesForEditor(rawLines);
```

- [ ] **Step 5: Run the focused test and verify it passes**

Run:

```bash
cmake --build build --target audit_log_tests
ctest --test-dir build -R audit_log_tests --output-on-failure
```

Expected: `audit_log_tests` passes.

### Task 3: Regression Verification and Commit

**Files:**
- Modify: `SYSTEM/audit/audit_log.cpp`
- Modify: `tests/audit_log_tests.cpp`
- Read: `docs/superpowers/specs/2026-05-21-tlog-format-display-design.md`

- [ ] **Step 1: Run the full test suite**

Run:

```bash
ctest --test-dir build --output-on-failure
```

Expected: all configured tests pass.

- [ ] **Step 2: Inspect the final diff**

Run:

```bash
git diff -- SYSTEM/audit/audit_log.cpp tests/audit_log_tests.cpp
```

Expected: the diff only adds display-only formatting and focused test coverage.

- [ ] **Step 3: Commit the implementation**

Run:

```bash
git add SYSTEM/audit/audit_log.cpp tests/audit_log_tests.cpp
git add -f docs/superpowers/plans/2026-05-21-tlog-format-display-implementation.md
git commit -m "feat: align tlog editor display"
```

Expected: one implementation commit containing the formatter, tests, and plan.
