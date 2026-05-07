# TUI 颜色开发要求

本文档约束 TundraUX 2.0 中所有控制台 TUI 的颜色用法。新增或修改 TUI 时必须按语义选色，不允许在业务代码里随意新增 ANSI 颜色串。

## 颜色来源

- 旧版共用颜色入口：`SYSTEM/console/color.hpp`、`SYSTEM/console/color.cpp`
  - 只提供 `colorcout(color, text)`、`rollcout(color, text)` 这类命名颜色输出。
  - 适合 Shell、启动流程、调试命令、普通提示语等线性控制台输出。
- Explorer TUI 样式入口：`APP/explorer/explorer_style.hpp`
  - 提供 `kTitleStyle`、`kWarningStyle`、`colorText()`、`colorCellPart()` 等语义化样式。
  - 新增 Explorer 相关 TUI 代码必须使用该文件，不得重复定义同一套 ANSI 常量。
- User Management TUI 当前在 `USER/account/manageusers.cpp` 内部重复定义了同一套 TUI 样式常量。这是开发遗漏导致的临时状态，后续应迁移为共用 TUI 样式文件。

## 通用命名颜色要求

以下颜色名称来自 `SYSTEM/console/color.cpp`，只能按语义使用：

| 颜色名 | ANSI 颜色 | 使用位置 |
| --- | --- | --- |
| `red` / `RED` / `ERROR` | bright red, 203 | 错误、危险操作、拒绝访问、损坏文件、删除确认等强警告；`colorcout()` 输出红色时会响铃 |
| `green` / `GREEN` | bright green, 119 | 成功、完成、保存、创建、复制完成、状态恢复正常 |
| `yellow` / `YELLOW` | bright yellow, 220 | 用户输入提示、下一步说明、可恢复警告、未知命令建议 |
| `blue` / `BLUE` | blue, 39 | 信息性文本、非关键说明 |
| `magenta` / `MAGENTA` | bright magenta, 213 | Shell 提示符、调试模式提示、强调性标签 |
| `cyan` / `CYAN` | bright cyan, 51 | 程序标题、页面标题、主分隔标题 |
| `white` / `WHITE` | white, 254 | 普通正文、默认输出 |
| `gray` / `grey` / `GRAY` / `GREY` | gray, 245 | 次要提示、禁用感文本、辅助说明 |

## TUI 语义颜色要求

复杂 TUI 页面必须使用语义化样式常量，而不是直接调用 `colorcout()` 拼接 UI：

| 语义 | 样式常量 | ANSI 颜色 | 使用位置 |
| --- | --- | --- | --- |
| 页面标题 | `kTitleStyle` | bold cyan, 51 | TUI 顶部标题，例如 Explorer、User Management、Help、Properties |
| 权限/角色 | `kRoleStyle` | bold magenta, 213 | `admin`、`debug`、角色标签、特殊权限标识 |
| 当前用户 | `kUserStyle` | bold yellow, 220 | 当前用户名、被操作用户名 |
| 路径/资源定位 | `kPathStyle` | light blue, 117 | 当前路径、数据文件名、选中文件名 |
| 边框 | `kBorderStyle` | blue, 39 | 表格边框、面板边框、列分隔符 |
| 表头 | `kHeaderStyle` | fg 195 + bg 24, bold | 表格列头、详情区块标题 |
| 区块标题 | `kSectionStyle` | bold cyan-green, 87 | Help 分组、状态标签、输入标签 |
| 快捷键 | `kKeyStyle` | bold yellow, 220 | `Enter`、`Esc`、`h`、`q` 等按键提示 |
| 次要提示 | `kHintStyle` | gray, 245 | 分隔符、辅助说明、空值、取消提示 |
| 帮助正文 | `kHelpTextStyle` | light gray, 252 | Help 描述、详情正文、普通 TUI 文本 |
| 目录 | `kDirStyle` | bold cyan-green, 87 | 目录名、`[D]` 标记、`<DIR>` |
| 普通文件 | `kFileStyle` | white, 254 | 未分类文件、单元格填充 |
| 文本文档 | `kTextFileStyle` | pale yellow, 229 | `.txt`、`.md` 文件 |
| TUX 文件 | `kTuxFileStyle` | bold magenta, 213 | `.tux` 文件、TUX 特殊提示 |
| 数据文件 | `kDataFileStyle` | red, 203 | `.dat` 文件、用户数据文件 |
| 隐藏项 | `kHiddenStyle` | dark gray, 244 | 隐藏文件、隐藏目录、隐藏标记 |
| 文件大小 | `kSizeStyle` | gray, 250 | 文件大小、容量信息 |
| 复制状态 | `kCopyStyle` | bright green, 119 | 已复制项、成功状态、可执行状态 |
| 剪切状态 | `kCutStyle` | gray, 246 | 已剪切项、待移动项 |
| 选中背景 | `kSelectedBgStyle` | bg 24 | 当前行、当前单元格、当前表单字段背景 |
| 选中标记 | `kSelectedMarkStyle` | bold pale yellow, 229 | `>`、`->`、当前选中指示符 |
| 输入内容 | `kInputStyle` | bold pale yellow, 230 | 表单输入值、正在输入的新文件夹名 |
| 警告/错误 | `kWarningStyle` | bold red, 203 | 权限拒绝、删除确认、错误状态、锁定账号 |

