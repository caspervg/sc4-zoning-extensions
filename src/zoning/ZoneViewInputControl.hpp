#pragma once

#include "cSC4BaseViewInputControl.h"
#include "zoning/ZoneToolState.hpp"

#include <string>

class cISC4City;
class cISC4NetworkTool;
class cISC4ZoneDeveloper;
class cISC4ZoneManager;
template <typename T> class SC4CellRegion;

class ZoneViewInputControl final : public cSC4BaseViewInputControl
{
public:
    static constexpr uint32_t kZoneViewInputControlID = 0x57C44A10;

    explicit ZoneViewInputControl(ZoneToolState& toolState);
    ~ZoneViewInputControl() override;

    bool Init() override;
    bool Shutdown() override;

    void Activate() override;
    void Deactivate() override;

    bool OnMouseDownL(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseUpL(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseMove(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseDownR(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseWheel(int32_t x, int32_t z, uint32_t modifiers, int32_t wheelDelta) override;
    bool OnMouseExit() override;
    bool OnKeyDown(int32_t vkCode, uint32_t modifiers) override;
    bool OnKeyUp(int32_t vkCode, uint32_t modifiers) override;

    bool ShouldStack() override;

    void SetZoneType(cISC4ZoneManager::ZoneType zoneType);

private:
    enum class ValidationFailure_
    {
        None = 0,
        TooSmall,
        TooLarge,
        TooSteep,
        InsufficientFunds,
    };

    bool TryGetServices_(cISC4City*& city, cISC4ZoneManager*& zoneManager, cISC4ZoneDeveloper*& zoneDeveloper) const;
    bool PickCell_(int32_t screenX, int32_t screenZ, int32_t& cellX, int32_t& cellZ) const;

    SC4CellRegion<long> BuildDeveloperRegion_() const;
    SC4CellRegion<int32_t> BuildZoneManagerRegion_() const;
    template <typename T>
    int ApplyPloppedLotMask_(SC4CellRegion<T>& region) const;
    template <typename T>
    bool HasAnyIncludedCells_(const SC4CellRegion<T>& region) const;
    bool ValidateSelection_(const ZoneToolSnapshot& snapshot,
                            cISC4City* city,
                            cISC4ZoneManager* zoneManager,
                            const SC4CellRegion<int32_t>& region,
                            std::string& outMessage,
                            ValidationFailure_& outFailure) const;
    void ShowInvalidSelectionOverlay_(cISC4City* city, const SC4CellRegion<int32_t>& region);
    void ClearInvalidSelectionOverlay_(cISC4City* city = nullptr);

    void UpdateCursorText_();
    void ClearCursorText_();
    bool UpdatePreview_();
    bool CommitSelection_();
    void ClearPreview_();
    void CancelDrag_();
    void ApplyZoneDeveloperOptions_(cISC4ZoneDeveloper* zoneDeveloper) const;
    cISC4NetworkTool* EnsureOverrideNetworkTool_(ZoneInternalNetworkMode mode);
    void ReleaseOverrideNetworkTool_();

private:
    ZoneToolState& toolState_;
    bool active_ = false;
    bool dragging_ = false;
    int32_t startCellX_ = 0;
    int32_t startCellZ_ = 0;
    int32_t currentCellX_ = 0;
    int32_t currentCellZ_ = 0;
    uint32_t modifiers_ = 0;
    std::string previewValidationMessage_;
    cISC4NetworkTool* overrideNetworkTool_ = nullptr;
    ZoneInternalNetworkMode overrideNetworkToolMode_ = ZoneInternalNetworkMode::Street;
    bool invalidSelectionOverlayActive_ = false;
    mutable uint64_t lastCostValidationLogKey_ = 0;
};
