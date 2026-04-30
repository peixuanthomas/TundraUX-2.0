# TundraUX 2.0

TundraUX 2.0 是一个仅支持 Windows 的 C++ 控制台应用，包含交互式 Shell、用户账户管理，以及基于 `.TUX` 格式的加密文件管理器。

项目使用 CMake 和 C++17 构建，并依赖 Windows 控制台 API 来实现清屏、彩色输出、隐藏密码输入、命令历史和内置文本编辑器等功能。

## 功能特性

- 交互式 Shell，支持命令历史和相似命令提示
- 用户登录、登出、密码修改和失败次数锁定
- 管理员用户管理界面，可列出、新增、修改和删除用户
- TUX File Manager，用于管理 `Files` 目录下的加密 `.TUX` 文件
- 文件操作：创建、查看、编辑、删除、重命名、复制、移动、搜索和目录管理
- 文件元数据：创建者、最后编辑者、创建时间和修改时间
- `.TUX` 与带元数据的 `.txt` 文件相互导入导出
- 内置文本编辑器，支持 Windows 后端和 portable 后端
- 主 Shell 中可用 `/` 前缀直接执行 Windows CMD 命令

## 环境要求

- Windows
- CMake 3.15 或更高版本
- 支持 C++17 的编译器，例如 MSVC 或 MinGW-w64

由于当前代码使用 Windows 专用控制台 API，项目会在非 Windows 系统上停止 CMake 配置。

## 构建

```powershell
cmake -B build
cmake --build build
```

生成的可执行文件名为 `TundraUX2`。

### 启动模式

默认情况下，CMake 选项 `TUNDRAUX_DEBUG_STARTUP` 是开启的。程序会以内置 `debug` 用户进入 Shell，方便开发和调试。

如果需要以普通访客模式启动：

```powershell
cmake -B build -DTUNDRAUX_DEBUG_STARTUP=OFF
cmake --build build
```

## 首次运行

程序启动时会检查 `user_data.dat`。

- 如果 `user_data.dat` 不存在且 `license` 文件存在，程序会显示许可证，按 Enter 后继续。
- 初始化后进入主 Shell。
- 用户数据保存在 `user_data.dat`。
- TUX 文件保存在 `Files` 目录。

## 主 Shell

主 Shell 的提示符会根据当前会话变化：

- `GUEST>>`：访客模式
- `<username>>`：已登录用户
- `DEBUG MODE ACTIVE>>`：调试模式

### 主命令

| 命令 | 说明 |
| --- | --- |
| `help`, `?` | 显示当前用户可用命令 |
| `login <username>` | 登录指定用户 |
| `logout` | 登出当前用户 |
| `modify` | 修改当前用户的密码或密码提示 |
| `listuser` | 列出已注册用户 |
| `manageuser` | 打开用户管理界面，仅 admin/debug 可用 |
| `TUXfile` | 打开 TUX File Manager，仅 user/admin/debug 可用 |
| `edit [filename]` | 打开 `Files` 目录下普通文件的文本编辑器 |
| `importdata` | 导入旧版本用户数据，仅 admin/debug 可用 |
| `time` | 显示本地时间和 Unix 时间戳 |
| `license` | 显示许可证文本 |
| `displaytest` | 运行控制台显示测试 |
| `info` | 显示构建信息 |
| `cls` | 清屏 |
| `exit` | 退出程序 |
| `/<cmd>` | 执行 Windows CMD 命令 |

调试命令不会出现在普通帮助中，包括编辑器后端检查、强制登录和诊断工具等。

## 用户角色

| 角色 | 权限 |
| --- | --- |
| `guest` | 可以登录并使用公开 Shell 命令 |
| `user` | 可以使用编辑器和 TUX File Manager |
| `admin` | 可以管理用户，并使用 TUX 的导入、导出和元数据命令 |
| `debug` | 拥有开发调试用的无限制访问权限 |

每个用户都会记录登录失败次数。失败次数超过 7 次后，账户会被禁用，需要 admin 或 debug 用户在用户管理界面中重置 `count`。

通过 `modify` 修改密码时，密码需要满足：

- 至少 6 个字符
- 至少包含一个大写字母
- 至少包含一个小写字母
- 至少包含一个数字
- 密码提示不能与密码完全相同

## 用户管理

在主 Shell 中输入：

```text
manageuser
```

可用命令：

