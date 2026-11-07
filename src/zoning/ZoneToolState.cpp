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

    const char* GetMouseButtonLabel(const ZoneMouseButton button) noexcept
    {
        switch (button) {
        case ZoneMouseButton::Middle:
            return "Middle";
        case ZoneMouseButton::XButton1:
            return "Mouse4";
        case ZoneMouseButton::XButton2:
            return "Mouse5";
        default:
            return "None";
        }
    }

    void AppendHint(std::string& line, const char* text)
    {
        if (!line.empty()) {
            line.append(" | ");
        }
        line.append(text);
    }

    void AppendButtonHint(std::string& line, const ZoneMouseButton button, const char* joiner, const char* action)
    {
        if (button == ZoneMouseButton::None) {
            return;
        }
        AppendHint(line, GetMouseButtonLabel(button));
        line.append(joiner);
        line.append(action);
    }

    // The mouse controls from [Controls], e.g. "Tilt wheel width | Wheel depth (while dragging) | Mouse4 prev preset".
    std::string BuildControlsLine(const ZoneControlSettings& controls)
    {
        std::string line;
        if (controls.horizontalWheel) {
            AppendHint(line, "Tilt wheel width");
        }
        if (controls.verticalWheel == ZoneVerticalWheelMode::DepthWhileDragging) {
            AppendHint(line, "Wheel depth (while dragging)");
        }
        else if (controls.verticalWheel == ZoneVerticalWheelMode::Depth) {
            AppendHint(line, "Wheel depth");
            AppendButtonHint(line, controls.zoomButton, "+Wheel ", "zoom");
        }
        AppendButtonHint(line, controls.presetPreviousButton, " ", "prev preset");
        AppendButtonHint(line, controls.presetNextButton, " ", "next preset");
        AppendButtonHint(line, controls.widthWheelButton, "+Wheel ", "width");
        AppendButtonHint(line, controls.depthWheelButton, "+Wheel ", "depth");
        AppendButtonHint(line, controls.streetIntervalWheelButton, "+Wheel ", "interval");
        return line;
    }
}

const ZoneToolSnapshot& ZoneToolState::Snapshot() const noexcept
{
    return snapshot_;
}

void ZoneToolState::SetZoneDefaults(const ZoneTypeDefaultsTable& defaults)
{
    zoneDefaults_ = defaults;
    ApplyDefaultsForCurrentZoneType_();
}

void ZoneToolState::SetControls(const ZoneControlSettings& controls) noexcept
{
    snapshot_.controls = controls;
}

void ZoneToolState::RememberCurrentSize() noexcept
{
    if (const size_t index = static_cast<size_t>(snapshot_.zoneType); index < zoneDefaults_.size()) {
        zoneDefaults_[index].parcelWidth = snapshot_.parcelWidth;
        zoneDefaults_[index].parcelLength = snapshot_.parcelLength;
    }
}

const ZoneTypeDefaultsTable& ZoneToolState::ZoneDefaults() const noexcept
{
    return zoneDefaults_;
}

void ZoneToolState::SetZoneType(const cISC4ZoneManager::ZoneType zoneType) noexcept
{
    RememberCurrentSize();
    snapshot_.zoneType = zoneType;
    ApplyDefaultsForCurrentZoneType_();
}

void ZoneToolState::SetToolActive(const bool value) noexcept
{
    snapshot_.toolActive = value;
}

void ZoneToolState::SetPreviewActive(const bool value) noexcept
{
    snapshot_.previewActive = value;
}

void ZoneToolState::SetValidationMessage(std::string value) noexcept
{
    if (snapshot_.validationMessage != value) {
        snapshot_.validationMessage = std::move(value);
    }
}

void ZoneToolState::SetDiagonalState(const bool supported, const bool enabled, const int thickness) noexcept
{
    snapshot_.diagonalSupported = supported;
    snapshot_.diagonalMode = supported && enabled;
    snapshot_.diagonalThickness = std::max(1, thickness);
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
    SyncPresetIndex_();
}

void ZoneToolState::AdjustParcelLength(const int delta) noexcept
{
    snapshot_.parcelLength = ClampParcelMetric_(snapshot_.parcelLength + delta);
    SyncPresetIndex_();
}

void ZoneToolState::AdjustStreetInterval(const int delta) noexcept
{
    snapshot_.streetInterval = ClampStreetInterval_(snapshot_.streetInterval + delta);
}

