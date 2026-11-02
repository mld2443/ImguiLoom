module;

#include <algorithm>   // any_of, clamp, contains, find_if
#include <cmath>       // fmod, pow
#include <concepts>    // derived_from, same_as
#include <cstddef>     // ptrdiff_t, size_t
#include <cstdint>     // uint64_t
#include <format>      // format, format_to
#include <functional>  // function
#include <limits>      // numeric_limits
#include <memory>      // make_unique, unique_ptr
#include <optional>    // optional
#include <stack>       // stack
#include <string>      // string
#include <type_traits> // remove_cvref_t
#include <utility>     // forward, move, swap
#include <variant>     // get, get_if, holds_alternative, monostate, variant, visit
#include <vector>      // erase, erase_if, vector

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>

export module loom;
export import :node;

export namespace loom {
    class Canvas {
    public:
        Canvas();
        Canvas(const Canvas&) = delete;
        Canvas(Canvas&&) = delete;
        Canvas& operator=(const Canvas&) = delete;
        Canvas& operator=(Canvas&&) = delete;
        ~Canvas();

        template<std::derived_from<Node> T>
        T& add(ImVec2 position, auto&&... args);
        void draw();

        // Call these between draw() calls. remove() requires a live node in this
        // canvas and clears history before destruction. add() is unrecorded.
        void remove(Node& node);
        void undo();
        void redo();
        void clearHistory();

        // Runs inside the Add submenu, with its opening point in canvas units.
        // Nodes added here form one undoable UI action. Balance your own ImGui
        // operations; captured application data must outlive this callable's use.
        std::function<void(Canvas&, ImVec2)> addMenu;
        NodeStyle style;

    private:
        // Ownership and inverse edits.
        struct NodeEntry {
            std::unique_ptr<Node> node;
            std::uint64_t serial;
        };

        // A transfer borrows a live node or exclusively owns its detached entry.
        struct NodeTransfer {
            std::size_t index;
            std::variant<Node*, NodeEntry> node;
        };
        struct Nodes {
            std::vector<NodeTransfer> nodes;
            std::vector<Node*> beforeSelection;
            std::vector<Node*> afterSelection;
        };
        struct Placement { Node* node; ImVec2 position; };
        struct Move { std::vector<Placement> positions; };
        struct Rename { Node* node; std::string title; };
        struct Appearance { Node* node; NodeStyle style; };
        struct Restyle { std::vector<Appearance> appearances; };
        using Action = std::variant<Nodes, Move, Rename, Restyle>;
        using History = std::stack<Action, std::vector<Action>>;

        // ImGui scopes.
        class ContextScope {
        public:
            explicit ContextScope(ImGuiContext* context);
            ContextScope(const ContextScope&) = delete;
            ContextScope& operator=(const ContextScope&) = delete;
            ~ContextScope();
        private:
            ImGuiContext* m_previous;
        };

        class NodeFrame {
        public:
            NodeFrame(const NodeEntry& entry, const NodeStyle& style, ImVec2 origin);
            NodeFrame(const NodeFrame&) = delete;
            NodeFrame& operator=(const NodeFrame&) = delete;
            ~NodeFrame();
            explicit operator bool() const;
        };

        class PopupScope {
        public:
            explicit PopupScope(const char* name);
            PopupScope(const PopupScope&) = delete;
            PopupScope& operator=(const PopupScope&) = delete;
            ~PopupScope();
            explicit operator bool() const;
        private:
            bool m_open;
        };

        class MenuScope {
        public:
            explicit MenuScope(const char* name);
            MenuScope(const MenuScope&) = delete;
            MenuScope& operator=(const MenuScope&) = delete;
            ~MenuScope();
            explicit operator bool() const;
        private:
            bool m_open;
        };

        class Frame {
        public:
            Frame(ImGuiContext* context, ImVec2 pan, float scale);
            Frame(const Frame&) = delete;
            Frame& operator=(const Frame&) = delete;
            ~Frame();
            explicit operator bool() const;
            void render(ImVec2 pan, float scale);

            ImGuiWindow* root = nullptr;
            ImVec2       screenOrigin;
            ImVec2       extent;
            ImRect       clip;
            const ImVec2 initialPan;
            const float  initialScale;
            bool         hovered = false;
            bool         focused = false;

        private:
            void forwardInput();
            void forwardRequests(ImVec2 pan, float scale);
            void append(const ImDrawList& source, ImVec2 pan, float scale) const;

            ImGuiContext* const m_host;
            ImGuiContext* const m_context;
        };

        // Interaction. References here never own nodes.
        struct NodeDrag {
            Node*  pressed;
            ImVec2 origin;
            ImVec2 applied;
            bool   toggle;
            std::vector<Placement> initial;
        };

        struct BoxSelection {
            ImVec2             origin;
            std::vector<Node*> initial;
            bool               toggle;
        };

        struct ContextMenu {
            Node*             node;
            ImVec2            position;
            std::vector<char> title;
        };

        void drawNodes(const Frame& frame);
        void advanceGesture(const Frame& frame);
        void finishGesture();
        Node* hoveredNode() const;
        void select(const Frame& frame);
        void drawSelection(const Frame& frame) const;
        void drawContextMenu(const Frame& frame);
        void drawCanvasMenu();
        void drawNodeMenu(Node& node);
        void drawStyleMenu(Node& node);
        void styleProperty(Node& node, const char* label, auto member, auto fallback);
        void beginStyleEdit();
        void finishStyleEdit();
        void shortcuts(const Frame& frame);
        void recordCreation(std::size_t first);
        void deleteSelection();
        void record(Action action);
        void replay(History& source, History& destination, bool undo);
        void transfer(Nodes& nodes);
        void detach(const NodeEntry& entry);
        const NodeEntry& entry(const Node& node) const;
        void navigate(const Frame& frame);
        void positionNodes();
        void positionPopups(const Frame& frame);
        void moveWindow(ImGuiWindow* window, ImVec2 position);
        void drawGrid(const Frame& frame) const;

