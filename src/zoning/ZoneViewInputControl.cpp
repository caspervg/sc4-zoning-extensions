// ReSharper disable CppDFAUnreachableFunctionCall
// ReSharper disable CppDFAConstantConditions
#include "zoning/ZoneViewInputControl.hpp"

#include "cIGZSndSys.h"
#include "cISC4App.h"
#include "cISC4BudgetSimulator.h"
#include "cISC4City.h"
#include "cISC4Lot.h"
#include "cISC4LotDeveloper.h"
#include "cISC4LotManager.h"
#include "cISC4NetworkTool.h"
#include "cISC4ZoneDeveloper.h"
#include "cISC4ZoneManager.h"
#include "cISTETerrain.h"
#include "cISTETerrainView.h"
#include "cRZBaseString.h"
#include "GZServPtrs.h"
#include "SC4CellRegion.h"
#include "SC4Point.h"
#include "util/Logger.h"
#include "util/VersionDetection.h"
#include "zoning/ZoneDeveloperHooks.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace {
    constexpr uint32_t kCursorTextId = 0x57C44A11;
    constexpr uint32_t kCursorTextPriority = 160;
    constexpr size_t kZoneSoundTableSize = 16;
    constexpr uint16_t kSupportedGameVersion = 641;
    constexpr size_t kZoneConstraintTableSize = 16;
    constexpr size_t kZoneMaxSlopeOffset = 0x198;
    constexpr size_t kZoneCostOffset = 0x1E0;
    constexpr size_t kZoneDestructionCostOffset = 0x260;
    constexpr uint32_t kZoneCursorId[kZoneSoundTableSize] = {
        0x42E434F7, 0x62E4352B, 0x016C4E26, 0x02E43524,
        0x82E4350C, 0xA16C4E2A, 0xC2E43505, 0x42E43517,
        0xC16C4E30, 0x22E43512, 0x02E43524, 0x42E434FF,
        0x62E43531, 0x02E43524, 0x02E4351E, 0x02E43524,
    };

    // Recovered from the stock cSC4ViewInputControlZone sound tables.
    constexpr uint32_t kZoneDragErrorSound[kZoneSoundTableSize] = {
        0x495A6D86, 0x495A6D86, 0x495A6D86, 0x495A6D86,
        0x495A6D86, 0x495A6D86, 0x495A6D86, 0x495A6D86,
        0x495A6D86, 0x495A6D86, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x495A6D86, 0x00000000,
    };

    constexpr uint32_t kZonePlopSound[kZoneSoundTableSize] = {
        0x8A5EC5B8, 0x8A5C329D, 0x8A5C329D, 0x8A5C329D,
        0x8A5C329D, 0x8A5C329D, 0x8A5C329D, 0x8A5C329D,
        0x8A5C329D, 0x8A5C329D, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x8A5C329D, 0x00000000,
    };

    constexpr uint32_t kZoneClickSound[kZoneSoundTableSize] = {
        0xCA5C328B, 0xCA5C328B, 0xCA5C328B, 0xCA5C328B,
        0xCA5C328B, 0xCA5C328B, 0xCA5C328B, 0xCA5C328B,
        0xCA5C328B, 0xCA5C328B, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0xCA5C328B, 0x00000000,
    };

    constexpr uint32_t kShiftModifierMask = 0x10000;
    constexpr uint32_t kControlModifierMask = 0x20000;
    constexpr uint32_t kAltModifierMask = 0x40000;

    bool IsShiftActive(const uint32_t modifiers) {
        if ((modifiers & kShiftModifierMask) != 0) {
            return true;
        }
        return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    }

    bool IsAltActive(const uint32_t modifiers) {
        if ((modifiers & kAltModifierMask) != 0) {
            return true;
        }
        return (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    }

    bool IsControlActive(const uint32_t modifiers) {
        if ((modifiers & kControlModifierMask) != 0) {
            return true;
        }
        return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    }

    uint32_t NormalizeModifiers(const uint32_t modifiers) {
        uint32_t value = modifiers & (kShiftModifierMask | kControlModifierMask | kAltModifierMask);

        if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
            value |= kShiftModifierMask;
        }
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) {
            value |= kControlModifierMask;
        }
        if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) {
            value |= kAltModifierMask;
        }

        return value;
    }

    int GetNetworkToolType(const ZoneInternalNetworkMode mode) {
        switch (mode) {
        case ZoneInternalNetworkMode::None:
            return -1;
        case ZoneInternalNetworkMode::Road:
            return 0;
        case ZoneInternalNetworkMode::Street:
            return 3;
        case ZoneInternalNetworkMode::Avenue:
            return 6;
        case ZoneInternalNetworkMode::OneWayRoad:
            return 10;
        }

        return 3;
    }

    uint32_t GetZoneSoundId(const uint32_t (&table)[kZoneSoundTableSize], const cISC4ZoneManager::ZoneType zoneType) {
        const auto index = static_cast<size_t>(zoneType);
        if (index >= kZoneSoundTableSize) {
            return 0;
        }

        return table[index];
    }

    uint32_t GetZoneCursorId(const cISC4ZoneManager::ZoneType zoneType) {
        return GetZoneSoundId(kZoneCursorId, zoneType);
    }

    template <typename T>
    T& FieldAt(void* base, const size_t offset) {
        return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(base) + offset);
    }

    template <typename T>
    const T& FieldAt(const void* base, const size_t offset) {
        return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(base) + offset);
    }

    bool TryGetZoneMaxSlope(const cISC4ZoneManager* zoneManager, const cISC4ZoneManager::ZoneType zoneType,
                            float& outValue) {
        const auto index = static_cast<size_t>(zoneType);
        if (!zoneManager ||
            VersionDetection::GetInstance().GetGameVersion() != kSupportedGameVersion ||
            index >= kZoneConstraintTableSize) {
            return false;
        }

        const auto& values = FieldAt<const float[kZoneConstraintTableSize]>(zoneManager, kZoneMaxSlopeOffset);
        outValue = values[index];
        return true;
    }

    bool TryGetZoneCost(const cISC4ZoneManager* zoneManager, const cISC4ZoneManager::ZoneType zoneType,
                        int64_t& outValue) {
        const auto index = static_cast<size_t>(zoneType);
        if (!zoneManager ||
            VersionDetection::GetInstance().GetGameVersion() != kSupportedGameVersion ||
            index >= kZoneConstraintTableSize) {
            return false;
        }

        const auto& values = FieldAt<const int64_t[kZoneConstraintTableSize]>(zoneManager, kZoneCostOffset);
        outValue = values[index];
        return true;
    }

    bool TryGetZoneDestructionCost(const cISC4ZoneManager* zoneManager, const cISC4ZoneManager::ZoneType zoneType,
                                   int64_t& outValue) {
        const auto index = static_cast<size_t>(zoneType);
        if (!zoneManager ||
            VersionDetection::GetInstance().GetGameVersion() != kSupportedGameVersion ||
            index >= kZoneConstraintTableSize) {
            return false;
        }

        const auto& values = FieldAt<const int64_t[kZoneConstraintTableSize]>(zoneManager, kZoneDestructionCostOffset);
        outValue = values[index];
        return true;
    }

    void AppendMessageLine(std::string& message, const std::string& line) {
        if (line.empty()) {
            return;
        }

        if (!message.empty()) {
            message.append("\n");
        }
        message.append(line);
    }

    std::string FormatPloppedExclusionMessage(const int maskedPloppedCellCount, const bool preview) {
        if (maskedPloppedCellCount <= 0) {
            return {};
        }

        char buffer[96] = {};
        std::snprintf(
            buffer,
            sizeof(buffer),
            preview ? "Excluding %d plopped tile%s" : "Excluded %d plopped tile%s",
            maskedPloppedCellCount,
            maskedPloppedCellCount == 1 ? "" : "s");
        return buffer;
    }

    bool PlaySoundId(const uint32_t soundId) {
        if (soundId == 0) {
            return false;
        }

        cIGZSndSysPtr soundSystem;
        if (!soundSystem || !soundSystem->GetEnabledFlag()) {
            return false;
        }

        // The stock zone VIC calls a six-argument sound helper with the shape
        // (1, soundId, 0, 0, 0). Mirror that through the public sound system.
        return soundSystem->DoEvent(1, soundId, 0, 0, 0) ||
            soundSystem->PostEvent(1, soundId, 0, 0, 0);
    }

    bool PlayZoneSound(const uint32_t (&table)[kZoneSoundTableSize], const cISC4ZoneManager::ZoneType zoneType) {
        return PlaySoundId(GetZoneSoundId(table, zoneType));
    }

    class ScopedZoneDeveloperNetworkToolOverride {
    public:
        ScopedZoneDeveloperNetworkToolOverride(cISC4ZoneDeveloper* zoneDeveloper, cISC4NetworkTool* replacementTool)
            : zoneDeveloper_(zoneDeveloper) {
            replacementTool_ = replacementTool;
            if (!zoneDeveloper_ || !replacementTool_) {
                return;
            }

            originalTool_ = ZoneDeveloperHooks::GetZoneDeveloperInternalNetworkTool(zoneDeveloper_);
            active_ = ZoneDeveloperHooks::SetZoneDeveloperInternalNetworkTool(zoneDeveloper_, replacementTool_);
        }

        ~ScopedZoneDeveloperNetworkToolOverride() {
            if (active_) {
                ZoneDeveloperHooks::SetZoneDeveloperInternalNetworkTool(zoneDeveloper_, originalTool_);
            }
        }

        ScopedZoneDeveloperNetworkToolOverride(const ScopedZoneDeveloperNetworkToolOverride&) = delete;
        ScopedZoneDeveloperNetworkToolOverride& operator=(const ScopedZoneDeveloperNetworkToolOverride&) = delete;

    private:
        cISC4ZoneDeveloper* zoneDeveloper_ = nullptr;
        cISC4NetworkTool* originalTool_ = nullptr;
        cISC4NetworkTool* replacementTool_ = nullptr;
        bool active_ = false;
    };
}