bool ZoneToolState::CyclePreset(const int delta) noexcept
{
    const size_t index = static_cast<size_t>(snapshot_.zoneType);
    if (index >= zoneDefaults_.size() || delta == 0) {
        return false;
    }

    const std::vector<ZoneParcelPreset>& presets = zoneDefaults_[index].presets;
    if (presets.empty()) {
        return false;
    }

    const int count = static_cast<int>(presets.size());
    int next = 0;
    if (snapshot_.presetIndex < 0) {
        next = delta > 0 ? 0 : count - 1;
    }
    else {
        next = (snapshot_.presetIndex + (delta > 0 ? 1 : -1) + count) % count;
    }

    snapshot_.parcelWidth = ClampParcelMetric_(presets[static_cast<size_t>(next)].width);
    snapshot_.parcelLength = ClampParcelMetric_(presets[static_cast<size_t>(next)].depth);
    snapshot_.presetIndex = next;
    return true;
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
    snapshot_.presetIndex = -1;
    SyncPresetIndex_();
}

void ZoneToolState::SyncPresetIndex_() noexcept
{
    const size_t index = static_cast<size_t>(snapshot_.zoneType);
    if (index >= zoneDefaults_.size()) {
        snapshot_.presetCount = 0;
        snapshot_.presetIndex = -1;
        return;
    }

    const std::vector<ZoneParcelPreset>& presets = zoneDefaults_[index].presets;
    snapshot_.presetCount = static_cast<int>(presets.size());

    // Keep the current preset while it still matches, so duplicates in the list cycle normally.
    const auto matches = [this](const ZoneParcelPreset& preset) {
        return preset.width == snapshot_.parcelWidth && preset.depth == snapshot_.parcelLength;
    };
    if (snapshot_.presetIndex >= 0 &&
        snapshot_.presetIndex < snapshot_.presetCount &&
        matches(presets[static_cast<size_t>(snapshot_.presetIndex)])) {
        return;
    }

    const auto it = std::find_if(presets.begin(), presets.end(), matches);
    snapshot_.presetIndex = it != presets.end() ? static_cast<int>(it - presets.begin()) : -1;
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

    char bodyBuffer[256] = {};
    std::snprintf(
        bodyBuffer,
        sizeof(bodyBuffer),
        "%s | W%d x D%d | I:%s\nTab/Shift+Tab network | -/+ width | [/] depth | ,/. interval",
        GetZoneNetworkModeShortLabel(snapshot.networkMode),
        snapshot.parcelWidth,
        snapshot.parcelLength,
        streetIntervalLabel);

    ZoneToolTipText text;
    text.title = GetZoneTypeShortLabel(snapshot.zoneType);
    text.body = bodyBuffer;
    const std::string controlsLine = BuildControlsLine(snapshot.controls);
    if (!controlsLine.empty()) {
        text.body.append("\n");
        text.body.append(controlsLine);
    }
    return text;
}

ZoneToolStatusText BuildZoneToolStatusText(const ZoneToolSnapshot& snapshot)
{
    char zoneBuffer[96] = {};
    char parcelBuffer[64] = {};
    char intervalBuffer[48] = {};

    std::snprintf(zoneBuffer, sizeof(zoneBuffer), "Zone: %s", GetZoneTypeLabel(snapshot.zoneType));
    if (snapshot.presetIndex < 0) {
        std::snprintf(parcelBuffer, sizeof(parcelBuffer), "Parcel: %d wide x %d deep", snapshot.parcelWidth,
                      snapshot.parcelLength);
    }
    else {
        std::snprintf(parcelBuffer, sizeof(parcelBuffer), "Parcel: %d wide x %d deep (preset %d/%d)",
                      snapshot.parcelWidth, snapshot.parcelLength, snapshot.presetIndex + 1, snapshot.presetCount);
    }
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
    text.modifiersLine = "Tab/Shift+Tab network | -/+ width | [/] depth | ,/. interval";
    text.controlsLine = BuildControlsLine(snapshot.controls);
    if (!snapshot.diagonalSupported) {
        text.wheelLine.clear();
    }
    else if (snapshot.diagonalMode) {
        char diagonalBuffer[64] = {};
        std::snprintf(
            diagonalBuffer,
            sizeof(diagonalBuffer),
            "Diagonal: On (D toggles) | Width: %d | Wheel adjusts",
            snapshot.diagonalThickness);
        text.wheelLine = diagonalBuffer;
    }
    else {
        text.wheelLine = "Diagonal: Off (D toggles)";
    }
    return text;
}