## 开发规则

1. 新增 TUI 页面时，先复用现有语义样式；确实缺少语义时，再在对应样式文件中增加新常量。
2. 不允许在普通业务逻辑里直接写 `"\x1b["` 或 `"\033["`。ANSI 串只能出现在颜色/样式集中定义文件里。
3. 列表、表格、详情页、帮助页必须用 `colorText()` 或 `colorCellPart()` 这类统一封装，保证选中背景和 reset 行为一致。
4. 错误和危险操作必须使用红色；成功状态必须使用绿色；输入提示和快捷键必须使用黄色；路径必须使用浅蓝色。
5. `.tux`、`.dat`、目录、隐藏文件必须按文件类型着色，不能只用普通白色。
6. 任何新文件如果需要 TUI 样式，不应再新增自己的局部颜色表；应迁移或复用共用 TUI 样式入口。

## 因开发遗漏仍使用共用 color 文件的文件

以下业务文件当前直接包含 `SYSTEM/console/color.hpp`。其中一部分是线性控制台输出可以继续保留；涉及复杂 TUI 的文件应逐步迁移到语义化 TUI 样式文件。

| 文件 | 当前用途 | 后续要求 |
| --- | --- | --- |
| `APP/file_manager/TUXfile.cpp` | TUX File Manager 命令输出使用 `colorcout()` | 若改为全屏 TUI，需要迁移到语义化 TUI 样式 |
| `APP/shell/command.cpp` | Shell 提示符、未知命令、CMD 执行状态 | 可保留共用颜色；Shell 提示符继续使用 magenta |
| `APP/shell/commandHandlers.cpp` | Shell 命令处理输出 | 可保留共用颜色；新增复杂界面时改用 TUI 样式 |
| `APP/shell/commandReg.cpp` | 命令注册/帮助输出 | 可保留共用颜色；帮助表格化时改用 TUI 样式 |
| `CORE/main/main.cpp` | 主入口错误或启动输出 | 可保留共用颜色 |
| `CORE/startup/hello.cpp` | 启动、登录、初始账号流程输出 | 可保留共用颜色；表单化后迁移 |
| `SYSTEM/debug/debug.cpp` | 调试命令输出和颜色测试 | 可保留共用颜色 |
| `USER/account/manageusers.cpp` | User Management 全屏 TUI，且重复定义了 Explorer 类似样式 | 开发遗漏；应拆出/复用共用 TUI 样式文件，避免继续复制颜色常量 |
| `USER/udata/udata.cpp` | 用户数据导入、读写错误输出 | 可保留共用颜色 |

`SYSTEM/console/color.cpp` 自身包含 `color.hpp` 是实现关系，不属于遗漏。

## 已有专用 TUI 样式文件

以下 Explorer 文件已经通过 `APP/explorer/explorer_style.hpp` 使用语义化样式，新增 Explorer 代码应保持这个模式：

- `APP/explorer/explorer_actions.cpp`
- `APP/explorer/explorer_clipboard.cpp`
- `APP/explorer/explorer_permissions.cpp`
- `APP/explorer/explorer_render.cpp`
