#pragma once

#include "cISC4ZoneManager.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum class ZoneInternalNetworkMode : uint8_t
{
    None = 0,
    Road = 1,
    Street = 2,
    Avenue = 3,
    OneWayRoad = 4,
};

enum class ZoneMouseButton : uint8_t
{
    None = 0,
    Middle = 1,
    XButton1 = 2,
    XButton2 = 3,
};

enum class ZoneVerticalWheelMode : uint8_t
{
    Zoom = 0,
    DepthWhileDragging = 1,
    Depth = 2,
};

// [Controls] in the INI. A wheel button turns the wheel into that adjustment while it is held;
// a click on a preset button (without turning the wheel) cycles the zone's presets.
struct ZoneControlSettings
{
    ZoneVerticalWheelMode verticalWheel = ZoneVerticalWheelMode::DepthWhileDragging;
    bool horizontalWheel = true;
    // 0 = one width step per tilt; otherwise one step per this many wheel units while tilted.
    int horizontalWheelUnitsPerStep = 0;
    ZoneMouseButton zoomButton = ZoneMouseButton::XButton2;
    ZoneMouseButton widthWheelButton = ZoneMouseButton::None;
    ZoneMouseButton depthWheelButton = ZoneMouseButton::None;
    ZoneMouseButton streetIntervalWheelButton = ZoneMouseButton::None;
    ZoneMouseButton presetPreviousButton = ZoneMouseButton::XButton1;
    ZoneMouseButton presetNextButton = ZoneMouseButton::XButton2;
};

struct ZoneParcelPreset
{
    int width = 1;
    int depth = 1;
};

struct ZoneToolSnapshot
{
    cISC4ZoneManager::ZoneType zoneType = cISC4ZoneManager::ZoneType::ResidentialLowDensity;
    ZoneInternalNetworkMode networkMode = ZoneInternalNetworkMode::Street;
    int parcelWidth = 1;
    int parcelLength = 2;
    int streetInterval = 0;
    bool diagonalSupported = false;
    bool diagonalMode = false;
    int diagonalThickness = 1;
    ZoneControlSettings controls{};
    // Index into the zone type's presets that matches the parcel size, or -1.
    int presetIndex = -1;
    int presetCount = 0;
    bool toolActive = false;
    bool previewActive = false;
    std::string validationMessage;
};

struct ZoneTypeDefaults
{
    int parcelWidth = 1;
    int parcelLength = 2;
    int streetInterval = 0;
    ZoneInternalNetworkMode networkMode = ZoneInternalNetworkMode::Street;
    std::vector<ZoneParcelPreset> presets;
};

using ZoneTypeDefaultsTable = std::array<ZoneTypeDefaults, 16>;

struct ZoneToolTipText
{
    std::string title;
    std::string body;
};

struct ZoneToolStatusText
{
    std::string zoneLine;
    std::string parcelLine;
    std::string streetIntervalLine;
    std::string modifiersLine;
    std::string controlsLine;
    std::string wheelLine;
};

class ZoneToolState
{
public:
    [[nodiscard]] const ZoneToolSnapshot& Snapshot() const noexcept;

    void SetZoneDefaults(const ZoneTypeDefaultsTable& defaults);
    // Keeps the current size as the zone type's starting size; switching zone types does this too.
    void RememberCurrentSize() noexcept;
    [[nodiscard]] const ZoneTypeDefaultsTable& ZoneDefaults() const noexcept;
    void SetControls(const ZoneControlSettings& controls) noexcept;
    void SetZoneType(cISC4ZoneManager::ZoneType zoneType) noexcept;
    void SetToolActive(bool value) noexcept;
    void SetPreviewActive(bool value) noexcept;
    void SetValidationMessage(std::string value) noexcept;
    void SetDiagonalState(bool supported, bool enabled, int thickness) noexcept;
    void CycleNetworkMode(int delta) noexcept;
    void AdjustParcelWidth(int delta) noexcept;
    void AdjustParcelLength(int delta) noexcept;
    void AdjustStreetInterval(int delta) noexcept;
    // Steps to the next (delta > 0) or previous preset of the zone type; false if it has none.
    bool CyclePreset(int delta) noexcept;

private:
    static int ClampParcelMetric_(int value) noexcept;
    static int ClampStreetInterval_(int value) noexcept;
    void ApplyDefaultsForCurrentZoneType_() noexcept;
    void SyncPresetIndex_() noexcept;

    ZoneToolSnapshot snapshot_{};
    ZoneTypeDefaultsTable zoneDefaults_{};
};

const char* GetZoneTypeLabel(cISC4ZoneManager::ZoneType zoneType) noexcept;
const char* GetZoneTypeShortLabel(cISC4ZoneManager::ZoneType zoneType) noexcept;
const char* GetZoneNetworkModeLabel(ZoneInternalNetworkMode mode) noexcept;
const char* GetZoneNetworkModeShortLabel(ZoneInternalNetworkMode mode) noexcept;

ZoneToolTipText BuildZoneToolTipText(const ZoneToolSnapshot& snapshot);
ZoneToolStatusText BuildZoneToolStatusText(const ZoneToolSnapshot& snapshot);
