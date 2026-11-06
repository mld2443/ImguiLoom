module;

#include <string>  // string
#include <utility> // move

#include <imgui.h>

export module loom:node;
import :style;

export namespace loom {
    struct Node : Stylish<NodeStyle> {
        explicit Node(std::string title) : title(std::move(title)), position{0, 0} {}

        Node(const Node&) = delete;
        Node(Node&&) = delete;
        Node& operator=(const Node&) = delete;
        Node& operator=(Node&&) = delete;
        virtual ~Node() = default;

        virtual void draw() = 0;

        std::string title;
        ImVec2      position;
    };
}
