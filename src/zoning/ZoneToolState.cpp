#include "zoning/ZoneToolState.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace
{
    constexpr std::array<ZoneInternalNetworkMode, 5> kNetworkModes{{
        ZoneInternalNetworkMode::None,
        ZoneInternalNetworkMode::Street,
        ZoneInternalNetworkMode::Road,
        ZoneInternalNetworkMode::OneWayRoad,
        ZoneInternalNetworkMode::Avenue,
    }};
}

const ZoneToolSnapshot& ZoneToolState::Snapshot() const noexcept
{
    return snapshot_;
}

void ZoneToolState::SetZoneDefaults(const ZoneTypeDefaultsTable& defaults) noexcept
{
    zoneDefaults_ = defaults;
    ApplyDefaultsForCurrentZoneType_();
}

void ZoneToolState::SetZoneType(const cISC4ZoneManager::ZoneType zoneType) noexcept
{
    snapshot_.zoneType = zoneType;
    ApplyDefaultsForCurrentZoneType_();
}

void ZoneToolState::SetToolActive(const bool value) noexcept
{
    snapshot_.toolActive = value;
}

void ZoneToolState::SetValidationMessage(std::string value) noexcept
{
    if (snapshot_.validationMessage != value) {
        snapshot_.validationMessage = std::move(value);
    }
}

void ZoneToolState::CycleNetworkMode(const int delta) noexcept
{
    const auto it = std::find(kNetworkModes.begin(), kNetworkModes.end(), snapshot_.networkMode);
    const int currentIndex = it != kNetworkModes.end() ? static_cast<int>(it - kNetworkModes.begin()) : 0;
    const int count = static_cast<int>(kNetworkModes.size());
    int nextIndex = (currentIndex + delta) % count;
    if (nextIndex < 0) {
        nextIndex += count;
    }
    snapshot_.networkMode = kNetworkModes[static_cast<size_t>(nextIndex)];
}

void ZoneToolState::AdjustParcelWidth(const int delta) noexcept
{
    snapshot_.parcelWidth = ClampParcelMetric_(snapshot_.parcelWidth + delta);
}

void ZoneToolState::AdjustParcelLength(const int delta) noexcept
{
    snapshot_.parcelLength = ClampParcelMetric_(snapshot_.parcelLength + delta);
}

void ZoneToolState::AdjustStreetInterval(const int delta) noexcept
{
    snapshot_.streetInterval = ClampStreetInterval_(snapshot_.streetInterval + delta);
}

int ZoneToolState::ClampParcelMetric_(const int value) noexcept
{
    return std::clamp(value, 1, 16);
}

int ZoneToolState::ClampStreetInterval_(const int value) noexcept
{
    return std::clamp(value, 0, 32);
}

void ZoneToolState::ApplyDefaultsForCurrentZoneType_() noexcept
{
    const size_t index = static_cast<size_t>(snapshot_.zoneType);
    if (index >= zoneDefaults_.size()) {
        return;
    }

    const ZoneTypeDefaults& defaults = zoneDefaults_[index];
    snapshot_.parcelWidth = ClampParcelMetric_(defaults.parcelWidth);
    snapshot_.parcelLength = ClampParcelMetric_(defaults.parcelLength);
    snapshot_.streetInterval = ClampStreetInterval_(defaults.streetInterval);
    snapshot_.networkMode = defaults.networkMode;
}

