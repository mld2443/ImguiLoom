module;

#include <algorithm>   // any_of, contains, find_if
#include <concepts>    // same_as
#include <cstddef>     // ptrdiff_t, size_t
#include <string>      // string
#include <type_traits> // remove_cvref_t
#include <utility>     // move, swap
#include <variant>     // get, holds_alternative, monostate, visit
#include <vector>      // erase, erase_if, vector

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
        std::vector<NodeStyle*> targets;
        for (Node* selected : m_selection) targets.push_back(&selected->style);
        for (auto& edit : m_styleEditor.draw(node.style, targets, defaults.node)) record(std::move(edit));
    }

    void Canvas::finishStyleEdit() {
        if (auto edit = m_styleEditor.finish()) record(std::move(*edit));
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
            else change.replay();
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

    bool Canvas::inputTitle(std::vector<char>& title) {
        return ImGui::InputText("Title", title.data(), title.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
            [](ImGuiInputTextCallbackData* data) {
                auto& buffer = *static_cast<std::vector<char>*>(data->UserData);
                buffer.resize(static_cast<std::size_t>(data->BufSize));
                data->Buf = buffer.data();
                return 0;
            }, &title);
    }

    //////////////////////////////
    // MARK: Canvas::PopupScope //
    //////////////////////////////

    Canvas::PopupScope::PopupScope(const char* name) : m_open(ImGui::BeginPopup(name)) {}
    Canvas::PopupScope::~PopupScope() { if (m_open) ImGui::EndPopup(); }
    Canvas::PopupScope::operator bool() const { return m_open; }

    /////////////////////////////
    // MARK: Canvas::MenuScope //
    /////////////////////////////

    Canvas::MenuScope::MenuScope(const char* name) : m_open(ImGui::BeginMenu(name)) {}
    Canvas::MenuScope::~MenuScope() { if (m_open) ImGui::EndMenu(); }
    Canvas::MenuScope::operator bool() const { return m_open; }
}