ZoneViewInputControl::ZoneViewInputControl(ZoneToolState& toolState)
    : cSC4BaseViewInputControl(kZoneViewInputControlID)
      , toolState_(toolState) {}

ZoneViewInputControl::~ZoneViewInputControl() = default;

bool ZoneViewInputControl::Init() {
    return cSC4BaseViewInputControl::Init();
}

bool ZoneViewInputControl::Shutdown() {
    CancelDrag_();
    active_ = false;
    toolState_.SetToolActive(false);
    ReleaseOverrideNetworkTool_();
    return cSC4BaseViewInputControl::Shutdown();
}

void ZoneViewInputControl::Activate() {
    cSC4BaseViewInputControl::Activate();
    active_ = true;
    modifiers_ = NormalizeModifiers(0);
    previewValidationMessage_.clear();
    toolState_.SetValidationMessage({});
    toolState_.SetToolActive(true);
    SetCursor(GetZoneCursorId(toolState_.Snapshot().zoneType));
    UpdateCursorText_();
}

void ZoneViewInputControl::Deactivate() {
    CancelDrag_();
    active_ = false;
    modifiers_ = 0;
    previewValidationMessage_.clear();
    toolState_.SetValidationMessage({});
    toolState_.SetToolActive(false);
    ReleaseOverrideNetworkTool_();
    ClearCursorText_();
    cSC4BaseViewInputControl::Deactivate();
}

