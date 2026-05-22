# TundraUX Rust Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first Rust migration stage: a Windows-first Rust executable with encrypted `.tdb` storage, core user/file/audit models, explicit legacy scan/import commands, and a CLI surface that can later be reused by a full TUI.

**Architecture:** Create a Rust workspace with focused crates: `tundra-core` for business models and validation, `tundra-storage` for authenticated encrypted containers, `tundra-legacy` for read-only C++ data import, and `tundra-cli` for commands. Keep UI concerns outside core and storage so future ratatui work can replace only the interaction layer.

**Tech Stack:** Rust 2021, Cargo workspace, `clap`, `serde`, `postcard`, `argon2`, `chacha20poly1305`, `rand`, `thiserror`, `tempfile`, and `assert_cmd`.

---

## Scope Check

This plan implements the first migration stage from `docs/superpowers/specs/2026-05-22-rust-migration-design.md`. It intentionally does not build the final ratatui UI or Rust text editor. Those are separate future specs after the CLI, storage, and import foundations are working.

## File Structure

Create these files:

- `Cargo.toml`: workspace membership and shared dependency versions.
- `crates/tundra-core/Cargo.toml`: core crate manifest.
- `crates/tundra-core/src/lib.rs`: public module exports.
- `crates/tundra-core/src/error.rs`: shared core error type.
- `crates/tundra-core/src/user.rs`: users, roles, account state, permission checks.
- `crates/tundra-core/src/vault_path.rs`: validated virtual vault paths.
- `crates/tundra-core/src/file.rs`: file metadata and in-memory vault file model.
- `crates/tundra-core/src/audit.rs`: audit event model.
- `crates/tundra-storage/Cargo.toml`: storage crate manifest.
- `crates/tundra-storage/src/lib.rs`: public storage exports.
- `crates/tundra-storage/src/error.rs`: storage error type.
- `crates/tundra-storage/src/container.rs`: `.tdb` container envelope, encryption, and decryption.
- `crates/tundra-storage/src/store.rs`: typed stores for users, files, audit, and metadata.
- `crates/tundra-legacy/Cargo.toml`: legacy crate manifest.
- `crates/tundra-legacy/src/lib.rs`: public legacy exports.
- `crates/tundra-legacy/src/error.rs`: legacy error type.
- `crates/tundra-legacy/src/scan.rs`: legacy project scanner.
- `crates/tundra-legacy/src/user_data.rs`: read-only `user_data.dat` parser.
- `crates/tundra-legacy/src/tux.rs`: read-only `.TUX` parser.
- `crates/tundra-legacy/src/tlog.rs`: read-only `.tlog` parser.
- `crates/tundra-cli/Cargo.toml`: CLI crate manifest.
- `crates/tundra-cli/src/main.rs`: executable entry point.
- `crates/tundra-cli/src/args.rs`: `clap` command definitions.
- `crates/tundra-cli/src/commands.rs`: command handlers.
- `crates/tundra-cli/tests/cli_smoke.rs`: CLI behavior tests.

Modify these files:

- `README.md`: add a short Rust migration status section after the build section once CLI commands exist.
- `README.zh-CN.md`: add the same short migration status section in Chinese once CLI commands exist.

## Task 1: Scaffold Rust Workspace

**Files:**
- Create: `Cargo.toml`
- Create: `crates/tundra-core/Cargo.toml`
- Create: `crates/tundra-core/src/lib.rs`
- Create: `crates/tundra-storage/Cargo.toml`
- Create: `crates/tundra-storage/src/lib.rs`
- Create: `crates/tundra-legacy/Cargo.toml`
- Create: `crates/tundra-legacy/src/lib.rs`
- Create: `crates/tundra-cli/Cargo.toml`
- Create: `crates/tundra-cli/src/main.rs`

- [ ] **Step 1: Create root workspace manifest**

Create `Cargo.toml`:

```toml
[workspace]
resolver = "2"
members = [
    "crates/tundra-core",
    "crates/tundra-storage",
    "crates/tundra-legacy",
    "crates/tundra-cli",
]

[workspace.package]
edition = "2021"
license = "MIT"
version = "0.1.0"

[workspace.dependencies]
anyhow = "1.0.86"
argon2 = "0.5.3"
assert_cmd = "2.0.14"
chacha20poly1305 = "0.10.1"
clap = { version = "4.5.4", features = ["derive"] }
postcard = { version = "1.0.8", features = ["alloc"] }
rand = "0.8.5"
serde = { version = "1.0.203", features = ["derive"] }
tempfile = "3.10.1"
thiserror = "1.0.61"
```

- [ ] **Step 2: Create crate manifests**

Create `crates/tundra-core/Cargo.toml`:

```toml
[package]
name = "tundra-core"
version.workspace = true
edition.workspace = true
license.workspace = true

[dependencies]
serde.workspace = true
thiserror.workspace = true
```

Create `crates/tundra-storage/Cargo.toml`:

```toml
[package]
name = "tundra-storage"
version.workspace = true
edition.workspace = true
license.workspace = true

[dependencies]
argon2.workspace = true
chacha20poly1305.workspace = true
postcard.workspace = true
rand.workspace = true
serde.workspace = true
thiserror.workspace = true
tundra-core = { path = "../tundra-core" }

[dev-dependencies]
tempfile.workspace = true
```

Create `crates/tundra-legacy/Cargo.toml`:

```toml
[package]
name = "tundra-legacy"
version.workspace = true
edition.workspace = true
license.workspace = true

[dependencies]
serde.workspace = true
thiserror.workspace = true
tundra-core = { path = "../tundra-core" }
```

Create `crates/tundra-cli/Cargo.toml`:

```toml
[package]
name = "tundraux"
version.workspace = true
edition.workspace = true
license.workspace = true

[[bin]]
name = "tundraux"
path = "src/main.rs"

[dependencies]
anyhow.workspace = true
clap.workspace = true
tundra-core = { path = "../tundra-core" }
tundra-legacy = { path = "../tundra-legacy" }
tundra-storage = { path = "../tundra-storage" }

[dev-dependencies]
assert_cmd.workspace = true
tempfile.workspace = true
```

- [ ] **Step 3: Add minimal crate entry points**

Create `crates/tundra-core/src/lib.rs`:

```rust
pub const VERSION: &str = env!("CARGO_PKG_VERSION");
```

Create `crates/tundra-storage/src/lib.rs`:

```rust
pub const STORAGE_FORMAT_VERSION: u16 = 1;
```

Create `crates/tundra-legacy/src/lib.rs`:

```rust
pub const LEGACY_IMPORT_VERSION: u16 = 1;
```

Create `crates/tundra-cli/src/main.rs`:

```rust
fn main() {
    println!("tundraux {}", tundra_core::VERSION);
}
```

- [ ] **Step 4: Verify workspace builds**

Run:

```powershell
cargo test
```

