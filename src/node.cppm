module;

#include <optional> // optional
#include <string>   // string
#include <utility>  // move

#include <imgui.h>

export module loom:node;

export namespace loom {
    // Unset properties inherit from the canvas, then from the current ImGui theme.
    struct NodeStyle {
        std::optional<ImVec4> header;
        std::optional<ImVec4> headerText;
        std::optional<ImVec4> body;
        std::optional<ImVec4> border;
        std::optional<ImVec4> selectionBorder;
        std::optional<ImVec2> padding;
        std::optional<float> rounding;
        std::optional<float> borderWidth;
        std::optional<float> selectionBorderWidth;
    };

    struct Node {
        explicit Node(std::string title) : title(std::move(title)), position{0, 0} {}

        Node(const Node&) = delete;
        Node(Node&&) = delete;
        Node& operator=(const Node&) = delete;
        Node& operator=(Node&&) = delete;
        virtual ~Node() = default;

        virtual void draw() = 0;

        std::string title;
        ImVec2      position;
        NodeStyle   style;
    };
}