bool ZoneViewInputControl::OnMouseDownL(const int32_t x, const int32_t z, const uint32_t modifiers) {
    if (!active_ || !IsOnTop()) {
        return false;
    }

    modifiers_ = NormalizeModifiers(modifiers);

    int32_t cellX = 0;
    int32_t cellZ = 0;
    if (!PickCell_(x, z, cellX, cellZ) || !SetCapture()) {
        return false;
    }

    dragging_ = true;
    startCellX_ = cellX;
    startCellZ_ = cellZ;
    currentCellX_ = cellX;
    currentCellZ_ = cellZ;
    previewValidationMessage_.clear();
    toolState_.SetValidationMessage({});
    PlayZoneSound(kZoneClickSound, toolState_.Snapshot().zoneType);
    UpdateCursorText_();
    UpdatePreview_();
    return true;
}

bool ZoneViewInputControl::OnMouseUpL(const int32_t x, const int32_t z, const uint32_t modifiers) {
    if (!active_ || !dragging_) {
        return false;
    }

    modifiers_ = NormalizeModifiers(modifiers);

    int32_t cellX = currentCellX_;
    int32_t cellZ = currentCellZ_;
    PickCell_(x, z, cellX, cellZ);
    currentCellX_ = cellX;
    currentCellZ_ = cellZ;

    const bool committed = CommitSelection_();
    CancelDrag_();
    return committed;
}

bool ZoneViewInputControl::OnMouseMove(const int32_t x, const int32_t z, const uint32_t modifiers) {
    if (!active_) {
        return false;
    }

    const uint32_t normalizedModifiers = NormalizeModifiers(modifiers);

    int32_t cellX = currentCellX_;
    int32_t cellZ = currentCellZ_;
    if (!PickCell_(x, z, cellX, cellZ)) {
        return false;
    }

    if (cellX == currentCellX_ &&
        cellZ == currentCellZ_ &&
        normalizedModifiers == modifiers_) {
        return true;
    }

    modifiers_ = normalizedModifiers;
    currentCellX_ = cellX;
    currentCellZ_ = cellZ;
    UpdateCursorText_();

    if (dragging_) {
        UpdatePreview_();
    }

    return true;
}

bool ZoneViewInputControl::OnMouseDownR(const int32_t, const int32_t, const uint32_t modifiers) {
    modifiers_ = NormalizeModifiers(modifiers);

    if (!active_) {
        return false;
    }

    if (dragging_) {
        CancelDrag_();
    }

    EndInput();
    return true;
}

bool ZoneViewInputControl::OnMouseWheel(const int32_t, const int32_t, const uint32_t modifiers,
                                        const int32_t wheelDelta) {
    (void)modifiers;
    (void)wheelDelta;

    return false;
}

