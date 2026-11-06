module;

#include <limits> // numeric_limits

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>

module loom;
import :context;

namespace loom {
    ///////////////////
    // MARK: Context //
    ///////////////////

    Context::Context() : m_host(ImGui::GetCurrentContext()), m_context(createContext(m_host)) {}
    Context::~Context() { ImGui::DestroyContext(m_context); }

    ImGuiContext* Context::createContext(ImGuiContext* host) {
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

    /////////////////////////////////
    // MARK: Context::ContextScope //
    /////////////////////////////////

    Context::ContextScope::ContextScope(ImGuiContext* context) : m_previous(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(context);
    }
    Context::ContextScope::~ContextScope() { ImGui::SetCurrentContext(m_previous); }

    //////////////////////////
    // MARK: Context::Frame //
    //////////////////////////

    Context::Frame::Frame(ImGuiContext* context, ImVec2 pan, float scale, ImVec4 background)
        : initialPan(pan), initialScale(scale), m_host(ImGui::GetCurrentContext()), m_context(context) {
        ImGui::PushID(m_context);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
        const bool visible = ImGui::BeginChild("##canvas", {0, 0}, ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor();
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
        // Retained windows are already positioned; clip their hit bounds to this viewport
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

    Context::Frame::~Frame() {
        ImGui::SetCurrentContext(m_context);
        // An interrupted draw still closes its ImGui scopes, without rendering it.
        if (root) ImGui::End();
        if (m_context->WithinFrameScope) ImGui::EndFrame();
        ImGui::SetCurrentContext(m_host);
        ImGui::EndChild();
        ImGui::PopID();
    }

    Context::Frame::operator bool() const { return root != nullptr; }

    void Context::Frame::render(ImVec2 pan, float scale) {
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

    void Context::Frame::forwardInput() {
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

    void Context::Frame::forwardRequests(ImVec2 pan, float scale) {
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

    void Context::Frame::append(const ImDrawList& source, ImVec2 pan, float scale) const {
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
}
