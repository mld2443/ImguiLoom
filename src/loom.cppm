module;

#include <algorithm>
#include <cmath>
#include <concepts>
#include <limits>
#include <utility>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>

export module loom;

export namespace loom {
    class Canvas {
    public:
        Canvas();
        Canvas(const Canvas&) = delete;
        Canvas(Canvas&&) = delete;
        Canvas& operator=(const Canvas&) = delete;
        Canvas& operator=(Canvas&&) = delete;
        ~Canvas();

        void draw(std::invocable auto&& content);

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

        class Frame {
        public:
            explicit Frame(Canvas& canvas);
            Frame(const Frame&) = delete;
            Frame& operator=(const Frame&) = delete;
            ~Frame();
            explicit operator bool() const;
        private:
            void forwardInput();
            void navigate();
            void positionPopups();
            void forwardRequests();
            void drawGrid() const;
            void append(const ImDrawList& source) const;

            Canvas&      m_canvas;
            ImGuiWindow* m_root = nullptr;
            ImVec2       m_origin;
            ImVec2       m_extent;
            ImRect       m_clip;
            ImVec2       m_pan;
            float        m_scale;
            bool         m_hovered = false;
            bool         m_focused = false;
        };

        static ImGuiContext* createContext(ImGuiContext* host);

        ImGuiContext* const m_host;
        ImGuiContext* const m_context; // Owned: ImGui creates and destroys its heap-allocated context.
        ImVec2              m_pan = {0, 0};
        float               m_scale = 1;
    };
}


//////////////////////////
// MARK: Implementation //
//////////////////////////

namespace loom {
    Canvas::Canvas() : m_host(ImGui::GetCurrentContext()), m_context(createContext(m_host)) {}
    Canvas::~Canvas() { ImGui::DestroyContext(m_context); }

    void Canvas::draw(std::invocable auto&& content) {
        IM_ASSERT(ImGui::GetCurrentContext() == m_host);
        if (Frame frame(*this); frame) std::forward<decltype(content)>(content)();
    }

    Canvas::ContextScope::ContextScope(ImGuiContext* context) : m_previous(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(context);
    }
    Canvas::ContextScope::~ContextScope() { ImGui::SetCurrentContext(m_previous); }

