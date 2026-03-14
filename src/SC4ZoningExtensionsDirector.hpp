#pragma once

#include "cRZMessage2COMDirector.h"
#include "util/Settings.hpp"
#include "zoning/ZoneToolState.hpp"

#include <cstdint>

class cIGZImGuiService;
class cIGZMessage2;
class cIGZMessageServer2;
class cISC4View3DWin;
class ZoneStatusPanel;
class ZoneViewInputControl;

static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;
static constexpr uint32_t kSC4ZoningExtensionsDirectorId = 0x4A5F3C21;

class SC4ZoningExtensionsDirector final : public cRZMessage2COMDirector
{
public:
    ~SC4ZoningExtensionsDirector() override;

    [[nodiscard]] uint32_t GetDirectorID() const override;
    bool DoMessage(cIGZMessage2* message) override;
    bool OnStart(cIGZCOM* com) override;
    bool PostAppInit() override;
    bool PostAppShutdown() override;

private:
    static bool InvokeZoningToolCallback_(void* context, cISC4View3DWin* view3D, cISC4ZoneManager::ZoneType zoneType);

    bool HandleInvokeZoningTool_(cISC4View3DWin* view3D, cISC4ZoneManager::ZoneType zoneType);
    void PostCityInit_();
    void PreCityShutdown_();
    bool EnsureZoneControl_(cISC4View3DWin* view3D);
    void ReleaseZoneControl_();
    void RegisterStatusPanel_();
    void UnregisterStatusPanel_();
    void ReleaseRenderServices_();

    Settings settings_{};
    ZoneToolState toolState_{};
    cIGZMessageServer2* messageServer_ = nullptr;
    cIGZImGuiService* imguiService_ = nullptr;
    cISC4View3DWin* view3D_ = nullptr;
    ZoneViewInputControl* zoneControl_ = nullptr;
    ZoneStatusPanel* statusPanel_ = nullptr;
    bool hookRegistered_ = false;
    bool statusPanelRegistered_ = false;
};
