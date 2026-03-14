#pragma once

#include "cISC4NetworkOccupant.h"
#include "cISC4ZoneManager.h"

#include <cstdint>

class cISC4City;
class cISC4NetworkTool;
class cISC4View3DWin;
class cISC4ZoneDeveloper;
class ZoneToolState;

namespace ZoneDeveloperHooks {
    using InvokeZoningToolCb = bool(*)(void* context, cISC4View3DWin* view3D, cISC4ZoneManager::ZoneType zoneType)
    ;

    struct HookContext {
        void* context = nullptr;
        InvokeZoningToolCb invokeZoningTool = nullptr;
        ZoneToolState* toolState = nullptr;
    };

    bool SupportsCurrentVersion();
    bool Install(const HookContext& context);
    void Uninstall();
    bool IsInstalled();

    bool ClearLiveHighlight(cISC4ZoneDeveloper* zoneDeveloper);

    bool CreateFreshNetworkTool(int32_t networkType, cISC4NetworkTool*& outTool);
    bool InitFreshNetworkTool(cISC4NetworkTool* tool);
    void ResetFreshNetworkTool(cISC4NetworkTool* tool);
    void DestroyFreshNetworkTool(cISC4NetworkTool*& tool);
    cISC4NetworkTool* GetZoneDeveloperInternalNetworkTool(cISC4ZoneDeveloper* zoneDeveloper);
    bool SetZoneDeveloperInternalNetworkTool(cISC4ZoneDeveloper* zoneDeveloper, cISC4NetworkTool* tool);
    bool PlaceIntersectionByRuleId(cISC4City* city, int32_t cellX, int32_t cellZ, uint32_t ruleId, bool commit,
                                   int32_t* outCost = nullptr);
}
