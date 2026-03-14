#include "zoning/ZoneDeveloperHooks.hpp"

#include "cISC4City.h"
#include "cISC4NetworkTool.h"
#include "util/Logger.h"
#include "util/VersionDetection.h"
#include "zoning/ZoneToolState.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>

namespace {
    constexpr uint16_t kSupportedGameVersion = 641;

    constexpr uintptr_t kInvokeZoningToolAddress = 0x007E6DA0;
    constexpr size_t kInvokeZoningToolPatchBytes = 9;

    constexpr uintptr_t kDetermineLotSizeAddress = 0x00732BF0;
    constexpr uintptr_t kDetermineLotSizeCallSite = 0x00733954;
    constexpr size_t kZoneDeveloperInternalNetworkToolOffset = 0x48;

    constexpr uintptr_t kClearHighlightAddress = 0x0072C0E0;

    constexpr uintptr_t kGetIntersectionRuleAddress = 0x00625380;
    constexpr uintptr_t kDoAutoCompleteAddress = 0x006097E0;
    constexpr uintptr_t kInsertIsolatedHighwayIntersectionAddress = 0x0062CD50;
    constexpr uintptr_t kNetworkToolCtorAddress = 0x0062C5F0;
    constexpr size_t kNetworkToolSize = 0x368;

    constexpr size_t kCallPatchSize = 5;

    using InvokeZoningToolFn = void(__thiscall*)(cISC4View3DWin*, cISC4ZoneManager::ZoneType);
    using DetermineLotSizeFn = void(__thiscall*)(void*, void*);
    using ClearHighlightFn = void(__thiscall*)(void*);
    using GetIntersectionRuleFn = void*(__cdecl*)(uint32_t);
    using DoAutoCompleteFn = void(__thiscall*)(void*, int32_t, int32_t, cISC4NetworkTool*);
    using InsertIsolatedHighwayIntersectionFn = bool(__thiscall*)(cISC4NetworkTool*, int32_t, int32_t, uint32_t, bool);
    using NetworkToolCtorFn = void*(__thiscall*)(void*, int32_t);

    struct FunctionDetour {
        uintptr_t address = 0;
        size_t patchBytes = 0;
        std::array<uint8_t, 16> originalBytes{};
        void* trampoline = nullptr;
        bool installed = false;
    };

    struct CallSitePatch {
        uintptr_t address = 0;
        uintptr_t expectedTarget = 0;
        int32_t originalRel = 0;
        bool installed = false;
    };

    std::mutex gMutex;
    bool gInstalled = false;
    ZoneDeveloperHooks::HookContext gContext{};
    FunctionDetour gInvokeZoningToolDetour{
        kInvokeZoningToolAddress,
        kInvokeZoningToolPatchBytes,
        {},
        nullptr,
        false,
    };
    CallSitePatch gDetermineLotSizePatch{
        kDetermineLotSizeCallSite,
        kDetermineLotSizeAddress,
        0,
        false,
    };

