module;

#include <algorithm> // clamp, contains
#include <cmath>     // fmod, pow
#include <concepts>  // derived_from
#include <cstddef>   // size_t
#include <format>    // format, format_to
#include <limits>    // numeric_limits
#include <memory>    // make_unique, unique_ptr
#include <string>    // string
#include <utility>   // forward, move
#include <variant>   // get_if, holds_alternative, monostate, variant
#include <vector>    // erase, vector

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

        NodeStyle style;

    private:
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
            NodeFrame(const Node& node, const NodeStyle& style, ImVec2 origin);
            NodeFrame(const NodeFrame&) = delete;
            NodeFrame& operator=(const NodeFrame&) = delete;
            ~NodeFrame();
            explicit operator bool() const;
        };

        struct NodeDrag {
            Node*  pressed;
            ImVec2 origin;
            ImVec2 applied;
            bool   toggle;
        };

        struct BoxSelection {
            ImVec2             origin;
            std::vector<Node*> initial;
            bool               toggle;
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

        void drawNodes(const Frame& frame);
        void advanceGesture(const Frame& frame);
        void select(const Frame& frame);
        void drawSelection(const Frame& frame) const;
        void navigate(const Frame& frame);
        void positionNodes();
        void positionPopups(const Frame& frame);
        void moveWindow(ImGuiWindow* window, ImVec2 position);
        void drawGrid(const Frame& frame) const;

        static ImGuiWindow* nodeWindow(const Node& node);
        static auto resolve(const NodeStyle& node, const NodeStyle& canvas, auto member, auto fallback) -> decltype(fallback);
        static ImGuiContext* createContext(ImGuiContext* host);

        ImGuiContext* const m_host;
        ImGuiContext* const m_context; // Owned: ImGui creates and destroys its heap-allocated context.
        ImVec2              m_pan = {0, 0};
        float               m_scale = 1;
        std::vector<std::unique_ptr<Node>> m_nodes;
        std::vector<Node*>                 m_selection;
        std::variant<std::monostate, NodeDrag, BoxSelection> m_gesture;
    };
}


//////////////////////////
// MARK: Implementation //
//////////////////////////

namespace loom {
    Canvas::Canvas() : m_host(ImGui::GetCurrentContext()), m_context(createContext(m_host)) {}
    Canvas::~Canvas() {
        m_nodes.clear();
        ImGui::DestroyContext(m_context);
    }

    template<std::derived_from<Node> T>
    T& Canvas::add(ImVec2 position, auto&&... args) {
        auto node = std::make_unique<T>(std::forward<decltype(args)>(args)...);
        node->position = position;
        T& result = *node;
        m_nodes.push_back(std::move(node));
        return result;
    }

    void Canvas::draw() {
        IM_ASSERT(ImGui::GetCurrentContext() == m_host);
        // Align retained windows before ImGui performs this frame's hit testing.
        positionNodes();
        Frame frame(m_context, m_pan, m_scale);
        if (!frame) {
            m_gesture = std::monostate{};
            return;
        }
        ImGui::KeepAliveID(frame.root->GetID("##pan"));
        ImGui::KeepAliveID(frame.root->GetID("##selection"));
        advanceGesture(frame);
        drawNodes(frame);
        select(frame);
        drawSelection(frame);
        navigate(frame);
        positionNodes();
        positionPopups(frame);
        drawGrid(frame);
        frame.render(m_pan, m_scale);
    }

    Canvas::ContextScope::ContextScope(ImGuiContext* context) : m_previous(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(context);
    }
    Canvas::ContextScope::~ContextScope() { ImGui::SetCurrentContext(m_previous); }

