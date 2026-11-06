module;

#include <algorithm> // any_of
#include <optional>  // optional
#include <span>      // span
#include <utility>   // exchange, move, swap
#include <vector>    // vector

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>

export module loom:style;

export namespace loom {
    using Color = ImVec4;

    struct NodeStyle {
        struct Resolved {
            Color  header;
            Color  headerText;
            Color  body;
            Color  border;
            Color  selectionBorder;
            ImVec2 padding;
            float  rounding;
            float  borderWidth;
            float  selectionBorderWidth;
        };

        std::optional<Color>  header;
        std::optional<Color>  headerText;
        std::optional<Color>  body;
        std::optional<Color>  border;
        std::optional<Color>  selectionBorder;
        std::optional<ImVec2> padding;
        std::optional<float>  rounding;
        std::optional<float>  borderWidth;
        std::optional<float>  selectionBorderWidth;

        Resolved resolve(const NodeStyle& defaults) const;
        bool operator==(const NodeStyle&) const = default;
    };

    struct CanvasStyle {
        struct Resolved {
            Color background;
            Color grid;
        };

        std::optional<Color> background;
        std::optional<Color> grid;

        Resolved resolve(const CanvasStyle& defaults) const;
        bool operator==(const CanvasStyle&) const = default;
    };

    struct StyleDefaults {
        NodeStyle node;
        CanvasStyle canvas;
    };

    template<typename STYLE>
    struct Stylish {
        // Unset properties inherit from context defaults, then the current ImGui theme.
        STYLE style;

        // Direct application changes are unrecorded; reset resumes inheritance.
        void resetStyle() { style = {}; }
        auto resolve(const STYLE& defaults) const { return style.resolve(defaults); }
    };
}

namespace loom {
    class StyleControl {
    public:
        static bool color    (const char* label, Color&  value);
        static bool dimension(const char* label, ImVec2& value);
        static bool dimension(const char* label, float&  value);
    };

    template<typename STYLE>
    class StyleEditor {
    public:
        // Targets borrow stable style subobjects. Their owner retains them through history.
        struct Edit {
            struct Change { STYLE* target; STYLE before; };
            std::vector<Change> changes;

            void replay() {
                for (Change& change : changes) std::swap(*change.target, change.before);
            }
        };

        std::optional<Edit> finish() {
            if (!m_edit) return {};
            auto edit = std::exchange(m_edit, {});
            if (std::ranges::any_of(edit->changes, [](const auto& change) {
                    return change.before != *change.target;
                })) return edit;
            return {};
        }

    protected:
        template<typename T>
        void property(const char* label, std::optional<T> STYLE::* member, T value,
                      std::span<STYLE* const> targets, std::vector<Edit>& completed,
                      bool (*control)(const char*, T&)) {
            ImGui::PushID(label);
            ImGui::SetNextItemWidth(170);
            if (control(label, value)) {
                begin(targets);
                for (STYLE* target : targets) target->*member = value;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) complete(completed);
            ImGui::SameLine();
            ImGui::BeginDisabled(!std::ranges::any_of(targets, [member](const STYLE* target) { return (target->*member).has_value(); }));
            if (ImGui::SmallButton("Inherit")) {
                complete(completed);
                begin(targets);
                for (STYLE* target : targets) (target->*member).reset();
                complete(completed);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        void reset(std::span<STYLE* const> targets, std::vector<Edit>& completed) {
            complete(completed);
            begin(targets);
            for (STYLE* target : targets) *target = {};
            complete(completed);
        }

    private:
        void begin(std::span<STYLE* const> targets) {
            if (m_edit) return;
            Edit edit;
            for (STYLE* target : targets) edit.changes.push_back({.target = target, .before = *target});
            m_edit = std::move(edit);
        }
        void complete(std::vector<Edit>& completed) {
            if (auto edit = finish()) completed.push_back(std::move(*edit));
        }

        std::optional<Edit> m_edit;
    };

    class NodeStyleEditor : protected StyleEditor<NodeStyle> {
    public:
        using StyleEditor<NodeStyle>::Edit;
        using StyleEditor<NodeStyle>::finish;

        std::vector<Edit> draw(const NodeStyle& displayed, std::span<NodeStyle* const> targets, const NodeStyle& defaults);
    };
}