Expected: all crates compile and Cargo reports zero tests run.

- [ ] **Step 5: Commit scaffold**

Run:

```powershell
git add Cargo.toml crates
git commit -m "feat: scaffold rust workspace"
```

## Task 2: Add Core Business Models

**Files:**
- Modify: `crates/tundra-core/src/lib.rs`
- Create: `crates/tundra-core/src/error.rs`
- Create: `crates/tundra-core/src/user.rs`
- Create: `crates/tundra-core/src/vault_path.rs`
- Create: `crates/tundra-core/src/file.rs`
- Create: `crates/tundra-core/src/audit.rs`

- [ ] **Step 1: Write core model tests before implementation**

Append these test modules in the files named below as they are created in Step 3.

`crates/tundra-core/src/user.rs` test block:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn admin_can_manage_users() {
        assert!(Role::Admin.can_manage_users());
        assert!(!Role::User.can_manage_users());
        assert!(!Role::Guest.can_manage_users());
    }

    #[test]
    fn disabled_user_cannot_login() {
        let user = UserRecord {
            username: "alice".to_string(),
            role: Role::User,
            state: AccountState::Disabled,
            password_kdf: PasswordKdf::Argon2id {
                salt: vec![1, 2, 3],
                params: "m=19456,t=2,p=1".to_string(),
            },
            password_verifier: vec![4, 5, 6],
        };

        assert!(!user.can_login());
    }
}
```

`crates/tundra-core/src/vault_path.rs` test block:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_simple_relative_paths() {
        let path = VaultPath::parse("docs/readme").unwrap();
        assert_eq!(path.as_str(), "docs/readme");
    }

    #[test]
    fn rejects_parent_traversal() {
        let err = VaultPath::parse("../secret").unwrap_err();
        assert_eq!(err, CoreError::InvalidVaultPath("../secret".to_string()));
    }

    #[test]
    fn normalizes_backslashes() {
        let path = VaultPath::parse("docs\\readme").unwrap();
        assert_eq!(path.as_str(), "docs/readme");
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```powershell
cargo test -p tundra-core
```

Expected: compile fails because the modules and types are not implemented yet.

- [ ] **Step 3: Implement core modules**

Replace `crates/tundra-core/src/lib.rs` with:

```rust
pub mod audit;
pub mod error;
pub mod file;
pub mod user;
pub mod vault_path;

pub use audit::{AuditEvent, AuditKind};
pub use error::CoreError;
pub use file::{FileEntry, FileKind};
pub use user::{AccountState, PasswordKdf, Role, UserRecord};
pub use vault_path::VaultPath;

pub const VERSION: &str = env!("CARGO_PKG_VERSION");
```

Create `crates/tundra-core/src/error.rs`:

```rust
use thiserror::Error;

#[derive(Debug, Error, PartialEq, Eq)]
pub enum CoreError {
    #[error("invalid username: {0}")]
    InvalidUsername(String),
    #[error("invalid vault path: {0}")]
    InvalidVaultPath(String),
}
```

Create `crates/tundra-core/src/user.rs`:

```rust
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum Role {
    Guest,
    User,
    Admin,
}

impl Role {
    pub fn can_manage_users(self) -> bool {
        matches!(self, Role::Admin)
    }

    pub fn can_manage_files(self) -> bool {
        matches!(self, Role::User | Role::Admin)
    }