    Canvas::Frame::Frame(Canvas& canvas) : m_canvas(canvas), m_pan(canvas.m_pan), m_scale(canvas.m_scale) {
        ImGui::PushID(&canvas);
        const bool visible = ImGui::BeginChild("##canvas", {0, 0}, ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        m_origin = ImGui::GetCursorScreenPos();
        m_extent = ImGui::GetContentRegionAvail();
        m_clip = ImGui::GetCurrentWindow()->ClipRect;
        m_clip.ClipWith(ImRect(m_origin, m_origin + m_extent));
        if (visible && m_extent.x > 0 && m_extent.y > 0) {
            ImGui::InvisibleButton("##surface", m_extent, ImGuiButtonFlags_MouseButtonLeft
                | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            m_hovered = ImGui::IsItemHovered();
            m_focused = ImGui::IsWindowFocused() && !canvas.m_host->IO.AppFocusLost;
        }

        ImGui::SetCurrentContext(canvas.m_context);
        if (!visible || m_extent.x <= 0 || m_extent.y <= 0) {
            canvas.m_context->InputEventsQueue.clear();
            canvas.m_context->IO.ClearInputKeys();
            canvas.m_context->IO.ClearInputMouse();
            ImGui::ClearActiveID();
            ImGui::FocusWindow(nullptr);
            ImGui::ClosePopupsOverWindow(nullptr, false);
            return;
        }
        forwardInput();

        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = canvas.m_host->IO.DeltaTime;
        io.DisplaySize = m_extent / m_scale;
        // Framebuffer density changes rasterization, not logical widget dimensions.
        io.DisplayFramebufferScale = canvas.m_host->IO.DisplayFramebufferScale * m_scale;
        io.BackendFlags = canvas.m_host->IO.BackendFlags & (ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
        io.ConfigFlags = canvas.m_host->IO.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetStyle() = canvas.m_host->Style;
        ImGui::NewFrame();
        if (!m_focused) {
            ImGui::ClearActiveID();
            ImGui::FocusWindow(nullptr);
            ImGui::ClosePopupsOverWindow(nullptr, false);
        }

        const ImVec2 position = m_pan / m_scale;
        ImGui::SetNextWindowPos(position);
        ImGui::SetNextWindowSize(ImMax(io.DisplaySize, io.DisplaySize - position));
        ImGui::Begin("##contents", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoBringToFrontOnFocus);
        m_root = ImGui::GetCurrentWindow();
        ImGui::KeepAliveID(m_root->GetID("##pan"));
    }

    Canvas::Frame::~Frame() {
        if (m_root) {
            navigate();
            forwardRequests();
            ImGui::End();
            positionPopups();
            // ImGui 1.93's modal backdrop requires exactly two triangles, including
            // at fractional density. Widget geometry is already built; Begin resets these flags next frame.
            if (ImGuiWindow* modal = ImGui::GetTopMostAndVisiblePopupModal())
                ImGui::FindBottomMostVisibleWindowWithinBeginStack(modal)->RootWindow->DrawList->Flags &= ~ImDrawFlags_AAFill;
            ImGui::Render();
            ImGui::SetCurrentContext(m_canvas.m_host);
            drawGrid();
            const ImDrawData& data = m_canvas.m_context->Viewports[0]->DrawDataP;
            for (const ImDrawList* list : data.CmdLists) append(*list);
        } else ImGui::SetCurrentContext(m_canvas.m_host);
        ImGui::EndChild();
        ImGui::PopID();
    }

    Canvas::Frame::operator bool() const { return m_root != nullptr; }

    void Canvas::Frame::forwardInput() {
        ImGuiIO& io = ImGui::GetIO();
        io.AddFocusEvent(m_focused);
        bool dragging = false;
        for (bool down : io.MouseDown) dragging |= down;
        // The host has already consumed this frame's events; its trail is current,
        // whereas its remaining queue contains deliberately deferred events.
        for (ImGuiInputEvent event : m_canvas.m_host->InputEventsTrail) {
            switch (event.Type) {
            case ImGuiInputEventType_MousePos:
                if (!m_hovered && !dragging) continue;
                event.MousePos.PosX = (event.MousePos.PosX - m_origin.x) / m_scale;
                event.MousePos.PosY = (event.MousePos.PosY - m_origin.y) / m_scale;
                break;
            case ImGuiInputEventType_MouseButton:
                if (event.MouseButton.Down && !m_hovered) continue;
                break;
            case ImGuiInputEventType_MouseWheel:
                if (!m_hovered) continue;
                break;
            case ImGuiInputEventType_Key:
            case ImGuiInputEventType_Text:
                if (!m_focused) continue;
                break;
            default: continue;
            }
            m_canvas.m_context->InputEventsQueue.push_back(event);
        }
        // A modifier can already be held when focus enters the canvas.
        const ImGuiIO& host = m_canvas.m_host->IO;
        io.AddKeyEvent(ImGuiMod_Ctrl,  m_focused && host.KeyCtrl);
        io.AddKeyEvent(ImGuiMod_Shift, m_focused && host.KeyShift);
        io.AddKeyEvent(ImGuiMod_Alt,   m_focused && host.KeyAlt);
        io.AddKeyEvent(ImGuiMod_Super, m_focused && host.KeySuper);
        // Recompute even without a mouse event: the canvas itself may have moved.
        const ImVec2 mouse = (m_canvas.m_host->IO.MousePos - m_origin) / m_scale;
        if (m_hovered || dragging) io.AddMousePosEvent(mouse.x, mouse.y);
        else io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    void Canvas::Frame::navigate() {
        ImGuiContext& context = *m_canvas.m_context;
        ImGuiIO& io = context.IO;
        const ImGuiID pan = m_root->GetID("##pan");
        const bool background = m_hovered && (context.HoveredWindow == m_root || context.HoveredWindow == nullptr) && context.HoveredId == 0
            && !io.MouseDown[ImGuiMouseButton_Left] && !io.MouseDown[ImGuiMouseButton_Right];
        if (background && io.MouseWheel != 0) {
            const float scale = std::clamp(m_scale * std::pow(1.1f, io.MouseWheel), 0.25f, 2.0f);
            const ImVec2 mouse = m_canvas.m_host->IO.MousePos - m_origin;
            m_canvas.m_pan = mouse - (mouse - m_pan) * (scale / m_scale);
            m_canvas.m_scale = scale;
        }
        if (background && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            ImGui::SetActiveID(pan, m_root);
            context.ActiveIdMouseButton = ImGuiMouseButton_Middle;
        } else if (context.ActiveId == pan) {
            if (io.MouseDown[ImGuiMouseButton_Middle]) m_canvas.m_pan += m_canvas.m_host->IO.MouseDelta;
            else ImGui::ClearActiveID();
        }
    }

    void Canvas::Frame::positionPopups() {
        // The content window follows the view on its next Begin(). Popup windows
        // retain positions, so move them and their anchors by the same rounded delta.
        const ImVec2 delta = ImTrunc(m_canvas.m_pan / m_canvas.m_scale) - m_root->Pos;
        for (ImGuiWindow* window : m_canvas.m_context->Windows) {
            if (!(window->RootWindow->Flags & ImGuiWindowFlags_Popup)) continue;
            if (!(window->RootWindow->Flags & ImGuiWindowFlags_Modal)) window->Flags |= ImGuiWindowFlags_NoMove;
            if (delta.x != 0 || delta.y != 0) {
                ImGui::SetWindowPos(window, window->Pos + delta);
                window->OuterRectClipped.Translate(delta); // NewFrame hit testing precedes Begin().
            }
        }
        for (ImGuiPopupData& popup : m_canvas.m_context->OpenPopupStack) {
            popup.OpenPopupPos += delta;
            popup.OpenMousePos += delta;
        }
    }

    void Canvas::Frame::forwardRequests() {
        ImGuiContext& host = *m_canvas.m_host;
        ImGuiContext& inner = *m_canvas.m_context;
        if (m_hovered || inner.ActiveId != 0) host.IO.WantCaptureMouse |= inner.IO.WantCaptureMouse;
        if (m_focused) {
            host.IO.WantCaptureKeyboard |= inner.IO.WantCaptureKeyboard;
            host.IO.WantTextInput |= inner.IO.WantTextInput;
            if (inner.WantTextInputNextFrame == 1) host.WantTextInputNextFrame = 1;
            if (inner.PlatformImeData.WantVisible || inner.WantTextInputNextFrame == 1) {
                host.PlatformImeData = inner.PlatformImeData;
                const float scale = m_canvas.m_scale;
                const ImVec2 origin = m_origin + m_canvas.m_pan - m_pan * (scale / m_scale);
                host.PlatformImeData.InputPos = origin + inner.PlatformImeData.InputPos * scale;
                host.PlatformImeData.InputLineHeight *= scale;
                host.PlatformImeData.ViewportId = host.CurrentWindow->Viewport->ID;
            }
        }
        if (m_hovered || inner.ActiveId != 0) host.MouseCursor = inner.MouseCursor;
    }

    void Canvas::Frame::drawGrid() const {
        ImDrawList& list = *ImGui::GetWindowDrawList();
        const float spacing = 32 * m_canvas.m_scale;
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_Border, 0.45f);
        list.PushClipRect(m_clip.Min, m_clip.Max, true);
        for (float x = std::fmod(m_canvas.m_pan.x, spacing); x < m_extent.x; x += spacing)
            list.AddLine({m_origin.x + x, m_origin.y}, {m_origin.x + x, m_origin.y + m_extent.y}, color);
        for (float y = std::fmod(m_canvas.m_pan.y, spacing); y < m_extent.y; y += spacing)
            list.AddLine({m_origin.x, m_origin.y + y}, {m_origin.x + m_extent.x, m_origin.y + y}, color);
        list.PopClipRect();
    }

    void Canvas::Frame::append(const ImDrawList& source) const {
        ImDrawList& target = *ImGui::GetWindowDrawList();
        const unsigned int vertex = static_cast<unsigned int>(target.VtxBuffer.Size);
        const unsigned int index = static_cast<unsigned int>(target.IdxBuffer.Size);
        const int callback = target._CallbacksDataBuf.Size;
        const bool offsets = (m_canvas.m_host->IO.BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset) != 0;
        const float scale = m_canvas.m_scale;
        const ImVec2 origin = m_origin + m_canvas.m_pan - m_pan * (scale / m_scale);

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
            ImRect clip(origin + ImVec2(command.ClipRect.x, command.ClipRect.y) * scale,
                        origin + ImVec2(command.ClipRect.z, command.ClipRect.w) * scale);
            clip.ClipWithFull(m_clip);
            command.ClipRect = clip.ToVec4();
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