        static ImGuiWindow* nodeWindow(const NodeEntry& entry);
        static bool sameStyle(const NodeStyle& a, const NodeStyle& b);
        static bool inputTitle(std::vector<char>& title);
        static auto resolve(const NodeStyle& node, const NodeStyle& canvas, auto member, auto fallback) -> decltype(fallback);
        static ImGuiContext* createContext(ImGuiContext* host);

        ImGuiContext* const m_host;
        ImGuiContext* const m_context; // Owned: ImGui creates and destroys its heap-allocated context.
        ImVec2              m_pan = {0, 0};
        float               m_scale = 1;
        std::uint64_t       m_nextSerial = 1;
        std::vector<NodeEntry>             m_nodes;
        std::vector<Node*>                 m_selection;
        std::variant<std::monostate, NodeDrag, BoxSelection> m_gesture;
        std::optional<ContextMenu> m_menu;
        std::optional<Restyle>     m_styleEdit;
        History m_undo;
        History m_redo;
    };
}


//////////////////////////
// MARK: Implementation //
//////////////////////////

namespace loom {
    Canvas::Canvas() : m_host(ImGui::GetCurrentContext()), m_context(createContext(m_host)) {}
    Canvas::~Canvas() {
        m_redo = {};
        m_undo = {};
        m_nodes.clear();
        ImGui::DestroyContext(m_context);
    }

    template<std::derived_from<Node> T>
    T& Canvas::add(ImVec2 position, auto&&... args) {
        auto node = std::make_unique<T>(std::forward<decltype(args)>(args)...);
        node->position = position;
        T& result = *node;
        m_nodes.push_back({.node = std::move(node), .serial = m_nextSerial++});
        return result;
    }

    void Canvas::draw() {
        IM_ASSERT(ImGui::GetCurrentContext() == m_host);
        // Align retained windows before ImGui performs this frame's hit testing.
        positionNodes();
        Frame frame(m_context, m_pan, m_scale);
        if (!frame) {
            finishGesture();
            finishStyleEdit();
            m_menu.reset();
            return;
        }
        ImGui::KeepAliveID(frame.root->GetID("##pan"));
        ImGui::KeepAliveID(frame.root->GetID("##selection"));
        advanceGesture(frame);
        drawNodes(frame);
        select(frame);
        drawContextMenu(frame);
        shortcuts(frame);
        drawSelection(frame);
        navigate(frame);
        positionNodes();
        positionPopups(frame);
        drawGrid(frame);
        frame.render(m_pan, m_scale);
    }

    void Canvas::remove(Node& node) {
        IM_ASSERT(!m_context->WithinFrameScope);
        const ContextScope context(m_context);
        const NodeEntry& item = entry(node);
        clearHistory();
        detach(item);
        std::erase_if(m_nodes, [&](const NodeEntry& candidate) { return candidate.node.get() == &node; });
    }

    void Canvas::undo() {
        IM_ASSERT(!m_context->WithinFrameScope);
        const ContextScope context(m_context);
        replay(m_undo, m_redo, true);
    }

    void Canvas::redo() {
        IM_ASSERT(!m_context->WithinFrameScope);
        const ContextScope context(m_context);
        replay(m_redo, m_undo, false);
    }

    void Canvas::clearHistory() {
        IM_ASSERT(!m_context->WithinFrameScope);
        const ContextScope context(m_context);
        finishGesture();
        finishStyleEdit();
        m_redo = {};
        m_undo = {};
    }

    Canvas::ContextScope::ContextScope(ImGuiContext* context) : m_previous(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(context);
    }
    Canvas::ContextScope::~ContextScope() { ImGui::SetCurrentContext(m_previous); }

