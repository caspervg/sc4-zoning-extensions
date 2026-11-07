#pragma once

#include "cIGZCanvasW32.h"
#include "cIGZWinProcFilterW32.h"
#include "cRZAutoRefCount.h"
#include "cSC4BaseViewInputControlEx.h"
#include "zoning/ZoneToolState.hpp"

#include <cstdint>
#include <string>

class cISC4City;
class cISC4NetworkTool;
class cISC4ZoneDeveloper;
class cISC4ZoneManager;
template <typename T> class SC4CellRegion;

class ZoneViewInputControl final : public cSC4BaseViewInputControlEx
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
    bool OnMouseWheel(int32_t x, int32_t z, uint32_t modifiers, int32_t wheelDelta) override;
    bool OnMouseExit() override;
    bool OnKeyDown(int32_t vkCode, uint32_t modifiers) override;
    bool OnKeyUp(int32_t vkCode, uint32_t modifiers) override;

    bool OnMouseDownM(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseUpM(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseDownX(int32_t x, int32_t z, uint32_t modifiers, uint32_t button) override;
    bool OnMouseUpX(int32_t x, int32_t z, uint32_t modifiers, uint32_t button) override;

    bool ShouldStack() override;

    void SetZoneType(cISC4ZoneManager::ZoneType zoneType);
    // Pushed onto the view and not shut down since; it may be below a vanilla tool.
    [[nodiscard]] bool IsActive() const { return active_; }

private:
    // Active and on top. Also marks the tool active again after a pause (see Deactivate).
    bool IsLive_();
    enum class ValidationFailure_
    {
        None = 0,
        TooSmall,
        TooLarge,
        TooSteep,
        InsufficientFunds,
    };

    enum class WheelTarget_ : uint8_t
    {
        None = 0,
        Width,
        Depth,
        StreetInterval,
        DiagonalThickness,
    };

    // Passes WM_MOUSEHWHEEL (tilt wheel) to the control; cSC4BaseViewInputControlEx only
    // forwards the middle and X buttons. Owned by the control, so the reference count is fixed.
    class HorizontalWheelFilter_ final : public cIGZWinProcFilterW32
    {
    public:
        explicit HorizontalWheelFilter_(ZoneViewInputControl& owner) : owner_(owner) {}

        bool QueryInterface(uint32_t riid, void** ppvObj) override;
        uint32_t AddRef() override { return 1; }
        uint32_t Release() override { return 1; }
        LRESULT FilterMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool& handled) override;

    private:
        ZoneViewInputControl& owner_;
    };

    bool TryGetServices_(cISC4City*& city, cISC4ZoneManager*& zoneManager, cISC4ZoneDeveloper*& zoneDeveloper) const;
    bool PickCell_(int32_t screenX, int32_t screenZ, int32_t& cellX, int32_t& cellZ) const;
    bool IsDiagonalModeActive_(const ZoneToolSnapshot& snapshot) const;
    void SyncDiagonalState_() noexcept;

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
    // Shows a changed width, depth, interval or network: redraws the preview and cursor text.
    void ApplyAdjustment_(const ZoneToolSnapshot& previousSnapshot);
    void ApplyWheelStep_(WheelTarget_ target, int steps, const ZoneToolSnapshot& previousSnapshot);
    bool IsControlButtonHeld_(ZoneMouseButton button) const;
    bool OnControlButtonDown_(ZoneMouseButton button);
    bool OnControlButtonUp_(ZoneMouseButton button);
    bool IsCursorOverView_(int32_t clientX, int32_t clientZ) const;
    bool OnMouseWheelHorizontal_(int32_t wheelDelta);
    void ResetWheelState_() noexcept;
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
    bool diagonalMode_ = false;
    int diagonalThickness_ = 1;
    bool invalidSelectionOverlayActive_ = false;

    int wheelAccumulator_ = 0;
    WheelTarget_ wheelTarget_ = WheelTarget_::None;
    // Bit per ZoneMouseButton turned with the wheel while held; its release is then no preset click.
    uint8_t wheelUsedButtons_ = 0;
    int horizontalWheelAccumulator_ = 0;
    HorizontalWheelFilter_ horizontalWheelFilter_{*this};
    cRZAutoRefCount<cIGZCanvasW32> canvasW32_;
};
