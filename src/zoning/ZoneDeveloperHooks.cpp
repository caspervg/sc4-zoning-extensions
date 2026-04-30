#include "zoning/ZoneDeveloperHooks.hpp"

#include "cISC4City.h"
#include "cISC4NetworkTool.h"
#include "SC4CellRegion.h"
#include "SC4Vector.h"
#include "util/Logger.h"
#include "util/VersionDetection.h"
#include "zoning/ZoneToolState.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <mutex>
#include <new>

namespace {
    constexpr uint16_t kSupportedGameVersion = 641;

    constexpr uintptr_t kInvokeZoningToolAddress = 0x007E6DA0;
    constexpr size_t kInvokeZoningToolPatchBytes = 9;

    constexpr uintptr_t kDetermineLotSizeAddress = 0x00732BF0;
    constexpr uintptr_t kDetermineLotSizeCallSite = 0x00733954;
    constexpr uintptr_t kLayInJogStreetsAddress = 0x00730D30;
    constexpr size_t kLayInJogStreetsPatchBytes = 11;
    constexpr uintptr_t kDrawNetworkLineAddress = 0x0063AF40;
    constexpr size_t kDrawNetworkLinePatchBytes = 5;
    constexpr uintptr_t kPlaceAllSegmentsAddress = 0x0063BDC0;
    constexpr size_t kPlaceAllSegmentsPatchBytes = 11;
    constexpr size_t kZoneDeveloperInternalNetworkToolOffset = 0x48;
    constexpr size_t kZoneDeveloperStreetIntervalOffset = 0x94;

    constexpr uintptr_t kClearHighlightAddress = 0x0072C0E0;

    constexpr uintptr_t kGetIntersectionRuleAddress = 0x00625380;
    constexpr uintptr_t kDoAutoCompleteAddress = 0x006097E0;
    constexpr uintptr_t kInsertIsolatedHighwayIntersectionAddress = 0x0062CD50;
    constexpr uintptr_t kNetworkToolCtorAddress = 0x0062C5F0;
    constexpr size_t kNetworkToolSize = 0x368;

    constexpr size_t kCallPatchSize = 5;