    Canvas::NodeFrame::NodeFrame(const NodeEntry& entry, const NodeStyle& style, ImVec2 origin) {
        const Node& node = *entry.node;
        const std::string name = std::format("{}###loom-{}", node.title, entry.serial);
        const ImGuiStyle& theme = ImGui::GetStyle();
        const ImVec4 header = resolve(node.style, style, &NodeStyle::header, theme.Colors[ImGuiCol_TitleBg]);
        ImGui::PushStyleColor(ImGuiCol_TitleBg, header);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, header);
        ImGui::PushStyleColor(ImGuiCol_Text, resolve(node.style, style, &NodeStyle::headerText, theme.Colors[ImGuiCol_Text]));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, resolve(node.style, style, &NodeStyle::body, theme.Colors[ImGuiCol_WindowBg]));
        ImGui::PushStyleColor(ImGuiCol_Border, resolve(node.style, style, &NodeStyle::border, theme.Colors[ImGuiCol_Border]));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, resolve(node.style, style, &NodeStyle::padding, theme.WindowPadding));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, resolve(node.style, style, &NodeStyle::rounding, theme.WindowRounding));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, resolve(node.style, style, &NodeStyle::borderWidth, theme.WindowBorderSize));
        ImGui::SetNextWindowPos(origin + node.position);
        // Native auto-fit clamps to the viewport. A node's logical dimensions must
        // instead follow its content, even when zoom makes it larger than the view.
        // ImGui's cached window name can retain an older title after a rename.
        float titleWidth = ImGui::CalcTextSize(node.title.c_str(), nullptr, true).x;
        ImGui::SetNextWindowSizeConstraints({0, 0}, {FLT_MAX, FLT_MAX}, [](ImGuiSizeCallbackData* data) {
            const ImGuiWindow* window = ImGui::GetCurrentWindow();
            data->DesiredSize = window->ContentSize + window->WindowPadding * 2 + ImVec2(0, window->TitleBarHeight);
            data->DesiredSize.x = ImMax(data->DesiredSize.x, *static_cast<const float*>(data->UserData) + window->WindowPadding.x * 2);
        }, &titleWidth);
        ImGui::Begin(name.c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize);
        // Begin locks the frame's appearance; body widgets use the ordinary theme.
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);
    }

    Canvas::NodeFrame::~NodeFrame() { ImGui::End(); }
    Canvas::NodeFrame::operator bool() const { return !ImGui::GetCurrentWindow()->SkipItems; }

    Canvas::PopupScope::PopupScope(const char* name) : m_open(ImGui::BeginPopup(name)) {}
    Canvas::PopupScope::~PopupScope() { if (m_open) ImGui::EndPopup(); }
    Canvas::PopupScope::operator bool() const { return m_open; }

    Canvas::MenuScope::MenuScope(const char* name) : m_open(ImGui::BeginMenu(name)) {}
    Canvas::MenuScope::~MenuScope() { if (m_open) ImGui::EndMenu(); }
    Canvas::MenuScope::operator bool() const { return m_open; }

    Canvas::Frame::Frame(ImGuiContext* context, ImVec2 pan, float scale)
        : initialPan(pan), initialScale(scale), m_host(ImGui::GetCurrentContext()), m_context(context) {
        ImGui::PushID(m_context);
        const bool visible = ImGui::BeginChild("##canvas", {0, 0}, ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        screenOrigin = ImGui::GetCursorScreenPos();
        extent = ImGui::GetContentRegionAvail();
        clip = ImGui::GetCurrentWindow()->ClipRect;
        clip.ClipWith(ImRect(screenOrigin, screenOrigin + extent));
        if (visible && extent.x > 0 && extent.y > 0) {
            ImGui::InvisibleButton("##surface", extent, ImGuiButtonFlags_MouseButtonLeft
                | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            hovered = ImGui::IsItemHovered();
            focused = ImGui::IsWindowFocused() && !m_host->IO.AppFocusLost;
        }

        ImGui::SetCurrentContext(m_context);
        if (!visible || extent.x <= 0 || extent.y <= 0) {
            m_context->InputEventsQueue.clear();
            m_context->IO.ClearInputKeys();
            m_context->IO.ClearInputMouse();
            ImGui::ClearActiveID();
            ImGui::FocusWindow(nullptr);
            ImGui::ClosePopupsOverWindow(nullptr, false);
            return;
        }
        forwardInput();

        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = m_host->IO.DeltaTime;
        io.DisplaySize = extent / initialScale;
        // Framebuffer density changes rasterization, not logical widget dimensions.
        io.DisplayFramebufferScale = m_host->IO.DisplayFramebufferScale * initialScale;
        io.BackendFlags = m_host->IO.BackendFlags & (ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
        io.ConfigFlags = m_host->IO.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard;
        io.MouseDragThreshold = m_host->IO.MouseDragThreshold / initialScale;
        ImGui::GetStyle() = m_host->Style;
        // Canvas has positioned its windows; clip their hit bounds to this viewport
        // before NewFrame, including when the host region was resized.
        for (ImGuiWindow* window : m_context->Windows)
            window->OuterRectClipped.ClipWith(ImRect({0, 0}, io.DisplaySize));
        ImGui::NewFrame();
        if (!focused) {
            ImGui::ClearActiveID();
            ImGui::FocusWindow(nullptr);
            ImGui::ClosePopupsOverWindow(nullptr, false);
        }

        const ImVec2 position = initialPan / initialScale;
        ImGui::SetNextWindowPos(position);
        ImGui::SetNextWindowSize(ImMax(io.DisplaySize, io.DisplaySize - position));
        ImGui::Begin("##contents", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoBringToFrontOnFocus);
        root = ImGui::GetCurrentWindow();
    }

    Canvas::Frame::~Frame() {
        ImGui::SetCurrentContext(m_context);
        // An interrupted draw still closes its ImGui scopes, without rendering it.
        if (root) ImGui::End();
        if (m_context->WithinFrameScope) ImGui::EndFrame();
        ImGui::SetCurrentContext(m_host);
        ImGui::EndChild();
        ImGui::PopID();
    }

    Canvas::Frame::operator bool() const { return root != nullptr; }

    void Canvas::Frame::render(ImVec2 pan, float scale) {
        forwardRequests(pan, scale);
        ImGui::End();
        root = nullptr;
        // ImGui 1.93's modal backdrop requires exactly two triangles, including
        // at fractional density. Widget geometry is already built; Begin resets these flags next frame.
        if (ImGuiWindow* modal = ImGui::GetTopMostAndVisiblePopupModal())
            ImGui::FindBottomMostVisibleWindowWithinBeginStack(modal)->RootWindow->DrawList->Flags &= ~ImDrawFlags_AAFill;
        ImGui::Render();
        const ContextScope host(m_host);
        const ImDrawData& data = m_context->Viewports[0]->DrawDataP;
        for (const ImDrawList* list : data.CmdLists) append(*list, pan, scale);
    }

    void Canvas::Frame::forwardInput() {
        ImGuiIO& io = ImGui::GetIO();
        io.AddFocusEvent(focused);
        bool dragging = false;
        for (bool down : io.MouseDown) dragging |= down;
        // The host has already consumed this frame's events; its trail is current,
        // whereas its remaining queue contains deliberately deferred events.
        for (ImGuiInputEvent event : m_host->InputEventsTrail) {
            switch (event.Type) {
            case ImGuiInputEventType_MousePos:
                if (!hovered && !dragging) continue;
                event.MousePos.PosX = (event.MousePos.PosX - screenOrigin.x) / initialScale;
                event.MousePos.PosY = (event.MousePos.PosY - screenOrigin.y) / initialScale;
                break;
            case ImGuiInputEventType_MouseButton:
                if (event.MouseButton.Down && !hovered) continue;
                break;
            case ImGuiInputEventType_MouseWheel:
                if (!hovered) continue;
                break;
            case ImGuiInputEventType_Key:
            case ImGuiInputEventType_Text:
                if (!focused) continue;
                break;
            default: continue;
            }
            m_context->InputEventsQueue.push_back(event);
        }
        // A modifier can already be held when focus enters the canvas.
        const ImGuiIO& host = m_host->IO;
        io.AddKeyEvent(ImGuiMod_Ctrl,  focused && host.KeyCtrl);
        io.AddKeyEvent(ImGuiMod_Shift, focused && host.KeyShift);
        io.AddKeyEvent(ImGuiMod_Alt,   focused && host.KeyAlt);
        io.AddKeyEvent(ImGuiMod_Super, focused && host.KeySuper);
        // Recompute even without a mouse event: the canvas itself may have moved.
        const ImVec2 mouse = (m_host->IO.MousePos - screenOrigin) / initialScale;
        if (hovered || dragging) io.AddMousePosEvent(mouse.x, mouse.y);
        else io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    void Canvas::Frame::forwardRequests(ImVec2 pan, float scale) {
        ImGuiContext& host = *m_host;
        ImGuiContext& inner = *m_context;
        if (hovered || inner.ActiveId != 0) host.IO.WantCaptureMouse |= inner.IO.WantCaptureMouse;
        if (focused) {
            host.IO.WantCaptureKeyboard |= inner.IO.WantCaptureKeyboard;
            host.IO.WantTextInput |= inner.IO.WantTextInput;
            if (inner.WantTextInputNextFrame == 1) host.WantTextInputNextFrame = 1;
            if (inner.PlatformImeData.WantVisible || inner.WantTextInputNextFrame == 1) {
                host.PlatformImeData = inner.PlatformImeData;
                const ImVec2 origin = screenOrigin + pan - initialPan * (scale / initialScale);
                host.PlatformImeData.InputPos = origin + inner.PlatformImeData.InputPos * scale;
                host.PlatformImeData.InputLineHeight *= scale;
                host.PlatformImeData.ViewportId = host.CurrentWindow->Viewport->ID;
            }
        }
        if (hovered || inner.ActiveId != 0) host.MouseCursor = inner.MouseCursor;
    }

    void Canvas::Frame::append(const ImDrawList& source, ImVec2 pan, float scale) const {
        ImDrawList& target = *ImGui::GetWindowDrawList();
        const unsigned int vertex = static_cast<unsigned int>(target.VtxBuffer.Size);
        const unsigned int index = static_cast<unsigned int>(target.IdxBuffer.Size);
        const int callback = target._CallbacksDataBuf.Size;
        const bool offsets = (m_host->IO.BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset) != 0;
        const ImVec2 origin = screenOrigin + pan - initialPan * (scale / initialScale);

        target.VtxBuffer.reserve(target.VtxBuffer.Size + source.VtxBuffer.Size);
        target.IdxBuffer.reserve(target.IdxBuffer.Size + source.IdxBuffer.Size);
        target.CmdBuffer.reserve(target.CmdBuffer.Size + source.CmdBuffer.Size + 1);
        target._CallbacksDataBuf.reserve(target._CallbacksDataBuf.Size + source._CallbacksDataBuf.Size);
        for (ImDrawVert value : source.VtxBuffer) {
            value.pos = origin + value.pos * scale;
            target.VtxBuffer.push_back(value);
        }
        if (!offsets) IM_ASSERT(static_cast<unsigned int>(target.VtxBuffer.Size) <= std::numeric_limits<ImDrawIdx>::max());
        for (ImDrawIdx value : source.IdxBuffer)
            target.IdxBuffer.push_back(offsets ? value : static_cast<ImDrawIdx>(value + vertex));
        for (ImU8 value : source._CallbacksDataBuf) target._CallbacksDataBuf.push_back(value);
        for (ImDrawCmd command : source.CmdBuffer) {
            ImRect commandClip(origin + ImVec2(command.ClipRect.x, command.ClipRect.y) * scale,
                               origin + ImVec2(command.ClipRect.z, command.ClipRect.w) * scale);
            commandClip.ClipWithFull(clip);
            command.ClipRect = commandClip.ToVec4();
            command.IdxOffset += index;
            if (offsets) command.VtxOffset += vertex;
            if (command.UserCallbackDataSize > 0) command.UserCallbackDataOffset += callback;
            target.CmdBuffer.push_back(command);
        }
        target._VtxWritePtr = target.VtxBuffer.Data + target.VtxBuffer.Size;
        target._IdxWritePtr = target.IdxBuffer.Data + target.IdxBuffer.Size;
        // Begin a fresh segment so subsequent host drawing has valid local indices.
        if (offsets) target._CmdHeader.VtxOffset = static_cast<unsigned int>(target.VtxBuffer.Size);
        target._VtxCurrentIdx = offsets ? 0 : static_cast<unsigned int>(target.VtxBuffer.Size);
        target.AddDrawCmd();
    }

    void Canvas::drawNodes(const Frame& frame) {
        // Capture the count so creation from a node callback takes effect next frame.
        // Moving the owning pointers never moves the node objects themselves.
        const auto count = m_nodes.size();
        for (std::size_t i = 0; i < count; ++i) {
            Node& node = *m_nodes[i].node;
            if (NodeFrame nodeFrame(m_nodes[i], style, frame.root->Pos); nodeFrame) node.draw();
        }
    }

    void Canvas::advanceGesture(const Frame& frame) {
        ImGuiContext& context = *m_context;
        ImGuiIO& io = context.IO;
        const ImGuiID selection = frame.root->GetID("##selection");
        if (!frame.focused || context.ActiveId != selection) {
            finishGesture();
            return;
        }
        const bool dragging = ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left);
        if (auto* drag = std::get_if<NodeDrag>(&m_gesture)) {
            if (dragging && std::ranges::contains(m_selection, drag->pressed)) {
                if (drag->initial.empty())
                    for (Node* node : m_selection) drag->initial.push_back({.node = node, .position = node->position});
                const ImVec2 distance = io.MousePos - drag->origin;
                for (Node* node : m_selection) node->position += distance - drag->applied;
                drag->applied = distance;
                positionNodes();
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !dragging && !drag->toggle)
                m_selection = {drag->pressed};
        } else if (auto* box = std::get_if<BoxSelection>(&m_gesture); box && dragging) {
            const ImVec2 mouse = io.MousePos - frame.root->Pos;
            const ImRect bounds(ImMin(box->origin, mouse), ImMax(box->origin, mouse));
            m_selection = box->initial;
            for (const auto& item : m_nodes) {
                const auto& node = item.node;
                const ImGuiWindow* window = nodeWindow(item);
                if (!window) continue;
                if (!bounds.Overlaps(ImRect(node->position, node->position + window->Size))) continue;
                if (box->toggle && std::ranges::contains(box->initial, node.get())) std::erase(m_selection, node.get());
                else m_selection.push_back(node.get());
            }
        }
        if (!io.MouseDown[ImGuiMouseButton_Left]) {
            finishGesture();
        }
    }

    void Canvas::finishGesture() {
        if (auto* drag = std::get_if<NodeDrag>(&m_gesture); drag && std::ranges::any_of(drag->initial,
                [](const Placement& item) { return item.node->position != item.position; }))
            record(Move{.positions = std::move(drag->initial)});
        if (!std::holds_alternative<std::monostate>(m_gesture)) ImGui::ClearActiveID();
        m_gesture = std::monostate{};
    }

    Node* Canvas::hoveredNode() const {
        if (ImGuiWindow* window = m_context->HoveredWindow)
            for (const NodeEntry& item : m_nodes)
                if (nodeWindow(item) == window->RootWindow) return item.node.get();
        return nullptr;
    }

    void Canvas::select(const Frame& frame) {
        ImGuiContext& context = *m_context;
        const ImGuiIO& io = context.IO;
        if (!frame.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
        if (!std::holds_alternative<std::monostate>(m_gesture)) return;
        if (Node* node = hoveredNode()) {
            ImGuiWindow* window = context.HoveredWindow->RootWindow;
            const bool selected = std::ranges::contains(m_selection, node);
            const bool popup = !context.OpenPopupStack.empty();
            const bool widget = popup || (context.HoveredId != 0 && context.HoveredId != window->MoveId)
                || (context.ActiveId != 0 && context.ActiveId != window->MoveId);
            // A widget may just have opened a popup. Select its node without taking
            // keyboard focus back from the popup or starting a canvas gesture.
            if (!popup) ImGui::FocusWindow(window);
            if (widget) {
                if (!selected) m_selection = {node};
                return;
            }
            if (io.KeyCtrl) {
                if (selected) std::erase(m_selection, node);
                else m_selection.push_back(node);
            } else if (!selected) m_selection = {node};
            m_gesture = NodeDrag{.pressed = node, .origin = io.MousePos, .applied = {0, 0}, .toggle = io.KeyCtrl};
            ImGui::SetActiveID(frame.root->GetID("##selection"), frame.root);
            context.ActiveIdMouseButton = ImGuiMouseButton_Left;
            return;
        }
        if (context.OpenPopupStack.empty() && (context.HoveredWindow == frame.root || context.HoveredWindow == nullptr) && context.HoveredId == 0
            && (context.ActiveId == 0 || context.ActiveId == frame.root->MoveId)) {
            if (!io.KeyCtrl) m_selection.clear();
            m_gesture = BoxSelection{.origin = io.MousePos - frame.root->Pos, .initial = m_selection, .toggle = io.KeyCtrl};
            ImGui::FocusWindow(frame.root);
            ImGui::SetActiveID(frame.root->GetID("##selection"), frame.root);
            context.ActiveIdMouseButton = ImGuiMouseButton_Left;
        }
    }

    void Canvas::drawSelection(const Frame& frame) const {
        const ImGuiStyle& theme = ImGui::GetStyle();
        for (const Node* node : m_selection) {
            ImGuiWindow* window = nodeWindow(entry(*node));
            if (!window || window->Hidden) continue;
            const ImVec4 color = resolve(node->style, style, &NodeStyle::selectionBorder, theme.Colors[ImGuiCol_NavCursor]);
            const float width = resolve(node->style, style, &NodeStyle::selectionBorderWidth, 2.0f);
            window->DrawList->PushClipRectFullScreen();
            window->DrawList->AddRect(window->Pos, window->Pos + window->Size, ImGui::GetColorU32(color), window->WindowRounding, width);
            window->DrawList->PopClipRect();
        }
        if (const auto* box = std::get_if<BoxSelection>(&m_gesture); box && ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left)) {
            const ImVec2 start = frame.root->Pos + box->origin;
            const ImVec2 end = ImGui::GetIO().MousePos;
            ImDrawList* list = ImGui::GetForegroundDrawList();
            list->AddRectFilled(ImMin(start, end), ImMax(start, end), ImGui::GetColorU32(ImGuiCol_Header, 0.25f));
            list->AddRect(ImMin(start, end), ImMax(start, end), ImGui::GetColorU32(ImGuiCol_NavCursor));
        }
    }

    void Canvas::drawContextMenu(const Frame& frame) {
        ImGuiContext& context = *m_context;
        const ImGuiIO& io = context.IO;
        if (frame.hovered && frame.focused && ImGui::IsMouseReleased(ImGuiMouseButton_Right)
                && !io.MouseDown[ImGuiMouseButton_Left] && !io.MouseDown[ImGuiMouseButton_Middle]
                && context.OpenPopupStack.empty() && std::holds_alternative<std::monostate>(m_gesture)) {
            Node* target = hoveredNode();
            const bool background = context.HoveredWindow == frame.root || context.HoveredWindow == nullptr;
            if ((target || background) && (context.ActiveId == 0 || context.ActiveId == context.InputTextState.ID)) {
                finishStyleEdit();
                m_menu = ContextMenu{.node = target, .position = io.MousePos - frame.root->Pos};
                if (target) {
                    if (!std::ranges::contains(m_selection, target)) m_selection = {target};
                    ImGui::FocusWindow(context.HoveredWindow->RootWindow);
                }
                ImGui::OpenPopup("##context");
            }
        }
        if (const PopupScope popup("##context"); popup) {
            if (m_menu->node) drawNodeMenu(*m_menu->node);
            else drawCanvasMenu();
        }
        if (!ImGui::IsPopupOpen("##context")) {
            finishStyleEdit();
            m_menu.reset();
        } else if (!frame.focused || context.ActiveId == 0) finishStyleEdit();
    }

    void Canvas::drawCanvasMenu() {
        if (addMenu) {
            if (const MenuScope menu("Add"); menu) {
                const auto first = m_nodes.size();
                try {
                    addMenu(*this, m_menu->position);
                } catch (...) {
                    // Successful additions remain owned and undoable even if the
                    // application's balanced menu callback subsequently throws.
                    recordCreation(first);
                    throw;
                }
                recordCreation(first);
            }
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_undo.empty())) replay(m_undo, m_redo, true);
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_redo.empty())) replay(m_redo, m_undo, false);
        ImGui::Separator();
        ImGui::TextDisabled("Zoom: %.0f%%", m_scale * 100);
        if (ImGui::MenuItem("Reset zoom", nullptr, false, m_scale != 1)) {
            m_pan = ImGui::GetMousePosOnOpeningCurrentPopup() * m_scale - m_menu->position;
            m_scale = 1;
        }
    }

    void Canvas::drawNodeMenu(Node& node) {
        if (const MenuScope menu("Rename"); menu) {
            if (ImGui::IsWindowAppearing()) {
                m_menu->title.assign(node.title.begin(), node.title.end());
                m_menu->title.push_back('\0');
                ImGui::SetKeyboardFocusHere();
            }
            const bool enter = inputTitle(m_menu->title);
            if (ImGui::Button("Apply") || enter) {
                std::string title(m_menu->title.data());
                if (title != node.title) {
                    Rename change{.node = &node, .title = std::move(node.title)};
                    node.title = std::move(title);
                    record(std::move(change));
                }
                ImGui::CloseCurrentPopup();
            }
        }
        if (const MenuScope menu("Style"); menu) drawStyleMenu(node);
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Delete")) deleteSelection();
    }

    void Canvas::drawStyleMenu(Node& node) {
        const ImGuiStyle& theme = ImGui::GetStyle();
        styleProperty(node, "Header",           &NodeStyle::header,               theme.Colors[ImGuiCol_TitleBg]);
        styleProperty(node, "Header text",      &NodeStyle::headerText,           theme.Colors[ImGuiCol_Text]);
        styleProperty(node, "Body",             &NodeStyle::body,                 theme.Colors[ImGuiCol_WindowBg]);
        styleProperty(node, "Border",           &NodeStyle::border,               theme.Colors[ImGuiCol_Border]);
        styleProperty(node, "Selection border", &NodeStyle::selectionBorder,      theme.Colors[ImGuiCol_NavCursor]);
        ImGui::Separator();
        styleProperty(node, "Padding",          &NodeStyle::padding,              theme.WindowPadding);
        styleProperty(node, "Rounding",         &NodeStyle::rounding,             theme.WindowRounding);
        styleProperty(node, "Border width",     &NodeStyle::borderWidth,          theme.WindowBorderSize);
        styleProperty(node, "Selection width",  &NodeStyle::selectionBorderWidth, 2.0f);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset style")) {
            finishStyleEdit();
            beginStyleEdit();
            for (Node* selected : m_selection) selected->style = {};
            finishStyleEdit();
        }
    }

    void Canvas::styleProperty(Node& node, const char* label, auto member, auto fallback) {
        ImGui::PushID(label);
        // Display the clicked node's value; edits apply to the selected group.
        auto value = resolve(node.style, style, member, fallback);
        ImGui::SetNextItemWidth(170);
        bool changed;
        if constexpr (std::same_as<decltype(value), ImVec4>)
            changed = ImGui::ColorEdit4(label, &value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        else if constexpr (std::same_as<decltype(value), ImVec2>)
            changed = ImGui::DragFloat2(label, &value.x, 0.1f, 0, FLT_MAX, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        else
            changed = ImGui::DragFloat(label, &value, 0.1f, 0, FLT_MAX, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        if (changed) {
            beginStyleEdit();
            for (Node* selected : m_selection) selected->style.*member = value;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) finishStyleEdit();
        ImGui::SameLine();
        ImGui::BeginDisabled(!std::ranges::any_of(m_selection,
            [&](const Node* selected) { return (selected->style.*member).has_value(); }));
        if (ImGui::SmallButton("Inherit")) {
            finishStyleEdit();
            beginStyleEdit();
            for (Node* selected : m_selection) (selected->style.*member).reset();
            finishStyleEdit();
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    void Canvas::beginStyleEdit() {
        if (m_styleEdit) return;
        m_styleEdit.emplace();
        for (Node* selected : m_selection)
            m_styleEdit->appearances.push_back({.node = selected, .style = selected->style});
    }

    void Canvas::finishStyleEdit() {
        if (!m_styleEdit) return;
        if (std::ranges::any_of(m_styleEdit->appearances,
                [](const Appearance& item) { return !sameStyle(item.style, item.node->style); }))
            record(std::move(*m_styleEdit));
        m_styleEdit.reset();
    }

    void Canvas::shortcuts(const Frame& frame) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!frame.focused || io.WantTextInput || m_context->ActiveId != 0 || !m_context->OpenPopupStack.empty()) return;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            if (io.KeyShift) replay(m_redo, m_undo, false);
            else replay(m_undo, m_redo, true);
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) replay(m_redo, m_undo, false);
        else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelection();
    }

    void Canvas::recordCreation(std::size_t first) {
        if (first == m_nodes.size()) return;
        Nodes change{.beforeSelection = m_selection};
        for (std::size_t i = first; i < m_nodes.size(); ++i) {
            change.nodes.push_back({.index = i, .node = m_nodes[i].node.get()});
            change.afterSelection.push_back(m_nodes[i].node.get());
        }
        m_selection = change.afterSelection;
        record(std::move(change));
    }

    void Canvas::deleteSelection() {
        if (m_selection.empty()) return;
        finishGesture();
        finishStyleEdit();
        Nodes change{.beforeSelection = m_selection};
        for (std::size_t i = 0; i < m_nodes.size(); ++i)
            if (std::ranges::contains(m_selection, m_nodes[i].node.get()))
                change.nodes.push_back({.index = i, .node = m_nodes[i].node.get()});
        transfer(change);
        m_selection = change.afterSelection;
        record(std::move(change));
    }

    void Canvas::record(Action action) {
        m_redo = {};
        m_undo.push(std::move(action));
    }

    void Canvas::replay(History& source, History& destination, bool undo) {
        finishGesture();
        finishStyleEdit();
        if (source.empty()) return;
        std::visit([&](auto& change) {
            using T = std::remove_cvref_t<decltype(change)>;
            if constexpr (std::same_as<T, Nodes>) {
                transfer(change);
                m_selection = undo ? change.beforeSelection : change.afterSelection;
            } else if constexpr (std::same_as<T, Move>) {
                for (Placement& item : change.positions) std::swap(item.node->position, item.position);
            } else if constexpr (std::same_as<T, Rename>) std::swap(change.node->title, change.title);
            else for (Appearance& item : change.appearances) std::swap(item.node->style, item.style);
        }, source.top());
        destination.push(std::move(source.top()));
        source.pop();
    }

    void Canvas::transfer(Nodes& change) {
        if (std::holds_alternative<Node*>(change.nodes.front().node)) {
            // Remove back-to-front and restore front-to-back to preserve order.
            for (auto it = change.nodes.rbegin(); it != change.nodes.rend(); ++it) {
                Node* node = std::get<Node*>(it->node);
                auto found = std::ranges::find_if(m_nodes, [&](const NodeEntry& item) { return item.node.get() == node; });
                IM_ASSERT(found != m_nodes.end());
                it->index = static_cast<std::size_t>(found - m_nodes.begin());
                detach(*found);
                it->node = std::move(*found);
                m_nodes.erase(found);
            }
        } else {
            for (NodeTransfer& item : change.nodes) {
                NodeEntry& owned = std::get<NodeEntry>(item.node);
                Node* node = owned.node.get();
                m_nodes.insert(m_nodes.begin() + static_cast<std::ptrdiff_t>(item.index), std::move(owned));
                item.node = node;
            }
        }
    }

    void Canvas::detach(const NodeEntry& entry) {
        Node* node = entry.node.get();
        std::erase(m_selection, node);
        ImGuiWindow* window = nodeWindow(entry);
        auto belongs = [window](ImGuiWindow* candidate) {
            if (!window) return false;
            for (; candidate; candidate = candidate->ParentWindowInBeginStack)
                if (candidate == window) return true;
            return false;
        };
        if (belongs(m_context->ActiveIdWindow)) ImGui::ClearActiveID();
        if (belongs(m_context->NavWindow)) ImGui::FocusWindow(nullptr);
        if (belongs(m_context->HoveredWindow)) m_context->HoveredWindow = nullptr;
        if (belongs(m_context->HoveredWindowUnderMovingWindow)) m_context->HoveredWindowUnderMovingWindow = nullptr;
        if (m_menu && m_menu->node == node) {
            ImGui::ClosePopupsOverWindow(nullptr, false);
            m_menu.reset();
        } else if (std::ranges::any_of(m_context->OpenPopupStack, [&](const ImGuiPopupData& popup) { return belongs(popup.Window); }))
            ImGui::ClosePopupsOverWindow(nullptr, false);
        for (ImGuiWindow* child : m_context->Windows) {
            if (!belongs(child)) continue;
            child->Active = child->WasActive = false;
            child->Hidden = true;
            child->OuterRectClipped = ImRect(0, 0, 0, 0);
        }
    }

    const Canvas::NodeEntry& Canvas::entry(const Node& node) const {
        auto found = std::ranges::find_if(m_nodes, [&](const NodeEntry& item) { return item.node.get() == &node; });
        IM_ASSERT(found != m_nodes.end());
        return *found;
    }

    void Canvas::navigate(const Frame& frame) {
        ImGuiContext& context = *m_context;
        ImGuiIO& io = context.IO;
        const ImGuiID pan = frame.root->GetID("##pan");
        const bool background = frame.hovered && (context.HoveredWindow == frame.root || context.HoveredWindow == nullptr) && context.HoveredId == 0
            && !io.MouseDown[ImGuiMouseButton_Left] && !io.MouseDown[ImGuiMouseButton_Right];
        if (background && io.MouseWheel != 0) {
            const float scale = std::clamp(frame.initialScale * std::pow(1.1f, io.MouseWheel), 0.25f, 2.0f);
            const ImVec2 mouse = m_host->IO.MousePos - frame.screenOrigin;
            m_pan = mouse - (mouse - frame.initialPan) * (scale / frame.initialScale);
            m_scale = scale;
        }
        if (background && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            ImGui::SetActiveID(pan, frame.root);
            context.ActiveIdMouseButton = ImGuiMouseButton_Middle;
        } else if (context.ActiveId == pan) {
            if (io.MouseDown[ImGuiMouseButton_Middle]) m_pan += m_host->IO.MouseDelta;
            else ImGui::ClearActiveID();
        }
    }

    void Canvas::positionNodes() {
        const ContextScope context(m_context);
        const ImVec2 origin = ImTrunc(m_pan / m_scale);
        for (const auto& item : m_nodes)
            if (ImGuiWindow* window = nodeWindow(item))
                moveWindow(window, ImTrunc(origin + item.node->position));
    }

    void Canvas::positionPopups(const Frame& frame) {
        // The content window follows the view on its next Begin(). Popup windows
        // retain positions, so move them and their anchors by the same rounded delta.
        const ImVec2 delta = ImTrunc(m_pan / m_scale) - frame.root->Pos;
        for (ImGuiWindow* window : m_context->Windows) {
            if (!(window->RootWindow->Flags & ImGuiWindowFlags_Popup)) continue;
            if (!(window->RootWindow->Flags & ImGuiWindowFlags_Modal)) window->Flags |= ImGuiWindowFlags_NoMove;
            if (delta.x != 0 || delta.y != 0) {
                ImGui::SetWindowPos(window, window->Pos + delta);
                window->OuterRectClipped.Translate(delta); // NewFrame hit testing precedes Begin().
            }
        }
        for (ImGuiPopupData& popup : m_context->OpenPopupStack) {
            popup.OpenPopupPos += delta;
            popup.OpenMousePos += delta;
        }
    }

    void Canvas::moveWindow(ImGuiWindow* window, ImVec2 position) {
        const ImVec2 delta = position - window->Pos;
        if (delta.x == 0 && delta.y == 0) return;
        for (ImGuiWindow* child : m_context->Windows) {
            if (child->RootWindow != window) continue;
            ImGui::SetWindowPos(child, child->Pos + delta);
            child->InnerRect.Translate(delta);
        }
        // NewFrame hit testing precedes Begin, including for newly exposed children.
        for (ImGuiWindow* child : m_context->Windows) {
            if (child->RootWindow != window) continue;
            child->OuterRectClipped = child->Rect();
            if (child->Flags & ImGuiWindowFlags_ChildWindow) child->OuterRectClipped.ClipWith(child->ParentWindow->InnerRect);
        }
    }

    void Canvas::drawGrid(const Frame& frame) const {
        const ContextScope host(m_host);
        ImDrawList& list = *ImGui::GetWindowDrawList();
        const float spacing = 32 * m_scale;
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_Border, 0.45f);
        list.PushClipRect(frame.clip.Min, frame.clip.Max, true);
        for (float x = std::fmod(m_pan.x, spacing); x < frame.extent.x; x += spacing)
            list.AddLine({frame.screenOrigin.x + x, frame.screenOrigin.y}, {frame.screenOrigin.x + x, frame.screenOrigin.y + frame.extent.y}, color);
        for (float y = std::fmod(m_pan.y, spacing); y < frame.extent.y; y += spacing)
            list.AddLine({frame.screenOrigin.x, frame.screenOrigin.y + y}, {frame.screenOrigin.x + frame.extent.x, frame.screenOrigin.y + y}, color);
        list.PopClipRect();
    }

    ImGuiWindow* Canvas::nodeWindow(const NodeEntry& entry) {
        // Lookup needs only the stable suffix, without allocating or copying the title.
        char name[32];
        const auto end = std::format_to(name, "###loom-{}", entry.serial);
        return ImGui::FindWindowByID(ImHashStr(name, static_cast<std::size_t>(end - name)));
    }

    bool Canvas::sameStyle(const NodeStyle& a, const NodeStyle& b) {
        return a.header == b.header && a.headerText == b.headerText && a.body == b.body && a.border == b.border
            && a.selectionBorder == b.selectionBorder && a.padding == b.padding && a.rounding == b.rounding
            && a.borderWidth == b.borderWidth && a.selectionBorderWidth == b.selectionBorderWidth;
    }

    bool Canvas::inputTitle(std::vector<char>& title) {
        return ImGui::InputText("Title", title.data(), title.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
            [](ImGuiInputTextCallbackData* data) {
                auto& buffer = *static_cast<std::vector<char>*>(data->UserData);
                buffer.resize(static_cast<std::size_t>(data->BufSize));
                data->Buf = buffer.data();
                return 0;
            }, &title);
    }

    auto Canvas::resolve(const NodeStyle& node, const NodeStyle& canvas, auto member, auto fallback) -> decltype(fallback) {
        return (node.*member).value_or((canvas.*member).value_or(fallback));
    }

    ImGuiContext* Canvas::createContext(ImGuiContext* host) {
        IM_ASSERT(host != nullptr && "Construct Canvas after the host ImGui context");
        ImGuiContext* context = ImGui::CreateContext(host->IO.Fonts);
        const ContextScope scope(context);
        context->IO.IniFilename = nullptr;
        context->IO.LogFilename = nullptr;
        context->IO.ConfigInputTrickleEventQueue = false;
        ImGuiPlatformIO& platform = context->PlatformIO;
        platform.Platform_ClipboardUserData = host;
        platform.Platform_GetClipboardTextFn = [](ImGuiContext* inner) {
            const ContextScope scope(static_cast<ImGuiContext*>(inner->PlatformIO.Platform_ClipboardUserData));
            return ImGui::GetClipboardText();
        };
        platform.Platform_SetClipboardTextFn = [](ImGuiContext* inner, const char* text) {
            const ContextScope scope(static_cast<ImGuiContext*>(inner->PlatformIO.Platform_ClipboardUserData));
            ImGui::SetClipboardText(text);
        };
        platform.Platform_SetImeDataFn = nullptr;
        return context;
    }
}
