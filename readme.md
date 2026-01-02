# TundraUX 2.0

A secure, modular file management system with encrypted storage, multi-user access control, and an advanced console-based editor.

## Overview

TundraUX 2.0 is a complete rewrite of TundraUX 1.0, featuring a refactored architecture with separated concerns, enhanced security, and a sophisticated inline text editor. It provides encrypted binary file storage (.TUX format) with comprehensive metadata tracking and permission-based access control.

## Key Improvements from 1.0

### Architecture
- **Modularized Design**: Separated functionality into distinct modules for maintainability
- **Better Code Organization**: Clear separation of concerns with dedicated utility functions
- **Improved Error Handling**: Comprehensive validation and error checking throughout

### Editor & UI
- **Advanced Inline Editor**: Multi-line text editing with real-time rendering
- **Text Wrapping**: Automatic line wrapping that adapts to console width
- **Integrated Command Mode**: Tab-accessible command interface within editor
- **Console Resizing Support**: Editor dynamically adapts to terminal size changes
- **Command History**: Navigation through previously entered commands with arrow keys
- **Colored Output**: Enhanced user experience with syntax-highlighted messages

### File Management
- **Robust I/O**: Improved read/write operations with extensive validation
- **Atomic Saves**: File write operations that prevent data corruption
- **Filename Validation**: Strict validation rules for safe file naming
- **Import/Export**: Seamless conversion between encrypted (.TUX) and plain text (.txt) formats

### Security & Access Control
- **Multi-user Support**: Track file creators and editors with timestamp metadata
- **Permission System**: Role-based access control (admin, debug, regular users)
- **Creator-based Access**: Only file creators or privileged users can edit files
- **Encrypted Storage**: XOR-based encryption for all sensitive data

## Features

### File Operations
- **Create**: Initialize new encrypted files with metadata
- **View**: Display file contents with safety checks
- **Edit**: Advanced inline editor with full text manipulation
- **Delete**: Safe file deletion with confirmation
- **Rename**: Rename files with validation
- **List**: Tree-view display of files and directories
- **Export**: Convert .TUX files to plain text .txt format (privileged)
- **Import**: Convert .txt files to encrypted .TUX format (privileged)
- **Metadata**: View file creation/modification history (privileged)

### Editor Features
- Full multi-line text editing
- Dynamic text wrapping based on console width
- Arrow key navigation (left, right, up, down)
- Line break insertion and deletion
- Character insertion and removal
- Command mode for save/quit operations
- Real-time cursor positioning
- Automatic line clearing to prevent artifacts

### Access Control
| User Type | Permissions |
|-----------|-------------|
| **Regular User** | Create, view, edit own files, delete own files |
| **Admin User** | All operations including export, import, metadata |
| **Debug User** | All operations |

## Command Reference

### Basic Commands
```
ls, list              List all files and directories
c, create <name>      Create new file
v, view <name>        View file contents
e, edit <name>        Edit file in inline editor
d, delete <name>      Delete file (with confirmation)
rn, rename <old> <new> Rename file
h, help               Display help menu
q, quit               Exit program
```

### Privileged Commands (Admin/Debug only)
```
ex, export <name>     Export to .txt format
im, import <name>     Import from .txt format
m, metadata <name>    View file metadata
```

## Editor Controls

| Control | Action |
|---------|--------|
| **Arrow Keys** | Move cursor (left, right, up, down) |
| **Enter** | Insert line break at cursor |
| **Backspace** | Delete character or merge lines |
| **Character Keys** | Insert character at cursor position |
| **Tab** | Enter command mode |
| **ESC** | Exit command mode |

### Command Mode
Activated by pressing **Tab** while editing:
- `/s` - Save file and exit editor
- `/q` - Discard changes and exit editor

## Technical Specification

### File Format (.TUX)

Binary format with XOR encryption:

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
- **Max String Length**: 1024 bytes
- **Max Content Size**: 16 MB per file
- **Command History**: 100 entries

### Filename Rules
- Allowed characters: A-Z, a-z, 0-9, hyphens (-), underscores (_)
- Must not be empty
- Case-sensitive

### Timestamps
- Stored as Unix timestamps (seconds since epoch)
- Local time display format: `YYYY-MM-DD HH:MM:SS`
- Automatically updated on file modification

## Requirements

### System
- **OS**: Windows (uses Windows Console API)
- **Compiler**: C++17 or later
- **Libraries**: Standard C++ library with filesystem support

### Dependencies
- `<filesystem>` - File operations
- `<chrono>` - Time handling
- `<iostream>` - Console I/O
- `<windows.h>` - Console API (Windows-specific)

## Building

Use Cmake to build the project

## IMPORTANT

Current encryption and decryption use a simple fixed key for pseudo-random number generation. This is NOT secure for production use. Please consider replacung it with better alogrithms. Add your own methods in crypto.cpp replacing the encrypt and decrypt function. Be aware that changing encryption methods will break compatibility with existing files.

## Version History

### 2.0 (Current)
- Complete architecture redesign
- Modularized codebase
- Advanced inline editor with dynamic wrapping
- Command history system
- Multi-user access control
- Comprehensive error handling
- Atomic file operations
- Colored output system

### 1.0 (Legacy)
- Basic file management
- Simple text editor
- Single-user support

## Future Enhancements

Potential improvements for future versions:
- [ ] File compression
- [ ] Undo/redo in editor
- [ ] Search and replace functionality
- [ ] File permissions matrix
- [ ] Backup system
- [ ] File versioning
- [ ] Multi-level folder structure

## Changelog

See commit history for detailed changes.