bool ZoneViewInputControl::OnMouseExit() {
    if (!active_ || !IsOnTop()) {
        return false;
    }

    ClearCursorText_();
    return false;
}

bool ZoneViewInputControl::OnKeyDown(const int32_t vkCode, const uint32_t modifiers) {
    if (!active_) {
        return false;
    }

    modifiers_ = NormalizeModifiers(modifiers);

    if (vkCode == VK_SHIFT || vkCode == VK_CONTROL || vkCode == VK_MENU) {
        UpdateCursorText_();
        if (dragging_) {
            UpdatePreview_();
        }
        return false;
    }

    const ZoneToolSnapshot previousSnapshot = toolState_.Snapshot();
    bool handledAdjustment = false;

    switch (vkCode) {
    case VK_TAB:
        if (IsShiftActive(modifiers_)) {
            toolState_.CycleNetworkMode(-1);
        }
        else {
            toolState_.CycleNetworkMode(1);
        }
        handledAdjustment = true;
        break;
    case VK_OEM_MINUS:
    case VK_SUBTRACT:
        toolState_.AdjustParcelWidth(-1);
        handledAdjustment = true;
        break;
    case VK_OEM_PLUS:
    case VK_ADD:
        toolState_.AdjustParcelWidth(1);
        handledAdjustment = true;
        break;
    case VK_OEM_4:
        toolState_.AdjustParcelLength(-1);
        handledAdjustment = true;
        break;
    case VK_OEM_6:
        toolState_.AdjustParcelLength(1);
        handledAdjustment = true;
        break;
    default:
        break;
    }

    if (handledAdjustment) {
        if (dragging_) {
            ClearPreview_();
        }

        const ZoneToolSnapshot nextSnapshot = toolState_.Snapshot();
        if (previousSnapshot.networkMode != nextSnapshot.networkMode) {
            ReleaseOverrideNetworkTool_();
        }

        UpdateCursorText_();
        if (dragging_) {
            UpdatePreview_();
        }
        return true;
    }

    if (vkCode != VK_ESCAPE) {
        return false;
    }

    if (dragging_) {
        CancelDrag_();
    }
    else if (view3D) {
        view3D->RemoveCurrentViewInputControl(false);
    }

    return true;
}

bool ZoneViewInputControl::OnKeyUp(const int32_t vkCode, const uint32_t modifiers) {
    if (!active_) {
        return false;
    }

    modifiers_ = NormalizeModifiers(modifiers);
    if (vkCode == VK_SHIFT || vkCode == VK_CONTROL || vkCode == VK_MENU) {
        UpdateCursorText_();
        if (dragging_) {
            UpdatePreview_();
        }
    }

    return false;
}

bool ZoneViewInputControl::ShouldStack() {
    return true;
}

void ZoneViewInputControl::SetZoneType(const cISC4ZoneManager::ZoneType zoneType) {
    const ZoneToolSnapshot previousSnapshot = toolState_.Snapshot();
    toolState_.SetZoneType(zoneType);
    SetCursor(GetZoneCursorId(zoneType));

    if (previousSnapshot.zoneType != zoneType && dragging_) {
        CancelDrag_();
    }

    UpdateCursorText_();
}

bool ZoneViewInputControl::TryGetServices_(cISC4City*& city,
                                           cISC4ZoneManager*& zoneManager,
                                           cISC4ZoneDeveloper*& zoneDeveloper) const {
    city = nullptr;
    zoneManager = nullptr;
    zoneDeveloper = nullptr;

    cISC4AppPtr app;
    if (!app) {
        return false;
    }

    city = app->GetCity();
    if (!city) {
        return false;
    }

    zoneManager = city->GetZoneManager();
    zoneDeveloper = city->GetZoneDeveloper();
    return zoneManager && zoneDeveloper;
}

bool ZoneViewInputControl::PickCell_(const int32_t screenX, const int32_t screenZ, int32_t& cellX,
                                     int32_t& cellZ) const {
    if (!view3D) {
        return false;
    }

    cISC4AppPtr app;
    cISC4City* city = app ? app->GetCity() : nullptr;
    if (!city) {
        return false;
    }

    float world[3] = {0.0f, 0.0f, 0.0f};
    if (!view3D->PickTerrain(screenX, screenZ, world, false)) {
        return false;
    }

    int pickedCellX = 0;
    int pickedCellZ = 0;
    if (!city->PositionToCell(world[0], world[2], pickedCellX, pickedCellZ) ||
        !city->CellIsInBounds(pickedCellX, pickedCellZ)) {
        return false;
    }

    cellX = pickedCellX;
    cellZ = pickedCellZ;
    return true;
}

