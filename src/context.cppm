module;

#include <imgui.h>
#include <imgui_internal.h>

export module loom:context;

namespace loom {
    class Context {
    protected:
        Context();
        Context(const Context&) = delete;
        Context(Context&&) = delete;
        Context& operator=(const Context&) = delete;
        Context& operator=(Context&&) = delete;
        ~Context();

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

        ImGuiContext* const m_host;
        ImGuiContext* const m_context; // Owned; derived state is destroyed before this context.

    private:
        static ImGuiContext* createContext(ImGuiContext* host);
    };
}