    pub fn can_view_audit(self) -> bool {
        matches!(self, Role::Admin)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum AccountState {
    Active,
    Disabled,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum PasswordKdf {
    Argon2id { salt: Vec<u8>, params: String },
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct UserRecord {
    pub username: String,
    pub role: Role,
    pub state: AccountState,
    pub password_kdf: PasswordKdf,
    pub password_verifier: Vec<u8>,
}

impl UserRecord {
    pub fn can_login(&self) -> bool {
        self.state == AccountState::Active
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn admin_can_manage_users() {
        assert!(Role::Admin.can_manage_users());
        assert!(!Role::User.can_manage_users());
        assert!(!Role::Guest.can_manage_users());
    }

    #[test]
    fn disabled_user_cannot_login() {
        let user = UserRecord {
            username: "alice".to_string(),
            role: Role::User,
            state: AccountState::Disabled,
            password_kdf: PasswordKdf::Argon2id {
                salt: vec![1, 2, 3],
                params: "m=19456,t=2,p=1".to_string(),
            },
            password_verifier: vec![4, 5, 6],
        };

        assert!(!user.can_login());
    }
}
```

Create `crates/tundra-core/src/vault_path.rs`:

```rust
use serde::{Deserialize, Serialize};

use crate::CoreError;

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
pub struct VaultPath(String);

impl VaultPath {
    pub fn parse(input: &str) -> Result<Self, CoreError> {
        let normalized = input.trim().replace('\\', "/");
        if normalized.is_empty()
            || normalized.starts_with('/')
            || normalized.split('/').any(|part| part.is_empty() || part == "." || part == "..")
        {
            return Err(CoreError::InvalidVaultPath(input.to_string()));
        }
        Ok(Self(normalized))
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_simple_relative_paths() {
        let path = VaultPath::parse("docs/readme").unwrap();
        assert_eq!(path.as_str(), "docs/readme");
    }

    #[test]
    fn rejects_parent_traversal() {
        let err = VaultPath::parse("../secret").unwrap_err();
        assert_eq!(err, CoreError::InvalidVaultPath("../secret".to_string()));
    }

    #[test]
    fn normalizes_backslashes() {
        let path = VaultPath::parse("docs\\readme").unwrap();
        assert_eq!(path.as_str(), "docs/readme");
    }
}
```

Create `crates/tundra-core/src/file.rs`:

```rust
use serde::{Deserialize, Serialize};

use crate::VaultPath;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum FileKind {
    File,
    Directory,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FileEntry {
    pub path: VaultPath,
    pub kind: FileKind,
    pub owner: String,
    pub created_unix: i64,
    pub modified_unix: i64,
    pub content: Vec<u8>,
}
```

Create `crates/tundra-core/src/audit.rs`:

```rust
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum AuditKind {
    Auth,
    User,
    File,
    LegacyImport,
    System,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AuditEvent {
    pub timestamp_unix: i64,
    pub actor: String,
    pub kind: AuditKind,
    pub message: String,
}
```

- [ ] **Step 4: Run core tests**

Run:

```powershell
cargo test -p tundra-core
```

Expected: all `tundra-core` tests pass.

- [ ] **Step 5: Commit core models**

Run:

```powershell
git add crates/tundra-core
git commit -m "feat: add rust core models"
```

## Task 3: Implement Encrypted `.tdb` Container

**Files:**
- Modify: `crates/tundra-storage/src/lib.rs`
- Create: `crates/tundra-storage/src/error.rs`
- Create: `crates/tundra-storage/src/container.rs`

- [ ] **Step 1: Write container round-trip and corruption tests**

Create `crates/tundra-storage/src/container.rs` with only the test module first:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encrypted_container_round_trips_payload() {
        let encrypted = encrypt_payload(ContainerType::Users, b"correct horse", b"payload").unwrap();
        assert_ne!(encrypted, b"payload");

        let decrypted = decrypt_payload(&encrypted, b"correct horse", ContainerType::Users).unwrap();
        assert_eq!(decrypted, b"payload");
    }

    #[test]
    fn wrong_password_fails() {
        let encrypted = encrypt_payload(ContainerType::Users, b"correct horse", b"payload").unwrap();
        let err = decrypt_payload(&encrypted, b"wrong password", ContainerType::Users).unwrap_err();
        assert!(matches!(err, StorageError::DecryptFailed));
    }

    #[test]
    fn tampered_ciphertext_fails() {
        let mut encrypted = encrypt_payload(ContainerType::Audit, b"secret", b"payload").unwrap();
        let last = encrypted.len() - 1;
        encrypted[last] ^= 0x55;

        let err = decrypt_payload(&encrypted, b"secret", ContainerType::Audit).unwrap_err();
        assert!(matches!(err, StorageError::DecryptFailed));
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```powershell
cargo test -p tundra-storage container
```

Expected: compile fails because `ContainerType`, `StorageError`, `encrypt_payload`, and `decrypt_payload` are missing.

- [ ] **Step 3: Implement container encryption**

Replace `crates/tundra-storage/src/lib.rs` with:

```rust
pub mod container;
pub mod error;

pub use container::{decrypt_payload, encrypt_payload, ContainerType};
pub use error::StorageError;

pub const STORAGE_FORMAT_VERSION: u16 = 1;
```

Create `crates/tundra-storage/src/error.rs`:

```rust
use thiserror::Error;

#[derive(Debug, Error, PartialEq, Eq)]
pub enum StorageError {
    #[error("invalid storage magic")]
    InvalidMagic,
    #[error("unsupported storage version: {0}")]
    UnsupportedVersion(u16),
    #[error("container type mismatch")]
    ContainerTypeMismatch,
    #[error("container is truncated")]
    Truncated,
    #[error("encryption failed")]
    EncryptFailed,
    #[error("decryption failed")]
    DecryptFailed,
}
```

Replace `crates/tundra-storage/src/container.rs` with:

```rust
use argon2::{Algorithm, Argon2, Params, Version};
use chacha20poly1305::aead::{Aead, KeyInit};
use chacha20poly1305::{ChaCha20Poly1305, Key, Nonce};
use rand::{rngs::OsRng, RngCore};

use crate::{StorageError, STORAGE_FORMAT_VERSION};

const MAGIC: &[u8; 8] = b"TUXTDB01";
const SALT_LEN: usize = 16;
const NONCE_LEN: usize = 12;
const KEY_LEN: usize = 32;
const HEADER_LEN: usize = 8 + 2 + 1 + SALT_LEN + NONCE_LEN + 4;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ContainerType {
    Users = 1,
    Files = 2,
    Audit = 3,
    Meta = 4,
}

impl ContainerType {
    fn from_byte(value: u8) -> Result<Self, StorageError> {
        match value {
            1 => Ok(Self::Users),
            2 => Ok(Self::Files),
            3 => Ok(Self::Audit),
            4 => Ok(Self::Meta),
            _ => Err(StorageError::ContainerTypeMismatch),
        }
    }
}

pub fn encrypt_payload(
    container_type: ContainerType,
    password: &[u8],
    plaintext: &[u8],
) -> Result<Vec<u8>, StorageError> {
    let mut salt = [0u8; SALT_LEN];
    let mut nonce = [0u8; NONCE_LEN];
    OsRng.fill_bytes(&mut salt);
    OsRng.fill_bytes(&mut nonce);

    let key = derive_key(password, &salt)?;
    let cipher = ChaCha20Poly1305::new(Key::from_slice(&key));
    let ciphertext = cipher
        .encrypt(Nonce::from_slice(&nonce), plaintext)
        .map_err(|_| StorageError::EncryptFailed)?;

    let mut out = Vec::with_capacity(HEADER_LEN + ciphertext.len());
    out.extend_from_slice(MAGIC);
    out.extend_from_slice(&STORAGE_FORMAT_VERSION.to_le_bytes());
    out.push(container_type as u8);
    out.extend_from_slice(&salt);
    out.extend_from_slice(&nonce);
    out.extend_from_slice(&(ciphertext.len() as u32).to_le_bytes());
    out.extend_from_slice(&ciphertext);
    Ok(out)
}

pub fn decrypt_payload(
    bytes: &[u8],
    password: &[u8],
    expected_type: ContainerType,
) -> Result<Vec<u8>, StorageError> {
    if bytes.len() < HEADER_LEN {
        return Err(StorageError::Truncated);
    }
    if &bytes[0..8] != MAGIC {
        return Err(StorageError::InvalidMagic);
    }

    let version = u16::from_le_bytes([bytes[8], bytes[9]]);
    if version != STORAGE_FORMAT_VERSION {
        return Err(StorageError::UnsupportedVersion(version));
    }

    let actual_type = ContainerType::from_byte(bytes[10])?;
    if actual_type != expected_type {
        return Err(StorageError::ContainerTypeMismatch);
    }

    let salt = &bytes[11..27];
    let nonce = &bytes[27..39];
    let len = u32::from_le_bytes([bytes[39], bytes[40], bytes[41], bytes[42]]) as usize;
    if bytes.len() != HEADER_LEN + len {
        return Err(StorageError::Truncated);
    }

    let ciphertext = &bytes[HEADER_LEN..];
    let key = derive_key(password, salt)?;
    let cipher = ChaCha20Poly1305::new(Key::from_slice(&key));
    cipher
        .decrypt(Nonce::from_slice(nonce), ciphertext)
        .map_err(|_| StorageError::DecryptFailed)
}

fn derive_key(password: &[u8], salt: &[u8]) -> Result<[u8; KEY_LEN], StorageError> {
    let params = Params::new(19_456, 2, 1, Some(KEY_LEN)).map_err(|_| StorageError::EncryptFailed)?;
    let argon2 = Argon2::new(Algorithm::Argon2id, Version::V0x13, params);
    let mut key = [0u8; KEY_LEN];
    argon2
        .hash_password_into(password, salt, &mut key)
        .map_err(|_| StorageError::EncryptFailed)?;
    Ok(key)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encrypted_container_round_trips_payload() {
        let encrypted = encrypt_payload(ContainerType::Users, b"correct horse", b"payload").unwrap();
        assert_ne!(encrypted, b"payload");

        let decrypted = decrypt_payload(&encrypted, b"correct horse", ContainerType::Users).unwrap();
        assert_eq!(decrypted, b"payload");
    }

    #[test]
    fn wrong_password_fails() {
        let encrypted = encrypt_payload(ContainerType::Users, b"correct horse", b"payload").unwrap();
        let err = decrypt_payload(&encrypted, b"wrong password", ContainerType::Users).unwrap_err();
        assert!(matches!(err, StorageError::DecryptFailed));
    }

    #[test]
    fn tampered_ciphertext_fails() {
        let mut encrypted = encrypt_payload(ContainerType::Audit, b"secret", b"payload").unwrap();
        let last = encrypted.len() - 1;
        encrypted[last] ^= 0x55;

        let err = decrypt_payload(&encrypted, b"secret", ContainerType::Audit).unwrap_err();
        assert!(matches!(err, StorageError::DecryptFailed));
    }
}
```

- [ ] **Step 4: Run storage tests**

Run:

```powershell
cargo test -p tundra-storage container
```

Expected: container round-trip, wrong password, and tamper tests pass.

- [ ] **Step 5: Commit encrypted container**

Run:

```powershell
git add crates/tundra-storage
git commit -m "feat: add encrypted storage container"
```

## Task 4: Add Typed Stores for Users, Files, Audit, and Metadata

**Files:**
- Modify: `crates/tundra-storage/src/lib.rs`
- Create: `crates/tundra-storage/src/store.rs`

- [ ] **Step 1: Write typed store round-trip tests**

Create `crates/tundra-storage/src/store.rs` with only this test module first:

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;
    use tundra_core::{AccountState, PasswordKdf, Role, UserRecord};

    #[test]
    fn user_store_round_trips_records() {
        let dir = tempdir().unwrap();
        let path = dir.path().join("users.tdb");
        let store = UserStore {
            users: vec![UserRecord {
                username: "admin".to_string(),
                role: Role::Admin,
                state: AccountState::Active,
                password_kdf: PasswordKdf::Argon2id {
                    salt: vec![1, 2, 3],
                    params: "m=19456,t=2,p=1".to_string(),
                },
                password_verifier: vec![7, 8, 9],
            }],
        };

        save_store(&path, ContainerType::Users, b"password", &store).unwrap();
        let loaded: UserStore = load_store(&path, ContainerType::Users, b"password").unwrap();
        assert_eq!(loaded.users[0].username, "admin");
        assert_eq!(loaded.users[0].role, Role::Admin);
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```powershell
cargo test -p tundra-storage store
```

Expected: compile fails because typed stores and `save_store`/`load_store` are missing.

- [ ] **Step 3: Implement typed stores**

Modify `crates/tundra-storage/src/lib.rs`:

```rust
pub mod container;
pub mod error;
pub mod store;

pub use container::{decrypt_payload, encrypt_payload, ContainerType};
pub use error::StorageError;
pub use store::{load_store, save_store, AuditStore, FileStore, MetaStore, UserStore};

pub const STORAGE_FORMAT_VERSION: u16 = 1;
```

Replace `crates/tundra-storage/src/store.rs` with:

```rust
use std::fs;
use std::path::Path;

use serde::{de::DeserializeOwned, Deserialize, Serialize};
use tundra_core::{AuditEvent, FileEntry, UserRecord};

use crate::{decrypt_payload, encrypt_payload, ContainerType, StorageError};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct UserStore {
    pub users: Vec<UserRecord>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct FileStore {
    pub files: Vec<FileEntry>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct AuditStore {
    pub events: Vec<AuditEvent>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct MetaStore {
    pub instance_id: String,
    pub imported_batches: Vec<String>,
}

impl Default for MetaStore {
    fn default() -> Self {
        Self {
            instance_id: "local-dev".to_string(),
            imported_batches: Vec::new(),
        }
    }
}

pub fn save_store<T: Serialize>(
    path: &Path,
    container_type: ContainerType,
    password: &[u8],
    value: &T,
) -> Result<(), StorageError> {
    let payload = postcard::to_allocvec(value).map_err(|_| StorageError::EncryptFailed)?;
    let encrypted = encrypt_payload(container_type, password, &payload)?;
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|_| StorageError::EncryptFailed)?;
    }
    fs::write(path, encrypted).map_err(|_| StorageError::EncryptFailed)
}

pub fn load_store<T: DeserializeOwned>(
    path: &Path,
    container_type: ContainerType,
    password: &[u8],
) -> Result<T, StorageError> {
    let bytes = fs::read(path).map_err(|_| StorageError::DecryptFailed)?;
    let payload = decrypt_payload(&bytes, password, container_type)?;
    postcard::from_bytes(&payload).map_err(|_| StorageError::DecryptFailed)
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;
    use tundra_core::{AccountState, PasswordKdf, Role, UserRecord};

    #[test]
    fn user_store_round_trips_records() {
        let dir = tempdir().unwrap();
        let path = dir.path().join("users.tdb");
        let store = UserStore {
            users: vec![UserRecord {
                username: "admin".to_string(),
                role: Role::Admin,
                state: AccountState::Active,
                password_kdf: PasswordKdf::Argon2id {
                    salt: vec![1, 2, 3],
                    params: "m=19456,t=2,p=1".to_string(),
                },
                password_verifier: vec![7, 8, 9],
            }],
        };

        save_store(&path, ContainerType::Users, b"password", &store).unwrap();
        let loaded: UserStore = load_store(&path, ContainerType::Users, b"password").unwrap();
        assert_eq!(loaded.users[0].username, "admin");
        assert_eq!(loaded.users[0].role, Role::Admin);
    }
}
```

- [ ] **Step 4: Run typed store tests**

Run:

```powershell
cargo test -p tundra-storage store
```

Expected: typed store tests pass.

- [ ] **Step 5: Commit typed stores**

Run:

```powershell
git add crates/tundra-storage
git commit -m "feat: add typed encrypted stores"
```

## Task 5: Add CLI Argument Model

**Files:**
- Create: `crates/tundra-cli/src/args.rs`
- Modify: `crates/tundra-cli/src/main.rs`

- [ ] **Step 1: Write CLI help smoke test**

Create `crates/tundra-cli/tests/cli_smoke.rs`:

```rust
use assert_cmd::Command;

#[test]
fn help_mentions_legacy_commands() {
    let mut cmd = Command::cargo_bin("tundraux").unwrap();
    cmd.arg("--help")
        .assert()
        .success()
        .stdout(predicates::str::contains("legacy"));
}
```

Add `predicates` to root workspace dependencies:

```toml
predicates = "3.1.0"
```

Add `predicates.workspace = true` under `crates/tundra-cli/Cargo.toml` `[dev-dependencies]`.

- [ ] **Step 2: Run CLI test to verify it fails**

Run:

```powershell
cargo test -p tundraux help_mentions_legacy_commands
```

Expected: compile fails until `predicates` is wired or test fails because `--help` does not contain `legacy`.

- [ ] **Step 3: Implement `clap` command definitions**

Create `crates/tundra-cli/src/args.rs`:

```rust
use std::path::PathBuf;

use clap::{Parser, Subcommand, ValueEnum};

#[derive(Debug, Parser)]
#[command(name = "tundraux")]
#[command(about = "TundraUX Rust migration CLI")]
pub struct Args {
    #[command(subcommand)]
    pub command: Command,
}

#[derive(Debug, Subcommand)]
pub enum Command {
    Init {
        #[arg(long, default_value = "data")]
        data_dir: PathBuf,
        #[arg(long)]
        admin: String,
        #[arg(long)]
        password: String,
    },
    Login {
        username: String,
        #[arg(long)]
        password: String,
        #[arg(long, default_value = "data")]
        data_dir: PathBuf,
    },
    User {
        #[command(subcommand)]
        command: UserCommand,
    },
    File {
        #[command(subcommand)]
        command: FileCommand,
    },
    Audit {
        #[command(subcommand)]
        command: AuditCommand,
    },
    Legacy {
        #[command(subcommand)]
        command: LegacyCommand,
    },
}

#[derive(Debug, Subcommand)]
pub enum UserCommand {
    List {
        #[arg(long, default_value = "data")]
        data_dir: PathBuf,
        #[arg(long)]
        password: String,
    },
    Add {
        username: String,
        #[arg(long)]
        role: RoleArg,
    },
    Disable {
        username: String,
    },
}

#[derive(Debug, Clone, Copy, ValueEnum)]
pub enum RoleArg {
    User,
    Admin,
}

#[derive(Debug, Subcommand)]
pub enum FileCommand {
    List {
        path: Option<String>,
        #[arg(long, default_value = "data")]
        data_dir: PathBuf,
        #[arg(long)]
        password: String,
    },
    Import {
        host_path: PathBuf,
        vault_path: String,
    },
    Export {
        vault_path: String,
        host_path: PathBuf,
    },
    Remove {
        vault_path: String,
    },
}

#[derive(Debug, Subcommand)]
pub enum AuditCommand {
    List {
        #[arg(long, default_value = "data")]
        data_dir: PathBuf,
        #[arg(long)]
        password: String,
    },
    Export {
        host_path: PathBuf,
    },
}

#[derive(Debug, Subcommand)]
pub enum LegacyCommand {
    Scan {
        #[arg(long)]
        from: PathBuf,
    },
    Import {
        #[arg(long)]
        from: PathBuf,
        #[arg(long, default_value = "data")]
        data_dir: PathBuf,
        #[arg(long)]
        password: String,
    },
}
```

Modify `crates/tundra-cli/src/main.rs`:

```rust
mod args;

use clap::Parser;

use args::{Args, Command};

fn main() {
    let args = Args::parse();
    match args.command {
        Command::Init { .. } => println!("init"),
        Command::Login { username, .. } => println!("login {username}"),
        Command::User { .. } => println!("user"),
        Command::File { .. } => println!("file"),
        Command::Audit { .. } => println!("audit"),
        Command::Legacy { .. } => println!("legacy"),
    }
}
```

- [ ] **Step 4: Run CLI help test**

Run:

```powershell
cargo test -p tundraux help_mentions_legacy_commands
```

Expected: test passes and help output mentions the `legacy` subcommand.

- [ ] **Step 5: Commit CLI argument model**

Run:

```powershell
git add Cargo.toml crates/tundra-cli
git commit -m "feat: add rust cli command model"
```

## Task 6: Implement `init`, `login`, `user list`, `file list`, and `audit list`

**Files:**
- Create: `crates/tundra-cli/src/commands.rs`
- Modify: `crates/tundra-cli/src/main.rs`
- Modify: `crates/tundra-cli/src/args.rs`
- Modify: `crates/tundra-cli/tests/cli_smoke.rs`

- [ ] **Step 1: Write CLI init/list integration test**

Append to `crates/tundra-cli/tests/cli_smoke.rs`:

```rust
#[test]
fn init_then_list_users() {
    let temp = tempfile::tempdir().unwrap();
    let data = temp.path().join("data");

    Command::cargo_bin("tundraux")
        .unwrap()
        .args([
            "init",
            "--data-dir",
            data.to_str().unwrap(),
            "--admin",
            "admin",
            "--password",
            "secret123",
        ])
        .assert()
        .success()
        .stdout(predicates::str::contains("initialized"));

    Command::cargo_bin("tundraux")
        .unwrap()
        .args([
            "user",
            "list",
            "--data-dir",
            data.to_str().unwrap(),
            "--password",
            "secret123",
        ])
        .assert()
        .success()
        .stdout(predicates::str::contains("admin"));
}
```

- [ ] **Step 2: Run integration test to verify it fails**

Run:

```powershell
cargo test -p tundraux init_then_list_users
```

Expected: test fails because command handlers only print fixed command names.

- [ ] **Step 3: Implement command handlers**

Create `crates/tundra-cli/src/commands.rs`:

```rust
use std::path::Path;

use anyhow::Result;
use tundra_core::{AccountState, PasswordKdf, Role, UserRecord};
use tundra_storage::{load_store, save_store, AuditStore, ContainerType, FileStore, MetaStore, UserStore};

use crate::args::{AuditCommand, Command, FileCommand, LegacyCommand, UserCommand};

pub fn run(command: Command) -> Result<()> {
    match command {
        Command::Init {
            data_dir,
            admin,
            password,
        } => init(&data_dir, &admin, password.as_bytes()),
        Command::Login {
            username,
            password,
            data_dir,
        } => login(&data_dir, &username, password.as_bytes()),
        Command::User { command } => run_user(command),
        Command::File { command } => run_file(command),
        Command::Audit { command } => run_audit(command),
        Command::Legacy { command } => run_legacy(command),
    }
}

fn init(data_dir: &Path, admin: &str, password: &[u8]) -> Result<()> {
    let user = UserRecord {
        username: admin.to_string(),
        role: Role::Admin,
        state: AccountState::Active,
        password_kdf: PasswordKdf::Argon2id {
            salt: vec![0],
            params: "container-key-v1".to_string(),
        },
        password_verifier: Vec::new(),
    };
    save_store(
        &data_dir.join("users.tdb"),
        ContainerType::Users,
        password,
        &UserStore { users: vec![user] },
    )?;
    save_store(
        &data_dir.join("files.tdb"),
        ContainerType::Files,
        password,
        &FileStore::default(),
    )?;
    save_store(
        &data_dir.join("audit.tdb"),
        ContainerType::Audit,
        password,
        &AuditStore::default(),
    )?;
    save_store(
        &data_dir.join("meta.tdb"),
        ContainerType::Meta,
        password,
        &MetaStore::default(),
    )?;
    println!("initialized {}", data_dir.display());
    Ok(())
}

fn login(data_dir: &Path, username: &str, password: &[u8]) -> Result<()> {
    let users: UserStore = load_store(&data_dir.join("users.tdb"), ContainerType::Users, password)?;
    let found = users.users.iter().any(|user| user.username == username && user.can_login());
    if found {
        println!("login ok {username}");
    } else {
        anyhow::bail!("login failed for {username}");
    }
    Ok(())
}

fn run_user(command: UserCommand) -> Result<()> {
    match command {
        UserCommand::List { data_dir, password } => {
            let users: UserStore = load_store(&data_dir.join("users.tdb"), ContainerType::Users, password.as_bytes())?;
            for user in users.users {
                println!("{}\t{:?}\t{:?}", user.username, user.role, user.state);
            }
            Ok(())
        }
        UserCommand::Add { username, role } => {
            println!("user add {username} {role:?}");
            Ok(())
        }
        UserCommand::Disable { username } => {
            println!("user disable {username}");
            Ok(())
        }
    }
}

fn run_file(command: FileCommand) -> Result<()> {
    match command {
        FileCommand::List {
            data_dir,
            password,
            path,
        } => {
            let store: FileStore = load_store(&data_dir.join("files.tdb"), ContainerType::Files, password.as_bytes())?;
            let prefix = path.unwrap_or_default();
            for file in store.files {
                if prefix.is_empty() || file.path.as_str().starts_with(&prefix) {
                    println!("{}", file.path.as_str());
                }
            }
            Ok(())
        }
        FileCommand::Import { host_path, vault_path } => {
            println!("file import {} {vault_path}", host_path.display());
            Ok(())
        }
        FileCommand::Export { vault_path, host_path } => {
            println!("file export {vault_path} {}", host_path.display());
            Ok(())
        }
        FileCommand::Remove { vault_path } => {
            println!("file remove {vault_path}");
            Ok(())
        }
    }
}

fn run_audit(command: AuditCommand) -> Result<()> {
    match command {
        AuditCommand::List { data_dir, password } => {
            let store: AuditStore = load_store(&data_dir.join("audit.tdb"), ContainerType::Audit, password.as_bytes())?;
            for event in store.events {
                println!("{}\t{:?}\t{}\t{}", event.timestamp_unix, event.kind, event.actor, event.message);
            }
            Ok(())
        }
        AuditCommand::Export { host_path } => {
            println!("audit export {}", host_path.display());
            Ok(())
        }
    }
}

fn run_legacy(command: LegacyCommand) -> Result<()> {
    match command {
        LegacyCommand::Scan { from } => {
            println!("legacy scan {}", from.display());
            Ok(())
        }
        LegacyCommand::Import { from, .. } => {
            println!("legacy import {}", from.display());
            Ok(())
        }
    }
}
```

Modify `crates/tundra-cli/src/main.rs`:

```rust
mod args;
mod commands;

use clap::Parser;

use args::Args;

fn main() -> anyhow::Result<()> {
    let args = Args::parse();
    commands::run(args.command)
}
```

- [ ] **Step 4: Run CLI init/list tests**

Run:

```powershell
cargo test -p tundraux init_then_list_users
```

Expected: test passes.

- [ ] **Step 5: Commit basic CLI commands**

Run:

```powershell
git add crates/tundra-cli
git commit -m "feat: add basic rust cli commands"
```

## Task 7: Implement Legacy Scan

**Files:**
- Modify: `crates/tundra-legacy/src/lib.rs`
- Create: `crates/tundra-legacy/src/error.rs`
- Create: `crates/tundra-legacy/src/scan.rs`
- Modify: `crates/tundra-cli/src/commands.rs`
- Modify: `crates/tundra-cli/tests/cli_smoke.rs`

- [ ] **Step 1: Write legacy scan unit test**

Create `crates/tundra-legacy/src/scan.rs` with only this test module first:

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn scan_counts_legacy_files() {
        let dir = tempdir().unwrap();
        fs::write(dir.path().join("user_data.dat"), b"legacy users").unwrap();
        fs::create_dir_all(dir.path().join("Files/docs")).unwrap();
        fs::write(dir.path().join("Files/docs/a.TUX"), b"legacy tux").unwrap();
        fs::create_dir_all(dir.path().join("Logs")).unwrap();
        fs::write(dir.path().join("Logs/a.tlog"), b"legacy log").unwrap();

        let report = scan_legacy_project(dir.path()).unwrap();
        assert!(report.has_user_data);
        assert_eq!(report.tux_files, 1);
        assert_eq!(report.tlog_files, 1);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
cargo test -p tundra-legacy scan_counts_legacy_files
```

Expected: compile fails because scanner types are missing.

- [ ] **Step 3: Implement scanner**

Modify `crates/tundra-legacy/src/lib.rs`:

```rust
pub mod error;
pub mod scan;

pub use error::LegacyError;
pub use scan::{scan_legacy_project, LegacyScanReport};

pub const LEGACY_IMPORT_VERSION: u16 = 1;
```

Create `crates/tundra-legacy/src/error.rs`:

```rust
use thiserror::Error;

#[derive(Debug, Error)]
pub enum LegacyError {
    #[error("legacy root does not exist")]
    MissingRoot,
    #[error("failed to read legacy directory")]
    ReadFailed,
}
```

Replace `crates/tundra-legacy/src/scan.rs` with:

```rust
use std::fs;
use std::path::Path;

use crate::LegacyError;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LegacyScanReport {
    pub has_user_data: bool,
    pub tux_files: usize,
    pub tlog_files: usize,
    pub unreadable_entries: usize,
}

pub fn scan_legacy_project(root: &Path) -> Result<LegacyScanReport, LegacyError> {
    if !root.exists() {
        return Err(LegacyError::MissingRoot);
    }
    Ok(LegacyScanReport {
        has_user_data: root.join("user_data.dat").is_file(),
        tux_files: count_extension(&root.join("Files"), "TUX")?,
        tlog_files: count_extension(&root.join("Logs"), "tlog")?,
        unreadable_entries: 0,
    })
}

fn count_extension(root: &Path, extension: &str) -> Result<usize, LegacyError> {
    if !root.exists() {
        return Ok(0);
    }
    let mut count = 0;
    let mut stack = vec![root.to_path_buf()];
    while let Some(dir) = stack.pop() {
        for entry in fs::read_dir(dir).map_err(|_| LegacyError::ReadFailed)? {
            let entry = entry.map_err(|_| LegacyError::ReadFailed)?;
            let path = entry.path();
            if path.is_dir() {
                stack.push(path);
            } else if path
                .extension()
                .and_then(|value| value.to_str())
                .is_some_and(|value| value.eq_ignore_ascii_case(extension))
            {
                count += 1;
            }
        }
    }
    Ok(count)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn scan_counts_legacy_files() {
        let dir = tempdir().unwrap();
        fs::write(dir.path().join("user_data.dat"), b"legacy users").unwrap();
        fs::create_dir_all(dir.path().join("Files/docs")).unwrap();
        fs::write(dir.path().join("Files/docs/a.TUX"), b"legacy tux").unwrap();
        fs::create_dir_all(dir.path().join("Logs")).unwrap();
        fs::write(dir.path().join("Logs/a.tlog"), b"legacy log").unwrap();

        let report = scan_legacy_project(dir.path()).unwrap();
        assert!(report.has_user_data);
        assert_eq!(report.tux_files, 1);
        assert_eq!(report.tlog_files, 1);
    }
}
```

- [ ] **Step 4: Wire CLI `legacy scan`**

In `crates/tundra-cli/src/commands.rs`, replace the `LegacyCommand::Scan` arm with:

```rust
LegacyCommand::Scan { from } => {
    let report = tundra_legacy::scan_legacy_project(&from)?;
    println!("user_data: {}", report.has_user_data);
    println!("tux_files: {}", report.tux_files);
    println!("tlog_files: {}", report.tlog_files);
    println!("unreadable_entries: {}", report.unreadable_entries);
    Ok(())
}
```

- [ ] **Step 5: Run scanner tests**

Run:

```powershell
cargo test -p tundra-legacy
cargo test -p tundraux help_mentions_legacy_commands
```

Expected: legacy scanner tests pass and CLI still builds.

- [ ] **Step 6: Commit legacy scan**

Run:

```powershell
git add crates/tundra-legacy crates/tundra-cli
git commit -m "feat: add legacy scan command"
```

## Task 8: Implement Minimal Legacy Import

**Files:**
- Create: `crates/tundra-legacy/src/tux.rs`
- Create: `crates/tundra-legacy/src/tlog.rs`
- Create: `crates/tundra-legacy/src/user_data.rs`
- Modify: `crates/tundra-legacy/src/lib.rs`
- Modify: `crates/tundra-cli/src/commands.rs`
- Modify: `crates/tundra-cli/tests/cli_smoke.rs`

- [ ] **Step 1: Write CLI import smoke test**

Append to `crates/tundra-cli/tests/cli_smoke.rs`:

```rust
#[test]
fn legacy_import_records_batch() {
    let temp = tempfile::tempdir().unwrap();
    let data = temp.path().join("data");
    let old = temp.path().join("old");
    std::fs::create_dir_all(old.join("Files")).unwrap();
    std::fs::write(old.join("user_data.dat"), b"legacy users").unwrap();
    std::fs::write(old.join("Files").join("note.TUX"), b"legacy tux").unwrap();

    Command::cargo_bin("tundraux")
        .unwrap()
        .args([
            "init",
            "--data-dir",
            data.to_str().unwrap(),
            "--admin",
            "admin",
            "--password",
            "secret123",
        ])
        .assert()
        .success();

    Command::cargo_bin("tundraux")
        .unwrap()
        .args([
            "legacy",
            "import",
            "--from",
            old.to_str().unwrap(),
            "--data-dir",
            data.to_str().unwrap(),
            "--password",
            "secret123",
        ])
        .assert()
        .success()
        .stdout(predicates::str::contains("imported legacy batch"));
}
```

- [ ] **Step 2: Run import test to verify it fails**

Run:

```powershell
cargo test -p tundraux legacy_import_records_batch
```

Expected: test fails because `legacy import` only prints the source path.

- [ ] **Step 3: Add minimal legacy reader modules**

Modify `crates/tundra-legacy/src/lib.rs`:

```rust
pub mod error;
pub mod scan;
pub mod tlog;
pub mod tux;
pub mod user_data;

pub use error::LegacyError;
pub use scan::{scan_legacy_project, LegacyScanReport};
pub use tlog::read_legacy_tlogs;
pub use tux::read_legacy_tux_files;
pub use user_data::read_legacy_users;

pub const LEGACY_IMPORT_VERSION: u16 = 1;
```

Create `crates/tundra-legacy/src/user_data.rs`:

```rust
use std::path::Path;

use tundra_core::{AccountState, PasswordKdf, Role, UserRecord};

use crate::LegacyError;

pub fn read_legacy_users(root: &Path) -> Result<Vec<UserRecord>, LegacyError> {
    let path = root.join("user_data.dat");
    if !path.exists() {
        return Ok(Vec::new());
    }
    Ok(vec![UserRecord {
        username: "legacy-admin".to_string(),
        role: Role::Admin,
        state: AccountState::Active,
        password_kdf: PasswordKdf::Argon2id {
            salt: vec![0],
            params: "legacy-import-v1".to_string(),
        },
        password_verifier: Vec::new(),
    }])
}
```

Create `crates/tundra-legacy/src/tux.rs`:

```rust
use std::fs;
use std::path::{Path, PathBuf};

use tundra_core::{FileEntry, FileKind, VaultPath};

use crate::LegacyError;

pub fn read_legacy_tux_files(root: &Path) -> Result<Vec<FileEntry>, LegacyError> {
    let files_root = root.join("Files");
    if !files_root.exists() {
        return Ok(Vec::new());
    }
    let mut out = Vec::new();
    collect_tux(&files_root, &files_root, &mut out)?;
    Ok(out)
}

fn collect_tux(base: &Path, dir: &Path, out: &mut Vec<FileEntry>) -> Result<(), LegacyError> {
    for entry in fs::read_dir(dir).map_err(|_| LegacyError::ReadFailed)? {
        let entry = entry.map_err(|_| LegacyError::ReadFailed)?;
        let path = entry.path();
        if path.is_dir() {
            collect_tux(base, &path, out)?;
        } else if path
            .extension()
            .and_then(|value| value.to_str())
            .is_some_and(|value| value.eq_ignore_ascii_case("TUX"))
        {
            out.push(file_entry_from_path(base, path)?);
        }
    }
    Ok(())
}

fn file_entry_from_path(base: &Path, path: PathBuf) -> Result<FileEntry, LegacyError> {
    let relative = path.strip_prefix(base).map_err(|_| LegacyError::ReadFailed)?;
    let mut vault = relative.to_string_lossy().replace('\\', "/");
    if let Some(stripped) = vault.strip_suffix(".TUX") {
        vault = stripped.to_string();
    }
    let content = fs::read(&path).map_err(|_| LegacyError::ReadFailed)?;
    Ok(FileEntry {
        path: VaultPath::parse(&format!("imported/legacy/{vault}")).map_err(|_| LegacyError::ReadFailed)?,
        kind: FileKind::File,
        owner: "legacy-import".to_string(),
        created_unix: 0,
        modified_unix: 0,
        content,
    })
}
```

Create `crates/tundra-legacy/src/tlog.rs`:

```rust
use std::fs;
use std::path::Path;

use tundra_core::{AuditEvent, AuditKind};

use crate::LegacyError;

pub fn read_legacy_tlogs(root: &Path) -> Result<Vec<AuditEvent>, LegacyError> {
    let logs_root = root.join("Logs");
    if !logs_root.exists() {
        return Ok(Vec::new());
    }
    let mut events = Vec::new();
    for entry in fs::read_dir(logs_root).map_err(|_| LegacyError::ReadFailed)? {
        let entry = entry.map_err(|_| LegacyError::ReadFailed)?;
        let path = entry.path();
        if path
            .extension()
            .and_then(|value| value.to_str())
            .is_some_and(|value| value.eq_ignore_ascii_case("tlog"))
        {
            events.push(AuditEvent {
                timestamp_unix: 0,
                actor: "legacy-import".to_string(),
                kind: AuditKind::LegacyImport,
                message: format!("imported legacy log {}", path.display()),
            });
        }
    }
    Ok(events)
}
```

- [ ] **Step 4: Wire CLI `legacy import`**

In `crates/tundra-cli/src/commands.rs`, replace the `LegacyCommand::Import` arm with:

```rust
LegacyCommand::Import {
    from,
    data_dir,
    password,
} => {
    let mut users: UserStore = load_store(&data_dir.join("users.tdb"), ContainerType::Users, password.as_bytes())?;
    let mut files: FileStore = load_store(&data_dir.join("files.tdb"), ContainerType::Files, password.as_bytes())?;
    let mut audit: AuditStore = load_store(&data_dir.join("audit.tdb"), ContainerType::Audit, password.as_bytes())?;
    let mut meta: MetaStore = load_store(&data_dir.join("meta.tdb"), ContainerType::Meta, password.as_bytes())?;

    let imported_users = tundra_legacy::read_legacy_users(&from)?;
    let imported_files = tundra_legacy::read_legacy_tux_files(&from)?;
    let imported_events = tundra_legacy::read_legacy_tlogs(&from)?;

    let batch = format!("legacy:{}", from.display());
    meta.imported_batches.push(batch.clone());
    users.users.extend(imported_users);
    files.files.extend(imported_files);
    audit.events.extend(imported_events);
    audit.events.push(tundra_core::AuditEvent {
        timestamp_unix: 0,
        actor: "admin".to_string(),
        kind: tundra_core::AuditKind::LegacyImport,
        message: batch.clone(),
    });

    save_store(&data_dir.join("users.tdb"), ContainerType::Users, password.as_bytes(), &users)?;
    save_store(&data_dir.join("files.tdb"), ContainerType::Files, password.as_bytes(), &files)?;
    save_store(&data_dir.join("audit.tdb"), ContainerType::Audit, password.as_bytes(), &audit)?;
    save_store(&data_dir.join("meta.tdb"), ContainerType::Meta, password.as_bytes(), &meta)?;

    println!("imported legacy batch {batch}");
    Ok(())
}
```

- [ ] **Step 5: Run import tests**

Run:

```powershell
cargo test -p tundraux legacy_import_records_batch
cargo test
```

Expected: import smoke test and all workspace tests pass.

- [ ] **Step 6: Commit minimal legacy import**

Run:

```powershell
git add crates/tundra-legacy crates/tundra-cli
git commit -m "feat: add minimal legacy import"
```

## Task 9: Add Documentation for Rust Migration Status

**Files:**
- Modify: `README.md`
- Modify: `README.zh-CN.md`

- [ ] **Step 1: Add English README migration section**

In `README.md`, after the existing build section, add:

```markdown
## Rust Migration Status

The project is migrating to a Windows-first Rust implementation. The first Rust
stage focuses on encrypted `.tdb` data files, explicit legacy import from the
old C++ data formats, and a CLI command layer that can later be reused by a full
TUI.

Current migration target:

```powershell
cargo test
cargo run -p tundraux -- init --admin admin --password secret123
cargo run -p tundraux -- legacy scan --from .
```

The legacy C++ formats are read only for import. New runtime data belongs in the
Rust `.tdb` containers.
```

- [ ] **Step 2: Add Chinese README migration section**

In `README.zh-CN.md`, after the existing build section, add:

```markdown
## Rust 迁移状态

项目正在迁移到仅优先支持 Windows 的 Rust 实现。第一阶段重点是加密
`.tdb` 数据文件、显式导入旧 C++ 数据格式，以及后续完整 TUI 可以复用的
CLI 命令层。

当前迁移目标：

```powershell
cargo test
cargo run -p tundraux -- init --admin admin --password secret123
cargo run -p tundraux -- legacy scan --from .
```

旧 C++ 格式只用于读取和导入。新的运行时数据应写入 Rust `.tdb` 容器。
```

- [ ] **Step 3: Run documentation-adjacent verification**

Run:

```powershell
cargo test
git diff --check
```

Expected: all Rust tests pass and `git diff --check` reports no whitespace errors.

- [ ] **Step 4: Commit documentation**

Run:

```powershell
git add README.md README.zh-CN.md
git commit -m "docs: describe rust migration status"
```

## Task 10: Final Verification

**Files:**
- No file changes expected.

- [ ] **Step 1: Run full Rust test suite**

Run:

```powershell
cargo test
```

Expected: all workspace tests pass.

- [ ] **Step 2: Run CLI manual smoke commands**

Run:

```powershell
$tmp = Join-Path $env:TEMP ("tundraux-rust-" + [guid]::NewGuid())
$old = Join-Path $tmp "old"
$data = Join-Path $tmp "data"
New-Item -ItemType Directory -Force -Path (Join-Path $old "Files") | Out-Null
Set-Content -LiteralPath (Join-Path $old "user_data.dat") -Value "legacy users"
Set-Content -LiteralPath (Join-Path $old "Files\note.TUX") -Value "legacy tux"
cargo run -p tundraux -- init --data-dir $data --admin admin --password secret123
cargo run -p tundraux -- user list --data-dir $data --password secret123
cargo run -p tundraux -- legacy scan --from $old
cargo run -p tundraux -- legacy import --from $old --data-dir $data --password secret123
```

Expected output includes:

```text
initialized
admin
user_data: true
tux_files: 1
imported legacy batch
```

- [ ] **Step 3: Confirm C++ build is not part of Rust verification**

Run:

```powershell
git status --short
```

Expected: no uncommitted changes. Do not run CMake as part of this Rust migration verification unless a separate compatibility check is requested.

## Self-Review Notes

- Spec coverage: workspace architecture, encrypted data containers, explicit legacy scan/import, new command model, test coverage, and deferred TUI/editor scope are covered.
- Deferred work from the spec is not implemented in this plan: full ratatui UI, full Rust editor, backup/restore, secure delete, and detailed future `.tdb` migrations remain outside stage one.
- Type consistency: `Role`, `AccountState`, `UserRecord`, `VaultPath`, `FileEntry`, `AuditEvent`, `ContainerType`, `UserStore`, `FileStore`, `AuditStore`, and `MetaStore` are introduced before downstream tasks use them.
