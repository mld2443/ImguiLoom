# Loom

A node editor for Dear ImGui.

## Design goals

- **Lightweight API:** The custom-node API is the project's make-or-break requirement: defining custom nodes must be simple, expressive, and versatile.
- **Small implementation:** The library should be only as large as it needs to be, and introduce as little latency as possible.
- **Feature complete:** Conventional node-graph editing, a zoomable canvas, and support for as many Dear ImGui widgets as possible, as detailed below.

## Feature-complete scope

Loom is under development. This is the intended scope, including capabilities
that are not implemented yet.

- **Custom nodes:** A small application-defined node interface, programmatic
  creation, and an application-supplied Add catalog.
- **Canvas and selection:** Panning, pointer-anchored zoom, a grid, click and
  Ctrl-click selection, box selection, group dragging, and correct overlap ordering.
- **Widgets:** Text editing, child windows, scrolling, popups, modals, and color
  pickers, with focus and clipboard support. Popups remain anchored while the
  canvas pans and zooms.
- **Context menus:** Creation, deletion, renaming, group styling, resetting style
  overrides, and zoom controls through native Dear ImGui menus.
- **Undo/redo:** Node creation, deletion, movement, renaming, and styling, with
  exclusive ownership preserved across history operations.
- **Component styling:** Canvas, node, pin, and link appearance, with editable
  defaults per component type, overrides, and reusable style-editing behavior.
- **Type colors:** Redefinable colors for standard types, a common fallback for
  other types, and per-type overrides. Pins of the same type use consistent colors
  throughout a canvas; link colors follow their endpoint types.
- **Inputs:** Editable local values that connections can override, optional inputs
  that preserve meaningful absence, and multi-input pins with a distinct visual
  representation. Optional multi-inputs allow zero contributions; required ones
  need at least one. Exact optional-pin symbols remain to be decided.
- **Link editing:** Reconnect either endpoint, detach or delete connections, insert
  reroutes, replace an occupied single input predictably, and connect an output to
  multiple inputs.
- **Visible conversions:** Indicate when a connection converts between types.
  Gradients or other decorators remain a presentation choice.
- **Node bypass:** Defined input-to-output pass-through routes, with muted link
  decoration when bypassing leaves a connection without a contribution. Inputs
  with local/default values fall back to them when no effective connection remains.
- **Reroutes and collapsing:** Routing points for organizing links, and collapsed
  nodes that retain separately identifiable sockets around their edges.
- **Validation:** Cycle/loop detection and feedback for incompatible connections
  and missing required values. Bypass routing must be defined when several inputs
  have compatible types.

Field/dynamic evaluation and independent link-muting controls are outside this
scope. Muted link appearance follows node bypass behavior.

## Future work

- Continuous panning with cursor wrapping and a four-arrow cursor, subject to
  platform/backend support.
- Node duplication and application-defined resetting of node contents.
- Save/load and copy/paste, preserving application data, editor state, identity,
  and connections.
- Undo/redo for connections, reroutes, and bypass changes.
- Frame all, frame selection, and searchable node creation.
- Labeled frames/groups and comments for visual organization.
- Dear ImGui docking-branch compatibility, nested canvases, and cross-context
  drag-and-drop.
- Reflection-assisted style editing when compiler support makes it practical.

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

Pins and connections are not implemented yet.
