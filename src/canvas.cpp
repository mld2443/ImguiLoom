module;

#include <algorithm> // any_of, clamp, contains
#include <cmath>     // fmod, pow
#include <cstddef>   // size_t
#include <format>    // format, format_to
#include <string>    // string
#include <utility>   // move
#include <variant>   // get_if, holds_alternative, monostate
#include <vector>    // erase

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>

module loom;

namespace loom {
    //////////////////
    // MARK: Canvas //
    //////////////////

    Canvas::Canvas() : m_host(ImGui::GetCurrentContext()), m_context(createContext(m_host)) {}
    Canvas::~Canvas() {
        m_redo = {};
        m_undo = {};
        m_nodes.clear();
        ImGui::DestroyContext(m_context);
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

    /////////////////////////////
    // MARK: Canvas::NodeFrame //
    /////////////////////////////

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
}