SC4CellRegion<long> ZoneViewInputControl::BuildDeveloperRegion_() const {
    const long minX = std::min<long>(startCellX_, currentCellX_);
    const long minZ = std::min<long>(startCellZ_, currentCellZ_);
    const long maxX = std::max<long>(startCellX_, currentCellX_);
    const long maxZ = std::max<long>(startCellZ_, currentCellZ_);

    SC4CellRegion<long> region(minX, minZ, maxX, maxZ, false);
    for (long z = minZ; z <= maxZ; ++z) {
        for (long x = minX; x <= maxX; ++x) {
            region.cellMap.SetValue(
                static_cast<uint32_t>(x - minX),
                static_cast<uint32_t>(z - minZ),
                true);
        }
    }
    return region;
}

SC4CellRegion<int32_t> ZoneViewInputControl::BuildZoneManagerRegion_() const {
    const int32_t minX = std::min(startCellX_, currentCellX_);
    const int32_t minZ = std::min(startCellZ_, currentCellZ_);
    const int32_t maxX = std::max(startCellX_, currentCellX_);
    const int32_t maxZ = std::max(startCellZ_, currentCellZ_);

    SC4CellRegion<int32_t> region(minX, minZ, maxX, maxZ, false);
    for (int32_t z = minZ; z <= maxZ; ++z) {
        for (int32_t x = minX; x <= maxX; ++x) {
            region.cellMap.SetValue(
                static_cast<uint32_t>(x - minX),
                static_cast<uint32_t>(z - minZ),
                true);
        }
    }
    return region;
}

template <typename T>
int ZoneViewInputControl::ApplyPloppedLotMask_(SC4CellRegion<T>& region) const {
    cISC4AppPtr app;
    cISC4City* city = app ? app->GetCity() : nullptr;
    if (!city) {
        return 0;
    }

    cISC4LotManager* lotManager = city->GetLotManager();
    if (!lotManager) {
        return 0;
    }

    int removedCellCount = 0;

    for (T z = region.bounds.topLeftY; z <= region.bounds.bottomRightY; ++z) {
        for (T x = region.bounds.topLeftX; x <= region.bounds.bottomRightX; ++x) {
            const uint32_t column = static_cast<uint32_t>(x - region.bounds.topLeftX);
            const uint32_t row = static_cast<uint32_t>(z - region.bounds.topLeftY);
            if (!region.cellMap.GetValue(column, row)) {
                continue;
            }

            cISC4Lot* lot = lotManager->GetLot(x, z, false);
            if (lot && lot->GetZoneType() == cISC4ZoneManager::ZoneType::Plopped) {
                region.cellMap.SetValue(column, row, false);
                removedCellCount++;
            }
        }
    }

    return removedCellCount;
}

template <typename T>
bool ZoneViewInputControl::HasAnyIncludedCells_(const SC4CellRegion<T>& region) const {
    for (T z = region.bounds.topLeftY; z <= region.bounds.bottomRightY; ++z) {
        for (T x = region.bounds.topLeftX; x <= region.bounds.bottomRightX; ++x) {
            if (region.cellMap.GetValue(
                static_cast<uint32_t>(x - region.bounds.topLeftX),
                static_cast<uint32_t>(z - region.bounds.topLeftY))) {
                return true;
            }
        }
    }

    return false;
}