    Canvas::NodeFrame::NodeFrame(const Node& node, const NodeStyle& style, ImVec2 origin) {
        const std::string name = std::format("{}###loom-{}", node.title, static_cast<const void*>(&node));
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
        ImGui::SetNextWindowSizeConstraints({0, 0}, {FLT_MAX, FLT_MAX}, [](ImGuiSizeCallbackData* data) {
            const ImGuiWindow* window = ImGui::GetCurrentWindow();
            data->DesiredSize = window->ContentSize + window->WindowPadding * 2 + ImVec2(0, window->TitleBarHeight);
            data->DesiredSize.x = ImMax(data->DesiredSize.x, ImGui::CalcTextSize(window->Name, nullptr, true).x + window->WindowPadding.x * 2);
        });
        ImGui::Begin(name.c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize);
        // Begin locks the frame's appearance; body widgets use the ordinary theme.
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);
    }

    Canvas::NodeFrame::~NodeFrame() { ImGui::End(); }
    Canvas::NodeFrame::operator bool() const { return !ImGui::GetCurrentWindow()->SkipItems; }

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
            Node& node = *m_nodes[i];
            if (NodeFrame nodeFrame(node, style, frame.root->Pos); nodeFrame) node.draw();
        }
    }

    void Canvas::advanceGesture(const Frame& frame) {
        ImGuiContext& context = *m_context;
        ImGuiIO& io = context.IO;
        const ImGuiID selection = frame.root->GetID("##selection");
        if (!frame.focused || context.ActiveId != selection) {
            m_gesture = std::monostate{};
            return;
        }
        const bool dragging = ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left);
        if (auto* drag = std::get_if<NodeDrag>(&m_gesture)) {
            if (dragging && std::ranges::contains(m_selection, drag->pressed)) {
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
            for (const auto& node : m_nodes) {
                const ImGuiWindow* window = nodeWindow(*node);
                if (!window) continue;
                if (!bounds.Overlaps(ImRect(node->position, node->position + window->Size))) continue;
                if (box->toggle && std::ranges::contains(box->initial, node.get())) std::erase(m_selection, node.get());
                else m_selection.push_back(node.get());
            }
        }
        if (!io.MouseDown[ImGuiMouseButton_Left]) {
            m_gesture = std::monostate{};
            ImGui::ClearActiveID();
        }
    }

    void Canvas::select(const Frame& frame) {
        ImGuiContext& context = *m_context;
        const ImGuiIO& io = context.IO;
        if (!frame.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
        if (!std::holds_alternative<std::monostate>(m_gesture)) return;
        for (const auto& node : m_nodes) {
            ImGuiWindow* window = nodeWindow(*node);
            if (!window || !context.HoveredWindow || context.HoveredWindow->RootWindow != window) continue;
            const bool selected = std::ranges::contains(m_selection, node.get());
            const bool popup = !context.OpenPopupStack.empty();
            const bool widget = popup || (context.HoveredId != 0 && context.HoveredId != window->MoveId)
                || (context.ActiveId != 0 && context.ActiveId != window->MoveId);
            // A widget may just have opened a popup. Select its node without taking
            // keyboard focus back from the popup or starting a canvas gesture.
            if (!popup) ImGui::FocusWindow(window);
            if (widget) {
                if (!selected) m_selection = {node.get()};
                return;
            }
            if (io.KeyCtrl) {
                if (selected) std::erase(m_selection, node.get());
                else m_selection.push_back(node.get());
            } else if (!selected) m_selection = {node.get()};
            m_gesture = NodeDrag{.pressed = node.get(), .origin = io.MousePos, .applied = {0, 0}, .toggle = io.KeyCtrl};
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
            ImGuiWindow* window = nodeWindow(*node);
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
        for (const auto& node : m_nodes)
            if (ImGuiWindow* window = nodeWindow(*node))
                moveWindow(window, ImTrunc(origin + node->position));
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

    ImGuiWindow* Canvas::nodeWindow(const Node& node) {
        // Lookup needs only the stable suffix, without allocating or copying the title.
        char name[16 + sizeof(void*) * 2];
        const auto end = std::format_to(name, "###loom-{}", static_cast<const void*>(&node));
        return ImGui::FindWindowByID(ImHashStr(name, static_cast<std::size_t>(end - name)));
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
