# Loom

A node editor for Dear ImGui.

## Design goals

- **Lightweight API:** Simple yet versatile, custom nodes that should be easy to make.
- **Small implementation:** The library should be only as large as it needs to be, and introduce as little latency as possible.
- **Feature complete:** Selection, a zoomable canvas, loop detection, and support for as many Dear ImGui widgets as possible.

## Building

Requires C++23 with module support and CMake 3.30 or newer.

```cmake
add_subdirectory(external/ImguiLoom)
target_link_libraries(MyApplication PRIVATE loom)
```

Loom reuses an existing `imgui` target, or fetches and builds Dear ImGui 1.93.0
at commit `c1aaac65e896a45f81f47a4d68f9959e0f86c859`. To supply another target,
define it first and select it before adding Loom:

```cmake
set(LOOM_IMGUI_TARGET MyImGuiTarget)
add_subdirectory(external/ImguiLoom)
target_link_libraries(MyApplication PRIVATE loom)
```

The supplied target must expose its ImGui headers and compile configuration.
Version 1.93.0 is the tested baseline; other revisions must provide compatible
public and internal APIs. The application supplies its platform/rendering backends
and runs the outer ImGui frame. The fallback builds only ImGui's four core sources.
Loom includes native ImGui headers and does not require an ImGui module wrapper or
the standard-library module.

## Canvas

```cpp
#include <imgui.h>
import loom;

struct Number : loom::Node {
    explicit Number(float value) : Node("Number"), value(value) {}
    void draw() override {
        ImGui::SetNextItemWidth(160);
        ImGui::DragFloat("Value", &value);
    }
    float value;
};

// After creating the application's ImGui context:
loom::Canvas canvas;
canvas.add<Number>({40, 40}, 1.0f);

// Each frame, inside an ImGui window:
canvas.draw();
```

The canvas owns its nodes and fills the remaining content region. Node `draw()`
methods run in a separate context sharing the host's font atlas; widget dimensions
are canvas units. Each node must balance its own ImGui operations. Destroy the
canvas before the host context. Canvases cannot be copied or moved.

Wheel over the background to zoom around the pointer; middle-drag the background
to pan, including while a popup or modal is open. Widgets retain scrolling and
mouse gestures. Popups follow their canvas anchors and scale with the view;
placement remains bounded by the canvas. The grid has fixed spacing in canvas units.

Connections, nested canvases, and cross-context drag-and-drop remain future work.
