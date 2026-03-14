#pragma once

#include "public/ImGuiPanel.h"

class ZoneToolState;

class ZoneStatusPanel final : public ImGuiPanel
{
public:
    explicit ZoneStatusPanel(ZoneToolState& toolState);

    void OnRender() override;
    void OnShutdown() override;

private:
    ZoneToolState& toolState_;
};
