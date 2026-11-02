module;

#include <print>

export module loom;

export namespace loom {
    class Canvas {
    public:
        Canvas();
    };
}


//////////////////////////
// MARK: Implementation //
//////////////////////////

namespace loom {
    Canvas::Canvas() {
        // Temporary integration evidence until the canvas has visible behavior.
        std::println("Loom canvas created.");
    }
}