    using InvokeZoningToolFn = void(__thiscall*)(cISC4View3DWin*, cISC4ZoneManager::ZoneType);
    using DetermineLotSizeFn = void(__thiscall*)(void*, void*);
    using LayInJogStreetsFn = void(__thiscall*)(void*, SC4CellRegion<long>*, int32_t);
    using DrawNetworkLineFn = uint32_t(__thiscall*)(cISC4NetworkTool*,
                                                    const SC4Point<uint32_t>&,
                                                    const SC4Point<uint32_t>&,
                                                    bool,
                                                    cISC4NetworkOccupant::eNetworkType);
    using PlaceAllSegmentsFn = void(__thiscall*)(cISC4NetworkTool*,
                                                 bool,
                                                 bool,
                                                 bool,
                                                 cISC4NetworkOccupant::eNetworkType);
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
    FunctionDetour gLayInJogStreetsDetour{
        kLayInJogStreetsAddress,
        kLayInJogStreetsPatchBytes,
        {},
        nullptr,
        false,
    };
    FunctionDetour gDrawNetworkLineDetour{
        kDrawNetworkLineAddress,
        kDrawNetworkLinePatchBytes,
        {},
        nullptr,
        false,
    };
    FunctionDetour gPlaceAllSegmentsDetour{
        kPlaceAllSegmentsAddress,
        kPlaceAllSegmentsPatchBytes,
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

    template <typename T>
    size_t VectorCountAt(void* base, const size_t beginOffset, const size_t endOffset) {
        const auto begin = reinterpret_cast<uintptr_t>(FieldAt<void*>(base, beginOffset));
        const auto end = reinterpret_cast<uintptr_t>(FieldAt<void*>(base, endOffset));
        if (end < begin) {
            return 0;
        }
        return static_cast<size_t>((end - begin) / sizeof(T));
    }

    std::string FormatPointVectorAt(void* base, const size_t beginOffset, const size_t endOffset, const size_t limit) {
        const auto* begin = reinterpret_cast<const SC4Point<uint32_t>*>(FieldAt<void*>(base, beginOffset));
        const auto* end = reinterpret_cast<const SC4Point<uint32_t>*>(FieldAt<void*>(base, endOffset));
        if (!begin || !end || end < begin) {
            return "[]";
        }

        std::ostringstream buffer;
        buffer << "[";
        size_t emitted = 0;
        for (auto* it = begin; it != end && emitted < limit; ++it, ++emitted) {
            if (emitted != 0) {
                buffer << ", ";
            }
            buffer << "(" << it->x << "," << it->y << ")";
        }
        if (begin + emitted != end) {
            buffer << ", ...";
        }
        buffer << "]";
        return buffer.str();
    }

    bool HasDiagonalStepAt(void* tool) {
        const auto* begin = reinterpret_cast<const SC4Point<uint32_t>*>(FieldAt<void*>(tool, 0x60));
        const auto* end = reinterpret_cast<const SC4Point<uint32_t>*>(tool ? FieldAt<void*>(tool, 0x64) : nullptr);
        if (!begin || !end || end <= begin) {
            return false;
        }

        const SC4Point<uint32_t>* previous = begin;
        for (auto* it = begin + 1; it != end; ++it) {
            const int32_t dx = static_cast<int32_t>(it->x) - static_cast<int32_t>(previous->x);
            const int32_t dz = static_cast<int32_t>(it->y) - static_cast<int32_t>(previous->y);
            if (dx != 0 && dz != 0) {
                return true;
            }
            previous = it;
        }

        return false;
    }

    bool ShouldTraceNetworkLine(cISC4NetworkTool* tool,
                                const SC4Point<uint32_t>& start,
                                const SC4Point<uint32_t>& end) {
        if (!tool) {
            return false;
        }

        const int32_t dx = static_cast<int32_t>(end.x) - static_cast<int32_t>(start.x);
        const int32_t dz = static_cast<int32_t>(end.y) - static_cast<int32_t>(start.y);
        return dx != 0 && dz != 0 && std::abs(dx) == std::abs(dz);
    }

    void LogNetworkToolState(const char* phase,
                             cISC4NetworkTool* tool,
                             const cISC4NetworkOccupant::eNetworkType networkType,
                             const bool previewMode,
                             const std::string& extra = {}) {
        if (!tool) {
            return;
        }

        const size_t draggedCount = VectorCountAt<SC4Point<uint32_t>>(tool, 0x60, 0x64);
        const size_t anchorCount = VectorCountAt<SC4Point<uint32_t>>(tool, 0x118, 0x11c);
        const uint32_t validSteps = FieldAt<uint32_t>(tool, 0x78);
        const bool previewEnabled = FieldAt<uint8_t>(tool, 0x234) != 0;
        const bool hasDiagonalStep = HasDiagonalStepAt(tool);

        LOG_INFO(
            "NetworkTrace {} tool=0x{:08X} type={} previewMode={} previewEnabled={} validSteps={} draggedCount={} anchorCount={} diagonalStep={} dragged={} anchors={} {}",
            phase,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tool)),
            static_cast<int>(networkType),
            previewMode,
            previewEnabled,
            validSteps,
            draggedCount,
            anchorCount,
            hasDiagonalStep,
            FormatPointVectorAt(tool, 0x60, 0x64, 8),
            FormatPointVectorAt(tool, 0x118, 0x11c, 8),
            extra);
    }

    cISC4NetworkOccupant::eNetworkType GetActiveNetworkType() {
        if (!gContext.toolState) {
            return cISC4NetworkOccupant::eNetworkType::Street;
        }

        switch (gContext.toolState->Snapshot().networkMode) {
        case ZoneInternalNetworkMode::Road:
            return cISC4NetworkOccupant::eNetworkType::Road;
        case ZoneInternalNetworkMode::Avenue:
            return cISC4NetworkOccupant::eNetworkType::Avenue;
        case ZoneInternalNetworkMode::OneWayRoad:
            return cISC4NetworkOccupant::eNetworkType::OneWayRoad;
        case ZoneInternalNetworkMode::Street:
        case ZoneInternalNetworkMode::None:
        default:
            return cISC4NetworkOccupant::eNetworkType::Street;
        }
    }

    bool IsDiagonalJogStreetPrototypeActive() {
        if (!gContext.toolState) {
            return false;
        }

        const ZoneToolSnapshot snapshot = gContext.toolState->Snapshot();
        return snapshot.toolActive && snapshot.diagonalMode && snapshot.networkMode != ZoneInternalNetworkMode::None;
    }

    bool IsDiagonalJogStreetPreviewPass() {
        if (!gContext.toolState) {
            return false;
        }

        return gContext.toolState->Snapshot().previewActive;
    }

    bool HasIncludedCell(const SC4CellRegion<long>& region, const long x, const long z) {
        if (x < region.bounds.topLeftX ||
            x > region.bounds.bottomRightX ||
            z < region.bounds.topLeftY ||
            z > region.bounds.bottomRightY) {
            return false;
        }

        return region.cellMap.GetValue(
            static_cast<uint32_t>(x - region.bounds.topLeftX),
            static_cast<uint32_t>(z - region.bounds.topLeftY));
    }

    bool TryGetDiagonalLineRange(const SC4CellRegion<long>& region,
                                 bool& outNorthWestToSouthEast,
                                 int32_t& outMinLine,
                                 int32_t& outMaxLine) {
        int64_t sumX = 0;
        int64_t sumZ = 0;
        int64_t cellCount = 0;

        for (long z = region.bounds.topLeftY; z <= region.bounds.bottomRightY; ++z) {
            for (long x = region.bounds.topLeftX; x <= region.bounds.bottomRightX; ++x) {
                if (!HasIncludedCell(region, x, z)) {
                    continue;
                }

                sumX += x;
                sumZ += z;
                ++cellCount;
            }
        }

        if (cellCount < 2) {
            return false;
        }

        int64_t covarianceNumerator = 0;
        for (long z = region.bounds.topLeftY; z <= region.bounds.bottomRightY; ++z) {
            for (long x = region.bounds.topLeftX; x <= region.bounds.bottomRightX; ++x) {
                if (!HasIncludedCell(region, x, z)) {
                    continue;
                }

                covarianceNumerator += (x * cellCount - sumX) * (z * cellCount - sumZ);
            }
        }

        outNorthWestToSouthEast = covarianceNumerator >= 0;

        bool first = true;
        int32_t minLine = 0;
        int32_t maxLine = 0;
        for (long z = region.bounds.topLeftY; z <= region.bounds.bottomRightY; ++z) {
            for (long x = region.bounds.topLeftX; x <= region.bounds.bottomRightX; ++x) {
                if (!HasIncludedCell(region, x, z)) {
                    continue;
                }

                const int32_t lineConstant = outNorthWestToSouthEast
                    ? static_cast<int32_t>(z - x)
                    : static_cast<int32_t>(x + z);

                if (first) {
                    minLine = lineConstant;
                    maxLine = lineConstant;
                    first = false;
                }
                else {
                    minLine = std::min(minLine, lineConstant);
                    maxLine = std::max(maxLine, lineConstant);
                }
            }
        }

        if (first) {
            return false;
        }

        outMinLine = minLine;
        outMaxLine = maxLine;
        return true;
    }

    bool TryGetDiagonalEndpoints(const SC4CellRegion<long>& region,
                                 const bool northWestToSouthEast,
                                 const int32_t lineConstant,
                                 int32_t& outStartX,
                                 int32_t& outStartZ,
                                 int32_t& outEndX,
                                 int32_t& outEndZ) {
        if (northWestToSouthEast) {
            const long startX = std::max<long>(region.bounds.topLeftX, region.bounds.topLeftY - lineConstant);
            const long endX = std::min<long>(region.bounds.bottomRightX, region.bounds.bottomRightY - lineConstant);
            if (startX > endX) {
                return false;
            }

            outStartX = static_cast<int32_t>(startX);
            outEndX = static_cast<int32_t>(endX);
            outStartZ = static_cast<int32_t>(startX + lineConstant);
            outEndZ = static_cast<int32_t>(endX + lineConstant);
            return true;
        }

        const long startX = std::max<long>(region.bounds.topLeftX, lineConstant - region.bounds.bottomRightY);
        const long endX = std::min<long>(region.bounds.bottomRightX, lineConstant - region.bounds.topLeftY);
        if (startX > endX) {
            return false;
        }

        outStartX = static_cast<int32_t>(startX);
        outEndX = static_cast<int32_t>(endX);
        outStartZ = static_cast<int32_t>(lineConstant - startX);
        outEndZ = static_cast<int32_t>(lineConstant - endX);
        return true;
    }

    void DrawDiagonalStreetLine(void* zoneDeveloper,
                                const SC4CellRegion<long>& region,
                                const bool northWestToSouthEast,
                                const int32_t lineConstant) {
        int32_t startX = 0;
        int32_t startZ = 0;
        int32_t endX = 0;
        int32_t endZ = 0;
        if (!TryGetDiagonalEndpoints(region, northWestToSouthEast, lineConstant, startX, startZ, endX, endZ)) {
            return;
        }

        if (std::abs(endX - startX) < 2 || std::abs(endZ - startZ) < 2) {
            return;
        }

        auto* networkTool = FieldAt<cISC4NetworkTool*>(zoneDeveloper, kZoneDeveloperInternalNetworkToolOffset);
        if (!networkTool) {
            return;
        }

        const cISC4NetworkOccupant::eNetworkType networkType = GetActiveNetworkType();
        const int32_t stepX = endX > startX ? 1 : (endX < startX ? -1 : 0);
        const int32_t stepZ = endZ > startZ ? 1 : (endZ < startZ ? -1 : 0);

        const bool previewPass = IsDiagonalJogStreetPreviewPass();
        networkTool->Reset();
        networkTool->EnablePreviewUpdate(true);

        SC4Point<uint32_t> clippedStart{
            static_cast<uint32_t>(startX),
            static_cast<uint32_t>(startZ),
        };
        SC4Point<uint32_t> clippedEnd{
            static_cast<uint32_t>(endX),
            static_cast<uint32_t>(endZ),
        };

        SC4Vector<SC4Point<uint32_t>> anchorPoints;
        anchorPoints.reserve(2);
        anchorPoints.push_back(clippedStart);
        anchorPoints.push_back(clippedEnd);
        networkTool->SetAnchorPoints(anchorPoints);

        const auto drawNetworkLine = reinterpret_cast<DrawNetworkLineFn>(kDrawNetworkLineAddress);
        const uint32_t drawResult = drawNetworkLine(networkTool, clippedStart, clippedEnd, true, networkType);
        if (drawResult == 0) {
            return;
        }

        const auto placeAllSegments = reinterpret_cast<PlaceAllSegmentsFn>(kPlaceAllSegmentsAddress);
        placeAllSegments(
            networkTool,
            !previewPass,
            previewPass,
            true,
            networkType);
    }

    void ExecuteDiagonalJogStreetPrototype(void* zoneDeveloper, SC4CellRegion<long>* region) {
        if (!zoneDeveloper || !region) {
            return;
        }

        bool northWestToSouthEast = true;
        int32_t minLine = 0;
        int32_t maxLine = 0;
        if (!TryGetDiagonalLineRange(*region, northWestToSouthEast, minLine, maxLine)) {
            return;
        }

        const int32_t configuredInterval = FieldAt<int32_t>(zoneDeveloper, kZoneDeveloperStreetIntervalOffset);
        const int32_t step = std::max(2, ClampStreetInterval(configuredInterval));
        if (maxLine - minLine < step * 2) {
            const int32_t centerLine = minLine + (maxLine - minLine) / 2;
            DrawDiagonalStreetLine(zoneDeveloper, *region, northWestToSouthEast, centerLine);
            return;
        }

        for (int32_t lineConstant = minLine + step; lineConstant < maxLine; lineConstant += step) {
            DrawDiagonalStreetLine(zoneDeveloper, *region, northWestToSouthEast, lineConstant);
        }
    }

    uint32_t __fastcall DrawNetworkLineHook(cISC4NetworkTool* self,
                                            void*,
                                            const SC4Point<uint32_t>& start,
                                            const SC4Point<uint32_t>& end,
                                            const bool useWorldCache,
                                            const cISC4NetworkOccupant::eNetworkType networkType) {
        const auto original = reinterpret_cast<DrawNetworkLineFn>(gDrawNetworkLineDetour.trampoline);
        if (!original) {
            return 0;
        }

        const uint32_t result = original(self, start, end, useWorldCache, networkType);
        if (ShouldTraceNetworkLine(self, start, end)) {
            std::ostringstream extra;
            extra << "start=(" << start.x << "," << start.y << ")"
                  << " end=(" << end.x << "," << end.y << ")"
                  << " useWorldCache=" << useWorldCache
                  << " result=" << result;
            LogNetworkToolState("DrawNetworkLine", self, networkType, true, extra.str());
        }
        return result;
    }

    void __fastcall PlaceAllSegmentsHook(cISC4NetworkTool* self,
                                         void*,
                                         const bool commit,
                                         const bool previewOnly,
                                         const bool allowDemolition,
                                         const cISC4NetworkOccupant::eNetworkType networkType) {
        const auto original = reinterpret_cast<PlaceAllSegmentsFn>(gPlaceAllSegmentsDetour.trampoline);
        if (!original) {
            return;
        }

        const bool shouldTrace = HasDiagonalStepAt(self);
        if (shouldTrace) {
            std::ostringstream extra;
            extra << "commit=" << commit
                  << " previewOnly=" << previewOnly
                  << " allowDemolition=" << allowDemolition;
            LogNetworkToolState("PlaceAllSegments:before", self, networkType, !commit, extra.str());
        }

        original(self, commit, previewOnly, allowDemolition, networkType);

        if (shouldTrace) {
            std::ostringstream extra;
            extra << "commit=" << commit
                  << " previewOnly=" << previewOnly
                  << " allowDemolition=" << allowDemolition;
            LogNetworkToolState("PlaceAllSegments:after", self, networkType, !commit, extra.str());
        }
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

    void __fastcall LayInJogStreetsHook(void* self, void*, SC4CellRegion<long>* region, const int32_t orientation) {
        if (!IsDiagonalJogStreetPrototypeActive()) {
            const auto original = reinterpret_cast<LayInJogStreetsFn>(gLayInJogStreetsDetour.trampoline);
            if (original) {
                original(self, region, orientation);
            }
            return;
        }

        // OnZone_ calls LayInJogStreets_ five times with side/orientation hints.
        // Run the diagonal prototype once on the first pass and suppress the
        // stock orthogonal planner for the remaining passes.
        if (orientation == 0x0B) {
            ExecuteDiagonalJogStreetPrototype(self, region);
        }
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
        !InstallFunctionDetour(gLayInJogStreetsDetour, reinterpret_cast<void*>(&LayInJogStreetsHook)) ||
        !InstallFunctionDetour(gDrawNetworkLineDetour, reinterpret_cast<void*>(&DrawNetworkLineHook)) ||
        !InstallFunctionDetour(gPlaceAllSegmentsDetour, reinterpret_cast<void*>(&PlaceAllSegmentsHook)) ||
        !InstallCallPatch(gDetermineLotSizePatch, reinterpret_cast<void*>(&DetermineLotSizeHook))) {
        UninstallCallPatch(gDetermineLotSizePatch);
        UninstallFunctionDetour(gPlaceAllSegmentsDetour);
        UninstallFunctionDetour(gDrawNetworkLineDetour);
        UninstallFunctionDetour(gLayInJogStreetsDetour);
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
    UninstallFunctionDetour(gPlaceAllSegmentsDetour);
    UninstallFunctionDetour(gDrawNetworkLineDetour);
    UninstallFunctionDetour(gLayInJogStreetsDetour);
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