    template <typename T>
    T& FieldAt(void* base, const size_t offset) {
        return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(base) + offset);
    }

    bool ComputeRelativeJump(const uintptr_t sourceAddress, const uintptr_t targetAddress, int32_t& relOut) {
        const auto delta = static_cast<intptr_t>(targetAddress) - static_cast<intptr_t>(sourceAddress + kCallPatchSize);
        if (delta < static_cast<intptr_t>(INT32_MIN) || delta > static_cast<intptr_t>(INT32_MAX)) {
            return false;
        }

        relOut = static_cast<int32_t>(delta);
        return true;
    }

    bool WriteJumpPatch(uint8_t* site, const size_t patchBytes, const void* target) {
        int32_t rel = 0;
        if (!ComputeRelativeJump(reinterpret_cast<uintptr_t>(site), reinterpret_cast<uintptr_t>(target), rel)) {
            return false;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(site, patchBytes, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }

        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, sizeof(rel));
        for (size_t i = kCallPatchSize; i < patchBytes; ++i) {
            site[i] = 0x90;
        }

        FlushInstructionCache(GetCurrentProcess(), site, patchBytes);
        VirtualProtect(site, patchBytes, oldProtect, &oldProtect);
        return true;
    }

    bool InstallFunctionDetour(FunctionDetour& detour, const void* hook) {
        if (detour.installed) {
            return true;
        }

        auto* site = reinterpret_cast<uint8_t*>(detour.address);
        std::memcpy(detour.originalBytes.data(), site, detour.patchBytes);

        auto* trampoline = static_cast<uint8_t*>(VirtualAlloc(
            nullptr,
            detour.patchBytes + kCallPatchSize,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (!trampoline) {
            return false;
        }

        std::memcpy(trampoline, site, detour.patchBytes);

        int32_t trampolineRel = 0;
        if (!ComputeRelativeJump(
            reinterpret_cast<uintptr_t>(trampoline + detour.patchBytes),
            detour.address + detour.patchBytes,
            trampolineRel)) {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        trampoline[detour.patchBytes] = 0xE9;
        std::memcpy(trampoline + detour.patchBytes + 1, &trampolineRel, sizeof(trampolineRel));

        if (!WriteJumpPatch(site, detour.patchBytes, hook)) {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        detour.trampoline = trampoline;
        detour.installed = true;
        return true;
    }

    void UninstallFunctionDetour(FunctionDetour& detour) {
        if (!detour.installed) {
            return;
        }

        auto* site = reinterpret_cast<uint8_t*>(detour.address);
        DWORD oldProtect = 0;
        if (VirtualProtect(site, detour.patchBytes, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memcpy(site, detour.originalBytes.data(), detour.patchBytes);
            FlushInstructionCache(GetCurrentProcess(), site, detour.patchBytes);
            VirtualProtect(site, detour.patchBytes, oldProtect, &oldProtect);
        }

        if (detour.trampoline) {
            VirtualFree(detour.trampoline, 0, MEM_RELEASE);
            detour.trampoline = nullptr;
        }

        detour.installed = false;
    }

    bool InstallCallPatch(CallSitePatch& patch, const void* hook) {
        if (patch.installed) {
            return true;
        }

        auto* site = reinterpret_cast<uint8_t*>(patch.address);
        if (site[0] != 0xE8) {
            LOG_ERROR("Zone hooks: expected CALL rel32 at 0x{:08X}", static_cast<uint32_t>(patch.address));
            return false;
        }

        std::memcpy(&patch.originalRel, site + 1, sizeof(patch.originalRel));
        const uintptr_t originalTarget = patch.address + kCallPatchSize + patch.originalRel;
        if (originalTarget != patch.expectedTarget) {
            LOG_ERROR(
                "Zone hooks: unexpected call target at 0x{:08X}, got 0x{:08X} expected 0x{:08X}",
                static_cast<uint32_t>(patch.address),
                static_cast<uint32_t>(originalTarget),
                static_cast<uint32_t>(patch.expectedTarget));
            patch.originalRel = 0;
            return false;
        }

        int32_t rel = 0;
        if (!ComputeRelativeJump(patch.address, reinterpret_cast<uintptr_t>(hook), rel)) {
            return false;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(site + 1, sizeof(rel), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }

        std::memcpy(site + 1, &rel, sizeof(rel));
        FlushInstructionCache(GetCurrentProcess(), site, kCallPatchSize);
        VirtualProtect(site + 1, sizeof(rel), oldProtect, &oldProtect);

        patch.installed = true;
        return true;
    }

    void UninstallCallPatch(CallSitePatch& patch) {
        if (!patch.installed) {
            return;
        }

        auto* site = reinterpret_cast<uint8_t*>(patch.address);
        DWORD oldProtect = 0;
        if (VirtualProtect(site + 1, sizeof(patch.originalRel), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memcpy(site + 1, &patch.originalRel, sizeof(patch.originalRel));
            FlushInstructionCache(GetCurrentProcess(), site, kCallPatchSize);
            VirtualProtect(site + 1, sizeof(patch.originalRel), oldProtect, &oldProtect);
        }

        patch.installed = false;
        patch.originalRel = 0;
    }

    int ClampParcelMetric(const int value) {
        return std::clamp(value, 1, 32);
    }

    int ClampStreetInterval(const int value) {
        return std::clamp(value, 0, 32);
    }

    void AdjustDetermineLotSizeResult(void* zoneDeveloper) {
        if (!zoneDeveloper || !gContext.toolState) {
            return;
        }

        const ZoneToolSnapshot snapshot = gContext.toolState->Snapshot();
        if (!snapshot.toolActive) {
            return;
        }

        int32_t& lotWidth = FieldAt<int32_t>(zoneDeveloper, 0x84);
        int32_t& lotHeight = FieldAt<int32_t>(zoneDeveloper, 0x88);
        int32_t& minWidth = FieldAt<int32_t>(zoneDeveloper, 0x8C);
        int32_t& minHeight = FieldAt<int32_t>(zoneDeveloper, 0x90);
        int32_t& streetInterval = FieldAt<int32_t>(zoneDeveloper, 0x94);

        lotWidth = ClampParcelMetric(snapshot.parcelWidth);
        lotHeight = ClampParcelMetric(snapshot.parcelLength);
        minWidth = lotWidth;
        minHeight = lotHeight;
        const int32_t requestedStreetInterval = ClampStreetInterval(snapshot.streetInterval);
        if (requestedStreetInterval == 0) {
            streetInterval = std::max(streetInterval, std::max(lotWidth, lotHeight) + 2);
        }
        else {
            streetInterval = requestedStreetInterval;
        }
    }

    void __fastcall DetermineLotSizeHook(void* self, void*, void* region) {
        const auto original = reinterpret_cast<DetermineLotSizeFn>(kDetermineLotSizeAddress);
        original(self, region);
        AdjustDetermineLotSizeResult(self);
    }

    void __fastcall InvokeZoningToolHook(cISC4View3DWin* view3D, void*, const cISC4ZoneManager::ZoneType zoneType) {
        bool handled = false;
        if (gContext.invokeZoningTool) {
            handled = gContext.invokeZoningTool(gContext.context, view3D, zoneType);
        }

        if (!handled) {
            const auto original = reinterpret_cast<InvokeZoningToolFn>(gInvokeZoningToolDetour.trampoline);
            if (original) {
                original(view3D, zoneType);
            }
        }
    }
}

bool ZoneDeveloperHooks::SupportsCurrentVersion() {
    return VersionDetection::GetInstance().GetGameVersion() == kSupportedGameVersion;
}

bool ZoneDeveloperHooks::Install(const HookContext& context) {
    std::lock_guard lock(gMutex);
    if (gInstalled) {
        return true;
    }

    if (!SupportsCurrentVersion()) {
        LOG_WARN("Zone hooks disabled for game version {}", VersionDetection::GetInstance().GetGameVersion());
        return false;
    }

    gContext = context;

    if (!InstallFunctionDetour(gInvokeZoningToolDetour, reinterpret_cast<void*>(&InvokeZoningToolHook)) ||
        !InstallCallPatch(gDetermineLotSizePatch, reinterpret_cast<void*>(&DetermineLotSizeHook))) {
        UninstallCallPatch(gDetermineLotSizePatch);
        UninstallFunctionDetour(gInvokeZoningToolDetour);
        gContext = {};
        return false;
    }

    gInstalled = true;
    LOG_INFO("Zone hooks installed");
    return true;
}

void ZoneDeveloperHooks::Uninstall() {
    std::lock_guard lock(gMutex);
    UninstallCallPatch(gDetermineLotSizePatch);
    UninstallFunctionDetour(gInvokeZoningToolDetour);
    gContext = {};
    gInstalled = false;
}

bool ZoneDeveloperHooks::IsInstalled() {
    std::lock_guard lock(gMutex);
    return gInstalled;
}

bool ZoneDeveloperHooks::ClearLiveHighlight(cISC4ZoneDeveloper* zoneDeveloper) {
    if (!zoneDeveloper || !SupportsCurrentVersion()) {
        return false;
    }

    const auto clearHighlight = reinterpret_cast<ClearHighlightFn>(kClearHighlightAddress);
    clearHighlight(zoneDeveloper);
    return true;
}

bool ZoneDeveloperHooks::CreateFreshNetworkTool(const int32_t networkType, cISC4NetworkTool*& outTool) {
    if (!SupportsCurrentVersion()) {
        return false;
    }

    DestroyFreshNetworkTool(outTool);

    void* rawMemory = ::operator new(kNetworkToolSize, std::nothrow);
    if (!rawMemory) {
        return false;
    }

    const auto ctor = reinterpret_cast<NetworkToolCtorFn>(kNetworkToolCtorAddress);
    auto* tool = reinterpret_cast<cISC4NetworkTool*>(ctor(rawMemory, networkType));
    if (!tool) {
        ::operator delete(rawMemory);
        return false;
    }

    tool->AddRef();
    outTool = tool;
    return true;
}

bool ZoneDeveloperHooks::InitFreshNetworkTool(cISC4NetworkTool* tool) {
    return tool && tool->Init();
}

void ZoneDeveloperHooks::ResetFreshNetworkTool(cISC4NetworkTool* tool) {
    if (tool) {
        tool->Reset();
    }
}

void ZoneDeveloperHooks::DestroyFreshNetworkTool(cISC4NetworkTool*& tool) {
    if (tool) {
        tool->Release();
        tool = nullptr;
    }
}

cISC4NetworkTool* ZoneDeveloperHooks::GetZoneDeveloperInternalNetworkTool(cISC4ZoneDeveloper* zoneDeveloper) {
    if (!zoneDeveloper || !SupportsCurrentVersion()) {
        return nullptr;
    }

    auto* self = reinterpret_cast<uint8_t*>(zoneDeveloper);
    return FieldAt<cISC4NetworkTool*>(self, kZoneDeveloperInternalNetworkToolOffset);
}

bool ZoneDeveloperHooks::SetZoneDeveloperInternalNetworkTool(cISC4ZoneDeveloper* zoneDeveloper,
                                                             cISC4NetworkTool* tool) {
    if (!zoneDeveloper || !SupportsCurrentVersion()) {
        return false;
    }

    auto* self = reinterpret_cast<uint8_t*>(zoneDeveloper);
    FieldAt<cISC4NetworkTool*>(self, kZoneDeveloperInternalNetworkToolOffset) = tool;
    return true;
}

bool ZoneDeveloperHooks::PlaceIntersectionByRuleId(cISC4City* city,
                                                   const int32_t cellX,
                                                   const int32_t cellZ,
                                                   const uint32_t ruleId,
                                                   const bool commit,
                                                   int32_t* outCost) {
    if (!city || !SupportsCurrentVersion()) {
        return false;
    }

    auto* networkManager = city->GetNetworkManager();
    if (!networkManager) {
        return false;
    }

    const auto getIntersectionRule = reinterpret_cast<GetIntersectionRuleFn>(kGetIntersectionRuleAddress);
    void* rule = getIntersectionRule(ruleId);
    if (!rule) {
        return false;
    }

    cISC4NetworkTool* tool = nullptr;
    if (!CreateFreshNetworkTool(cISC4NetworkOccupant::eNetworkType::Street, tool) || !tool) {
        return false;
    }

    bool success = false;
    if (InitFreshNetworkTool(tool)) {
        ResetFreshNetworkTool(tool);

        const auto insertIntersection = reinterpret_cast<InsertIsolatedHighwayIntersectionFn>(
            kInsertIsolatedHighwayIntersectionAddress);
        success = insertIntersection(tool, cellX, cellZ, ruleId, commit);

        if (outCost) {
            *outCost = success ? tool->GetCostOfSolution() : 0;
        }

        if (success && commit) {
            const auto doAutoComplete = reinterpret_cast<DoAutoCompleteFn>(kDoAutoCompleteAddress);
            doAutoComplete(networkManager, cellX, cellZ, tool);
        }
    }

    DestroyFreshNetworkTool(tool);
    return success;
}