const char* GetZoneTypeLabel(const cISC4ZoneManager::ZoneType zoneType) noexcept
{
    switch (zoneType) {
    case cISC4ZoneManager::ZoneType::None:
        return "De-zone";
    case cISC4ZoneManager::ZoneType::ResidentialLowDensity:
        return "Low Density Residential";
    case cISC4ZoneManager::ZoneType::ResidentialMediumDensity:
        return "Medium Density Residential";
    case cISC4ZoneManager::ZoneType::ResidentialHighDensity:
        return "High Density Residential";
    case cISC4ZoneManager::ZoneType::CommercialLowDensity:
        return "Low Density Commercial";
    case cISC4ZoneManager::ZoneType::CommercialMediumDensity:
        return "Medium Density Commercial";
    case cISC4ZoneManager::ZoneType::CommercialHighDensity:
        return "High Density Commercial";
    case cISC4ZoneManager::ZoneType::Agriculture:
        return "Agriculture";
    case cISC4ZoneManager::ZoneType::IndustrialMediumDensity:
        return "Medium Density Industrial";
    case cISC4ZoneManager::ZoneType::IndustrialHighDensity:
        return "High Density Industrial";
    case cISC4ZoneManager::ZoneType::Military:
        return "Military";
    case cISC4ZoneManager::ZoneType::Airport:
        return "Airport";
    case cISC4ZoneManager::ZoneType::Seaport:
        return "Seaport";
    case cISC4ZoneManager::ZoneType::Spaceport:
        return "Spaceport";
    case cISC4ZoneManager::ZoneType::Landfill:
        return "Landfill";
    case cISC4ZoneManager::ZoneType::Plopped:
        return "Plopped";
    }

    return "Unknown";
}

const char* GetZoneTypeShortLabel(const cISC4ZoneManager::ZoneType zoneType) noexcept
{
    return GetZoneTypeLabel(zoneType);
}

const char* GetZoneNetworkModeLabel(const ZoneInternalNetworkMode mode) noexcept
{
    switch (mode) {
    case ZoneInternalNetworkMode::None:
        return "None";
    case ZoneInternalNetworkMode::Road:
        return "Road";
    case ZoneInternalNetworkMode::Street:
        return "Street";
    case ZoneInternalNetworkMode::Avenue:
        return "Avenue";
    case ZoneInternalNetworkMode::OneWayRoad:
        return "One-Way Road";
    }

    return "Unknown";
}

const char* GetZoneNetworkModeShortLabel(const ZoneInternalNetworkMode mode) noexcept
{
    return GetZoneNetworkModeLabel(mode);
}

ZoneToolTipText BuildZoneToolTipText(const ZoneToolSnapshot& snapshot)
{
    const char* streetIntervalLabel = snapshot.streetInterval == 0 ? "Auto" : nullptr;
    char streetIntervalBuffer[16] = {};
    if (!streetIntervalLabel) {
        std::snprintf(streetIntervalBuffer, sizeof(streetIntervalBuffer), "%d", snapshot.streetInterval);
        streetIntervalLabel = streetIntervalBuffer;
    }

    char bodyBuffer[192] = {};
    std::snprintf(
        bodyBuffer,
        sizeof(bodyBuffer),
        "%s | %dx%d | I:%s\nTab/Shift+Tab network | -/+ width | [/] height | ,/. interval",
        GetZoneNetworkModeShortLabel(snapshot.networkMode),
        snapshot.parcelWidth,
        snapshot.parcelLength,
        streetIntervalLabel);

    ZoneToolTipText text;
    text.title = GetZoneTypeShortLabel(snapshot.zoneType);
    text.body = bodyBuffer;
    return text;
}

ZoneToolStatusText BuildZoneToolStatusText(const ZoneToolSnapshot& snapshot)
{
    char zoneBuffer[96] = {};
    char parcelBuffer[64] = {};
    char intervalBuffer[48] = {};

    std::snprintf(zoneBuffer, sizeof(zoneBuffer), "Zone: %s", GetZoneTypeLabel(snapshot.zoneType));
    std::snprintf(parcelBuffer, sizeof(parcelBuffer), "Parcel: %d x %d", snapshot.parcelWidth, snapshot.parcelLength);
    if (snapshot.streetInterval == 0) {
        std::snprintf(intervalBuffer, sizeof(intervalBuffer), "Street interval: Auto");
    }
    else {
        std::snprintf(intervalBuffer, sizeof(intervalBuffer), "Street interval: %d", snapshot.streetInterval);
    }

    ZoneToolStatusText text;
    text.zoneLine = zoneBuffer;
    text.parcelLine = parcelBuffer;
    text.streetIntervalLine = intervalBuffer;
    text.modifiersLine = "Tab/Shift+Tab network | -/+ width | [/] height | ,/. interval";
    text.wheelLine.clear();
    return text;
}