bool ZoneViewInputControl::ValidateSelection_(const ZoneToolSnapshot& snapshot,
                                              cISC4City* city,
                                              cISC4ZoneManager* zoneManager,
                                              const SC4CellRegion<int32_t>& region,
                                              std::string& outMessage,
                                              ValidationFailure_& outFailure) const {
    outMessage.clear();
    outFailure = ValidationFailure_::None;

    if (!city || !zoneManager) {
        return false;
    }

    const int32_t width = region.bounds.bottomRightX - region.bounds.topLeftX + 1;
    const int32_t height = region.bounds.bottomRightY - region.bounds.topLeftY + 1;

    if (snapshot.zoneType != cISC4ZoneManager::ZoneType::None) {
        const int32_t minZoneSize = zoneManager->GetMinZoneSize(snapshot.zoneType);
        if (width < minZoneSize || height < minZoneSize) {
            char buffer[96] = {};
            std::snprintf(buffer, sizeof(buffer), "Selection is smaller than the minimum %dx%d.", minZoneSize,
                          minZoneSize);
            outMessage = buffer;
            outFailure = ValidationFailure_::TooSmall;
            return false;
        }

        const int32_t maxZoneSize = zoneManager->GetMaxZoneSize(snapshot.zoneType);
        if (width > maxZoneSize || height > maxZoneSize) {
            char buffer[96] = {};
            std::snprintf(buffer, sizeof(buffer), "Selection is larger than the maximum %dx%d.", maxZoneSize,
                          maxZoneSize);
            outMessage = buffer;
            outFailure = ValidationFailure_::TooLarge;
            return false;
        }

        cISC4LotDeveloper* lotDeveloper = city->GetLotDeveloper();
        float maxSlope = 0.0f;
        if (lotDeveloper &&
            TryGetZoneMaxSlope(zoneManager, snapshot.zoneType, maxSlope) &&
            lotDeveloper->GetAreaSlope(region.bounds, true) > maxSlope) {
            char buffer[112] = {};
            std::snprintf(buffer, sizeof(buffer), "Selection is too steep for this zone (max %.2f).", maxSlope);
            outMessage = buffer;
            outFailure = ValidationFailure_::TooSteep;
            return false;
        }
    }

    cISC4BudgetSimulator* budgetSimulator = city->GetBudgetSimulator();
    if (!budgetSimulator) {
        return true;
    }

    int64_t totalFunds = budgetSimulator->GetTotalFunds();
    int64_t newZoneCost = 0;
    if (snapshot.zoneType != cISC4ZoneManager::ZoneType::None &&
        !TryGetZoneCost(zoneManager, snapshot.zoneType, newZoneCost)) {
        newZoneCost = 0;
    }

    int64_t estimatedCost = 0;
    int64_t destructionCostTotal = 0;
    int includedCellCount = 0;
    int changedCellCount = 0;
    int sameZoneCellCount = 0;
    int demolitionCellCount = 0;
    for (int32_t z = region.bounds.topLeftY; z <= region.bounds.bottomRightY; ++z) {
        for (int32_t x = region.bounds.topLeftX; x <= region.bounds.bottomRightX; ++x) {
            if (!region.cellMap.GetValue(
                static_cast<uint32_t>(x - region.bounds.topLeftX),
                static_cast<uint32_t>(z - region.bounds.topLeftY))) {
                continue;
            }

            includedCellCount++;
            const auto existingZoneType = zoneManager->GetZoneType(x, z);
            if (existingZoneType == cISC4ZoneManager::ZoneType::Plopped) {
                continue;
            }

            if (existingZoneType == snapshot.zoneType) {
                sameZoneCellCount++;
                continue;
            }

            changedCellCount++;
            if (existingZoneType != cISC4ZoneManager::ZoneType::None) {
                int64_t destructionCost = 0;
                if (TryGetZoneDestructionCost(zoneManager, existingZoneType, destructionCost)) {
                    estimatedCost += destructionCost;
                    destructionCostTotal += destructionCost;
                }
                demolitionCellCount++;
            }

            if (snapshot.zoneType != cISC4ZoneManager::ZoneType::None) {
                estimatedCost += newZoneCost;
            }
        }
    }

    if (estimatedCost > totalFunds) {
        char buffer[128] = {};
        std::snprintf(buffer, sizeof(buffer), "Insufficient funds for zoning (§%lld needed).", estimatedCost);
        outMessage = buffer;
        outFailure = ValidationFailure_::InsufficientFunds;
        return false;
    }

    return true;
}

void ZoneViewInputControl::ShowInvalidSelectionOverlay_(cISC4City* city, const SC4CellRegion<int32_t>& region) {
    cISTETerrain* terrain = city ? city->GetTerrain() : nullptr;
    cISTETerrainView* terrainView = terrain ? terrain->GetView() : nullptr;
    if (!terrainView) {
        invalidSelectionOverlayActive_ = false;
        return;
    }

    SC4Rect<int> selectionRect(
        static_cast<int>(region.bounds.topLeftX),
        static_cast<int>(region.bounds.topLeftY),
        static_cast<int>(region.bounds.bottomRightX),
        static_cast<int>(region.bounds.bottomRightY));
    terrainView->MarkSelected(selectionRect, cISTETerrain::eHilightColorType::Red, true);
    invalidSelectionOverlayActive_ = true;
}

void ZoneViewInputControl::ClearInvalidSelectionOverlay_(cISC4City* city) {
    if (!invalidSelectionOverlayActive_) {
        return;
    }

    if (!city) {
        cISC4AppPtr app;
        city = app ? app->GetCity() : nullptr;
    }

    cISTETerrain* terrain = city ? city->GetTerrain() : nullptr;
    cISTETerrainView* terrainView = terrain ? terrain->GetView() : nullptr;
    if (terrainView) {
        terrainView->ClearCurrentSelections();
    }

    invalidSelectionOverlayActive_ = false;
}

void ZoneViewInputControl::UpdateCursorText_() {
    if (!view3D || !active_) {
        return;
    }

    toolState_.SetValidationMessage(previewValidationMessage_);

    ZoneToolTipText text = BuildZoneToolTipText(toolState_.Snapshot());
    if (!previewValidationMessage_.empty()) {
        text.body.append("\n");
        text.body.append(previewValidationMessage_);
    }

    cRZBaseString title(text.title.c_str());
    cRZBaseString body(text.body.c_str());
    view3D->SetCursorText(kCursorTextId, kCursorTextPriority, &title, &body, 0);
}

