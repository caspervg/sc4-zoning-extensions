#include "zoning/ZoneStatusPanel.hpp"

#include "imgui.h"
#include "zoning/ZoneToolState.hpp"

ZoneStatusPanel::ZoneStatusPanel(ZoneToolState& toolState)
    : toolState_(toolState)
{
}

void ZoneStatusPanel::OnRender()
{
    const ZoneToolSnapshot snapshot = toolState_.Snapshot();
    if (!snapshot.toolActive) {
        return;
    }

    ImGui::SetNextWindowBgAlpha(0.7f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoInputs;

    if (!ImGui::Begin("##ZoneStatus", nullptr, kFlags)) {
        ImGui::End();
        return;
    }

    const ZoneToolStatusText text = BuildZoneToolStatusText(snapshot);
    ImGui::TextUnformatted(text.zoneLine.c_str());
    ImGui::TextUnformatted(text.parcelLine.c_str());
    ImGui::TextUnformatted(text.streetIntervalLine.c_str());
    if (!snapshot.validationMessage.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
        ImGui::TextWrapped("%s", snapshot.validationMessage.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::TextUnformatted(text.modifiersLine.c_str());
    if (!text.wheelLine.empty()) {
        ImGui::TextUnformatted(text.wheelLine.c_str());
    }
    ImGui::PopStyleColor();

    ImGui::End();
}

void ZoneStatusPanel::OnShutdown()
{
    delete this;
}
