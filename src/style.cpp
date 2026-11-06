module;

#include <limits> // numeric_limits
#include <span>   // span
#include <vector> // vector

#include <imgui.h>

module loom;
import :style;

namespace loom {
    /////////////////////
    // MARK: NodeStyle //
    /////////////////////

    NodeStyle::Resolved NodeStyle::resolve(const NodeStyle& defaults) const {
        const ImGuiStyle& theme = ImGui::GetStyle();
        return {
            .header               = header.value_or(defaults.header.value_or(theme.Colors[ImGuiCol_TitleBg])),
            .headerText           = headerText.value_or(defaults.headerText.value_or(theme.Colors[ImGuiCol_Text])),
            .body                 = body.value_or(defaults.body.value_or(theme.Colors[ImGuiCol_WindowBg])),
            .border               = border.value_or(defaults.border.value_or(theme.Colors[ImGuiCol_Border])),
            .selectionBorder      = selectionBorder.value_or(defaults.selectionBorder.value_or(theme.Colors[ImGuiCol_NavCursor])),
            .padding              = padding.value_or(defaults.padding.value_or(theme.WindowPadding)),
            .rounding             = rounding.value_or(defaults.rounding.value_or(theme.WindowRounding)),
            .borderWidth          = borderWidth.value_or(defaults.borderWidth.value_or(theme.WindowBorderSize)),
            .selectionBorderWidth = selectionBorderWidth.value_or(defaults.selectionBorderWidth.value_or(2.0f))
        };
    }

    ///////////////////////
    // MARK: CanvasStyle //
    ///////////////////////

    CanvasStyle::Resolved CanvasStyle::resolve(const CanvasStyle& defaults) const {
        const ImGuiStyle& theme = ImGui::GetStyle();
        Color gridColor = theme.Colors[ImGuiCol_Border];
        gridColor.w *= 0.45f;
        return {
            .background = background.value_or(defaults.background.value_or(theme.Colors[ImGuiCol_ChildBg])),
            .grid       = grid.value_or(defaults.grid.value_or(gridColor))
        };
    }

    ////////////////////////
    // MARK: StyleControl //
    ////////////////////////

    bool StyleControl::color    (const char* label, Color&  value) { return ImGui::ColorEdit4(label, &value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar); }
    bool StyleControl::dimension(const char* label, ImVec2& value) { return ImGui::DragFloat2(label, &value.x, 0.1f, 0, std::numeric_limits<float>::max(), "%.1f", ImGuiSliderFlags_AlwaysClamp); }
    bool StyleControl::dimension(const char* label, float&  value) { return ImGui::DragFloat (label, &value,   0.1f, 0, std::numeric_limits<float>::max(), "%.1f", ImGuiSliderFlags_AlwaysClamp); }

    ///////////////////////////
    // MARK: NodeStyleEditor //
    ///////////////////////////

    std::vector<NodeStyleEditor::Edit> NodeStyleEditor::draw(const NodeStyle& displayed, std::span<NodeStyle* const> targets, const NodeStyle& defaults) {
        std::vector<Edit> completed;
        const auto appearance = displayed.resolve(defaults);
        property("Header",                 &NodeStyle::header,               appearance.header,               targets, completed, StyleControl::color);
        property("Header text",            &NodeStyle::headerText,           appearance.headerText,           targets, completed, StyleControl::color);
        property("Body",                   &NodeStyle::body,                 appearance.body,                 targets, completed, StyleControl::color);
        property("Border",                 &NodeStyle::border,               appearance.border,               targets, completed, StyleControl::color);
        property("Selection border",       &NodeStyle::selectionBorder,      appearance.selectionBorder,      targets, completed, StyleControl::color);
        property("Padding",                &NodeStyle::padding,              appearance.padding,              targets, completed, StyleControl::dimension);
        property("Rounding",               &NodeStyle::rounding,             appearance.rounding,             targets, completed, StyleControl::dimension);
        property("Border width",           &NodeStyle::borderWidth,          appearance.borderWidth,          targets, completed, StyleControl::dimension);
        property("Selection border width", &NodeStyle::selectionBorderWidth, appearance.selectionBorderWidth, targets, completed, StyleControl::dimension);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset style")) reset(targets, completed);
        return completed;
    }
}