void ZoneViewInputControl::ClearCursorText_() {
    if (view3D) {
        view3D->ClearCursorText(kCursorTextId);
    }
}

bool ZoneViewInputControl::UpdatePreview_() {
    const ZoneToolSnapshot snapshot = toolState_.Snapshot();
    if (snapshot.zoneType == cISC4ZoneManager::ZoneType::Plopped) {
        return false;
    }

    cISC4City* city = nullptr;
    cISC4ZoneManager* zoneManager = nullptr;
    cISC4ZoneDeveloper* zoneDeveloper = nullptr;
    if (!TryGetServices_(city, zoneManager, zoneDeveloper)) {
        return false;
    }

    SC4CellRegion<int32_t> zoneRegion = BuildZoneManagerRegion_();
    SC4CellRegion<long> region = BuildDeveloperRegion_();
    int maskedPloppedCellCount = 0;
    if (snapshot.zoneType != cISC4ZoneManager::ZoneType::None) {
        maskedPloppedCellCount = ApplyPloppedLotMask_(zoneRegion);
        ApplyPloppedLotMask_(region);
        if (!HasAnyIncludedCells_(zoneRegion)) {
            previewValidationMessage_ = "Selection fully blocked by plopped lots";
            ReleaseOverrideNetworkTool_();
            ZoneDeveloperHooks::ClearLiveHighlight(zoneDeveloper);
            ShowInvalidSelectionOverlay_(city, zoneRegion);
            UpdateCursorText_();
            return true;
        }
    }

    std::string validationMessage;
    ValidationFailure_ validationFailure = ValidationFailure_::None;
    if (!ValidateSelection_(snapshot, city, zoneManager, zoneRegion, validationMessage, validationFailure)) {
        previewValidationMessage_ = validationMessage;
        AppendMessageLine(previewValidationMessage_, FormatPloppedExclusionMessage(maskedPloppedCellCount, true));
        ReleaseOverrideNetworkTool_();
        ZoneDeveloperHooks::ClearLiveHighlight(zoneDeveloper);
        ShowInvalidSelectionOverlay_(city, zoneRegion);
        UpdateCursorText_();
        return true;
    }

    ClearInvalidSelectionOverlay_(city);
    ZoneDeveloperHooks::ClearLiveHighlight(zoneDeveloper);
    ApplyZoneDeveloperOptions_(zoneDeveloper);
    const ScopedZoneDeveloperNetworkToolOverride networkOverride(
        zoneDeveloper,
        EnsureOverrideNetworkTool_(snapshot.networkMode));

    SC4Point<long> focusPoint{static_cast<long>(currentCellX_), static_cast<long>(currentCellZ_)};
    zoneDeveloper->HighlightParcels(region, snapshot.zoneType, &focusPoint, nullptr);
    if (validationFailure == ValidationFailure_::None) {
        previewValidationMessage_ = FormatPloppedExclusionMessage(maskedPloppedCellCount, true);
    }
    UpdateCursorText_();
    return true;
}

