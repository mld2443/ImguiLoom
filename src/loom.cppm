module;

#include <concepts>   // derived_from
#include <cstddef>    // size_t
#include <cstdint>    // uint64_t
#include <functional> // function
#include <memory>     // make_unique, unique_ptr
#include <optional>   // optional
#include <stack>      // stack
#include <string>     // string
#include <utility>    // forward, move
#include <variant>    // monostate, variant
#include <vector>     // vector

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
        T& add(ImVec2 position, auto&&... args) {
            auto node = std::make_unique<T>(std::forward<decltype(args)>(args)...);
            node->position = position;
            T& result = *node;
            m_nodes.push_back({.node = std::move(node), .serial = m_nextSerial++});
            return result;
        }

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
        static auto resolve(const NodeStyle& node, const NodeStyle& canvas, auto member, auto fallback) -> decltype(fallback) {
            return (node.*member).value_or((canvas.*member).value_or(fallback));
        }

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
