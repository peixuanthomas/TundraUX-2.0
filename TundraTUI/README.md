# TundraTUI

TundraTUI is the reusable terminal UI boundary for TundraUX. It collects console
screen handling, semantic ANSI styles, key input, simple text/layout helpers, and
a small render engine behind one CMake target.

This module is intentionally kept independent from TundraUX application logic so
it can later move to its own repository and be consumed by other projects.

## Current Scope

- `TundraTUI::TundraTUI` CMake target
- `ConsoleScreenGuard` and `clearConsoleScreen()`
- semantic style constants such as `kTitleStyle`, `kWarningStyle`, `kPathStyle`
- `colorText()` and `colorCellPart()` helpers
- `Key` / `KeyPress` and `readKey()`
- `RenderEngine` for cursor, clear, write, and flush operations
- text helpers such as `fitText()`, `trimToWidth()`, and simple borders
- compatibility shims for the old TundraUX headers

## Build From TundraUX

From the TundraUX repository root on Windows:

```powershell
cmake -B build
cmake --build build
```

The top-level build enables the TundraTUI examples by default while the module
option remains configurable:

```cmake
set(TUNDRATUI_BUILD_EXAMPLES ON)
add_subdirectory(TundraTUI)
target_link_libraries(my_app PRIVATE TundraTUI::TundraTUI)
```

## Standalone Module Build

TundraTUI can also be configured directly, which is the path to use when testing
the framework without the Windows-only TundraUX app:

```powershell
cmake -S TundraTUI -B build-tundratui -DTUNDRATUI_BUILD_EXAMPLES=ON
cmake --build build-tundratui
```

On POSIX terminals, `readKey()` enters raw mode for one key read and decodes
common ANSI escape sequences. `getHiddenInput()` disables echo while reading.

## Public Headers

Use the aggregate header for small applications:

```cpp
#include "TundraTUI/TundraTUI.hpp"
```

Use focused headers when integrating a larger application:

```cpp
#include "TundraTUI/screen.hpp"
#include "TundraTUI/style.hpp"
#include "TundraTUI/input.hpp"
#include "TundraTUI/render_engine.hpp"
#include "TundraTUI/text.hpp"
```

## Examples

`examples/style_demo.cpp` prints the semantic color palette and text helpers.

`examples/render_demo.cpp` enters the alternate screen, renders a small frame,
waits for one key, and restores the previous screen.

After building, run the generated binaries from the build directory. The exact
path depends on the selected CMake generator.

## Migration Notes

Existing TundraUX code can still include the legacy headers:

- `SYSTEM/console/color.hpp`
- `SYSTEM/console/console_screen.hpp`
- `APP/explorer/explorer_style.hpp`

Those headers now forward to TundraTUI. New code should include `TundraTUI/...`
directly.

The current migration keeps business-specific rendering in TundraUX. Explorer,
account settings, user management, and editor screens should move incrementally:
first use TundraTUI primitives, then extract reusable widgets only when repeated
behavior is clear.
