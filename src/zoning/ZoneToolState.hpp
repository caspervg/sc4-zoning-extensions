#pragma once

#include "cISC4ZoneManager.h"

#include <array>
#include <cstdint>
#include <string>

enum class ZoneInternalNetworkMode : uint8_t
{
    None = 0,
    Road = 1,
    Street = 2,
    Avenue = 3,
    OneWayRoad = 4,
};

struct ZoneToolSnapshot
{
    cISC4ZoneManager::ZoneType zoneType = cISC4ZoneManager::ZoneType::ResidentialLowDensity;
    ZoneInternalNetworkMode networkMode = ZoneInternalNetworkMode::Street;
    int parcelWidth = 3;
    int parcelLength = 3;
    int streetInterval = 0;
    bool toolActive = false;
    std::string validationMessage;
};

struct ZoneTypeDefaults
{
    int parcelWidth = 3;
    int parcelLength = 3;
    int streetInterval = 0;
    ZoneInternalNetworkMode networkMode = ZoneInternalNetworkMode::Street;
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
    std::string wheelLine;
};

class ZoneToolState
{
public:
    [[nodiscard]] const ZoneToolSnapshot& Snapshot() const noexcept;

    void SetZoneDefaults(const ZoneTypeDefaultsTable& defaults) noexcept;
    void SetZoneType(cISC4ZoneManager::ZoneType zoneType) noexcept;
    void SetToolActive(bool value) noexcept;
    void SetValidationMessage(std::string value) noexcept;
    void CycleNetworkMode(int delta) noexcept;
    void AdjustParcelWidth(int delta) noexcept;
    void AdjustParcelLength(int delta) noexcept;
    void AdjustStreetInterval(int delta) noexcept;

private:
    static int ClampParcelMetric_(int value) noexcept;
    static int ClampStreetInterval_(int value) noexcept;
    void ApplyDefaultsForCurrentZoneType_() noexcept;

    ZoneToolSnapshot snapshot_{};
    ZoneTypeDefaultsTable zoneDefaults_{};
};

const char* GetZoneTypeLabel(cISC4ZoneManager::ZoneType zoneType) noexcept;
const char* GetZoneTypeShortLabel(cISC4ZoneManager::ZoneType zoneType) noexcept;
const char* GetZoneNetworkModeLabel(ZoneInternalNetworkMode mode) noexcept;
const char* GetZoneNetworkModeShortLabel(ZoneInternalNetworkMode mode) noexcept;

ZoneToolTipText BuildZoneToolTipText(const ZoneToolSnapshot& snapshot);
ZoneToolStatusText BuildZoneToolStatusText(const ZoneToolSnapshot& snapshot);