bool ZoneViewInputControl::CommitSelection_() {
    const ZoneToolSnapshot snapshot = toolState_.Snapshot();
    if (snapshot.zoneType == cISC4ZoneManager::ZoneType::Plopped) {
        if (view3D) {
            view3D->SetErrorReportString("Plopped zones are not supported.");
        }
        return false;
    }

    cISC4City* city = nullptr;
    cISC4ZoneManager* zoneManager = nullptr;
    cISC4ZoneDeveloper* zoneDeveloper = nullptr;
    if (!TryGetServices_(city, zoneManager, zoneDeveloper)) {
        return false;
    }
    (void)city;

    SC4CellRegion<int32_t> zoneRegion = BuildZoneManagerRegion_();
    SC4CellRegion<long> developerRegion = BuildDeveloperRegion_();
    int maskedPloppedCellCount = 0;
    if (snapshot.zoneType != cISC4ZoneManager::ZoneType::None) {
        maskedPloppedCellCount = ApplyPloppedLotMask_(zoneRegion);
        ApplyPloppedLotMask_(developerRegion);
        if (!HasAnyIncludedCells_(zoneRegion)) {
            previewValidationMessage_ = "Selection fully blocked by plopped lots";
            PlayZoneSound(kZoneDragErrorSound, snapshot.zoneType);
            if (view3D) {
                view3D->SetErrorReportString(previewValidationMessage_.c_str());
            }
            return false;
        }
    }

    std::string validationMessage;
    ValidationFailure_ validationFailure = ValidationFailure_::None;
    if (!ValidateSelection_(snapshot, city, zoneManager, zoneRegion, validationMessage, validationFailure)) {
        previewValidationMessage_ = validationMessage;
        AppendMessageLine(previewValidationMessage_, FormatPloppedExclusionMessage(maskedPloppedCellCount, false));
        PlayZoneSound(kZoneDragErrorSound, snapshot.zoneType);
        if (view3D) {
            view3D->SetErrorReportString(previewValidationMessage_.c_str());
        }
        return false;
    }

    previewValidationMessage_ = FormatPloppedExclusionMessage(maskedPloppedCellCount, false);

    ZoneDeveloperHooks::ClearLiveHighlight(zoneDeveloper);
    ApplyZoneDeveloperOptions_(zoneDeveloper);
    const ScopedZoneDeveloperNetworkToolOverride networkOverride(
        zoneDeveloper,
        EnsureOverrideNetworkTool_(snapshot.networkMode));

    int64_t zonedCellCount = 0;
    int32_t errorCode = 0;
    const bool placeZone = snapshot.zoneType != cISC4ZoneManager::ZoneType::None;
    const bool placed = zoneManager->PlaceZone(
        zoneRegion,
        snapshot.zoneType,
        placeZone,
        false,
        true,
        false,
        true,
        &zonedCellCount,
        &errorCode,
        0);

    if (!placed) {
        PlayZoneSound(kZoneDragErrorSound, snapshot.zoneType);
        if (view3D) {
            char errorBuffer[96] = {};
            std::snprintf(errorBuffer, sizeof(errorBuffer), "Zone placement failed (error %d).", errorCode);
            view3D->SetErrorReportString(errorBuffer);
        }
        return false;
    }

    PlayZoneSound(kZonePlopSound, snapshot.zoneType);

    if (placeZone) {
        zoneDeveloper->DoParcellization(developerRegion, snapshot.zoneType, true);
    }

    if (view3D && zonedCellCount == 0) {
        view3D->SetErrorReportString("Zone operation completed, but no cells changed.");
    }
    return true;
}

void ZoneViewInputControl::ClearPreview_() {
    cISC4City* city = nullptr;
    cISC4ZoneManager* zoneManager = nullptr;
    cISC4ZoneDeveloper* zoneDeveloper = nullptr;
    if (TryGetServices_(city, zoneManager, zoneDeveloper)) {
        ZoneDeveloperHooks::ClearLiveHighlight(zoneDeveloper);
    }
    ClearInvalidSelectionOverlay_(city);

    previewValidationMessage_.clear();
    toolState_.SetValidationMessage({});
    ClearCursorText_();
}

void ZoneViewInputControl::CancelDrag_() {
    ReleaseOverrideNetworkTool_();
    dragging_ = false;
    ReleaseCapture();
    ClearPreview_();
    UpdateCursorText_();
}

void ZoneViewInputControl::ApplyZoneDeveloperOptions_(cISC4ZoneDeveloper* zoneDeveloper) const {
    if (!zoneDeveloper) {
        return;
    }

    const ZoneToolSnapshot snapshot = toolState_.Snapshot();
    const bool alternateOrientation = IsAltActive(modifiers_);
    const bool placeStreets = snapshot.networkMode != ZoneInternalNetworkMode::None;
    const bool customZoneSize = IsControlActive(modifiers_);
    zoneDeveloper->SetOptions(alternateOrientation, placeStreets, customZoneSize);
}

cISC4NetworkTool* ZoneViewInputControl::EnsureOverrideNetworkTool_(const ZoneInternalNetworkMode mode) {
    if (mode == ZoneInternalNetworkMode::None) {
        ReleaseOverrideNetworkTool_();
        return nullptr;
    }

    if (overrideNetworkTool_ && overrideNetworkToolMode_ == mode) {
        return overrideNetworkTool_;
    }

    ReleaseOverrideNetworkTool_();

    cISC4NetworkTool* tool = nullptr;
    if (!ZoneDeveloperHooks::CreateFreshNetworkTool(GetNetworkToolType(mode), tool) || !tool) {
        return nullptr;
    }

    if (!ZoneDeveloperHooks::InitFreshNetworkTool(tool)) {
        ZoneDeveloperHooks::DestroyFreshNetworkTool(tool);
        return nullptr;
    }

    ZoneDeveloperHooks::ResetFreshNetworkTool(tool);
    overrideNetworkTool_ = tool;
    overrideNetworkToolMode_ = mode;
    return overrideNetworkTool_;
}

void ZoneViewInputControl::ReleaseOverrideNetworkTool_() {
    if (overrideNetworkTool_) {
        ZoneDeveloperHooks::ResetFreshNetworkTool(overrideNetworkTool_);
        ZoneDeveloperHooks::DestroyFreshNetworkTool(overrideNetworkTool_);
        overrideNetworkTool_ = nullptr;
    }
    overrideNetworkToolMode_ = ZoneInternalNetworkMode::Street;
}