| 命令 | 说明 |
| --- | --- |
| `help`, `h`, `?` | 显示帮助 |
| `list`, `ls`, `l` | 列出用户 |
| `show <username>` | 显示用户详情 |
| `add <username> type=<admin|user> password=<password> [hint=<hint>] [count=<n>]` | 新增用户 |
| `set <username> field=value...` | 修改 `type`、`password`、`hint` 或 `count` |
| `delete <username>` | 删除用户 |
| `exit`, `quit`, `q` | 返回主 Shell |

## TUX File Manager

在主 Shell 中输入：

```text
TUXfile
```

文件会保存在 `Files` 目录下。文件名和路径组件可以包含字母、数字、连字符和下划线。使用 `/` 表示子目录，例如：

```text
touch docs/notes
edit docs/notes
```

### 文件命令

| 命令 | 说明 |
| --- | --- |
| `help`, `h`, `?` | 显示 TUX File Manager 帮助 |
| `ls`, `list`, `ll` | 列出文件和目录 |
| `create`, `touch`, `new`, `c <file>` | 创建空 `.TUX` 文件 |
| `edit`, `open`, `e <file>` | 编辑 `.TUX` 文件 |
| `view`, `cat`, `read`, `v <file>` | 查看文件内容 |
| `delete`, `remove`, `rm`, `del`, `d <file>` | 删除文件 |
| `rename`, `rn <old> <new>` | 重命名文件 |
| `cp`, `copy <src> <dst>` | 复制文件 |
| `cp <file1> [file2..] <dir>` | 将多个文件复制到已有目录 |
| `mv`, `move <src> <dst>` | 移动或重命名文件 |
| `mv <file1> [file2..] <dir>` | 将多个文件移动到已有目录 |
| `find`, `search <pattern>` | 按文件名搜索 |
| `mkdir`, `md <dir>` | 创建目录 |
| `rmdir`, `rd <dir>` | 删除目录 |
| `quit`, `q`, `exit` | 返回主 Shell |

### TUX 特权命令

以下命令需要 `admin` 或 `debug` 权限。

| 命令 | 说明 |
| --- | --- |
| `metadata`, `meta`, `m`, `info <file>` | 查看文件元数据 |
| `export`, `ex <file>` | 将 `.TUX` 文件导出为 `.txt` |
| `import`, `im <file>` | 将 `.txt` 文件导入为 `.TUX` |

## 编辑器

编辑器可以从主 Shell 打开普通文件，也可以从 TUX File Manager 打开 `.TUX` 文件。

| 按键 | 操作 |
| --- | --- |
| 方向键 | 移动光标 |
| Enter | 插入换行 |
| Backspace | 删除字符或合并行 |
| 普通字符键 | 输入文本 |
| Tab | 进入编辑器命令模式 |

编辑器命令模式：

| 命令 | 操作 |
| --- | --- |
| `/s` | 保存并退出 |
| `/q` | 放弃修改并退出 |

## `.TUX` 文件格式

当前格式版本为 `1`。

```text
[Version: unsigned int]
[Creator length: size_t][Encrypted creator]
[Last editor length: size_t][Encrypted last editor]
[Create time: time_t]
[Modify time: time_t]
[Content length: size_t][Encrypted content]
```

实现限制：

- 元数据字符串最大长度：1024 字节
- 文件内容最大长度：16 MiB
- 命令历史最大长度：100 条

## 安全说明

当前加密方式是简单 XOR 变换，适合演示文件格式和读写流程，但不具备真正的密码学安全性。不要在未替换加密实现的情况下，用本项目保护敏感数据。

如果要使用真实加密方案，应替换 `crypto.cpp` 中的实现，并考虑已有 `.TUX` 文件的迁移兼容问题。

## 项目结构

| 路径 | 作用 |
| --- | --- |
| `CMakeLists.txt` | 构建配置 |
| `inc/` | 头文件 |
| `src/main.cpp` | 启动流程、许可证检查、Shell 入口 |
| `src/command.cpp` | 主 Shell 循环、命令历史、相似命令提示、CMD 转发 |
| `src/commandRegistry.cpp` | 主 Shell 命令表 |
| `src/commandHandlers.cpp` | 主 Shell 命令处理函数 |
| `src/TUXfile.cpp` | TUX File Manager 和 `.TUX` 文件读写 |
| `src/editor*.cpp` | 编辑器前端和后端选择 |
| `src/manageusers.cpp` | 用户管理界面 |
| `src/udata.cpp` | 用户数据持久化 |
| `src/crypto.cpp` | XOR 加密辅助函数 |
| `src/color.cpp` | 彩色控制台输出 |
| `src/console_screen.cpp` | 控制台屏幕保护工具 |

## 许可证

本项目使用 MIT License。详情见 `license` 文件。
