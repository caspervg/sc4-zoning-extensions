#include "util/Settings.hpp"

#include "Logger.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr wchar_t kIniFileName[] = L"SC4ZoningExtensions.ini";
    constexpr wchar_t kGeneralSectionName[] = L"SC4ZoningExtensions";
    constexpr wchar_t kControlsSectionName[] = L"Controls";
    constexpr wchar_t kDefaultPresets[] = L"1x1,1x2,2x1,2x2,2x3,3x2,3x3,3x4,4x3,4x4,5x5,6x6,7x7,8x8";
    constexpr int kMaxHorizontalWheelUnitsPerStep = 100000;

    std::wstring ToLower(const std::wstring_view value)
    {
        std::wstring normalized(value);
        std::transform(
            normalized.begin(),
            normalized.end(),
            normalized.begin(),
            [](const wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return normalized;
    }

    spdlog::level::level_enum ParseLogLevel(const std::wstring_view value, bool& valid) noexcept
    {
        const std::wstring normalized = ToLower(value);

        if (normalized == L"trace") { valid = true; return spdlog::level::trace; }
        if (normalized == L"debug") { valid = true; return spdlog::level::debug; }
        if (normalized == L"info") { valid = true; return spdlog::level::info; }
        if (normalized == L"warn" || normalized == L"warning") { valid = true; return spdlog::level::warn; }
        if (normalized == L"error") { valid = true; return spdlog::level::err; }
        if (normalized == L"critical") { valid = true; return spdlog::level::critical; }
        if (normalized == L"off") { valid = true; return spdlog::level::off; }

        valid = false;
        return spdlog::level::info;
    }

    bool ParseBool(const std::wstring_view value, bool& valid) noexcept
    {
        const std::wstring normalized = ToLower(value);

        if (normalized == L"true" || normalized == L"1" || normalized == L"yes") {
            valid = true;
            return true;
        }
        if (normalized == L"false" || normalized == L"0" || normalized == L"no") {
            valid = true;
            return false;
        }

        valid = false;
        return false;
    }

    const wchar_t* GetVerticalWheelIniName(const ZoneVerticalWheelMode mode) noexcept
    {
        switch (mode) {
        case ZoneVerticalWheelMode::Zoom:
            return L"Zoom";
        case ZoneVerticalWheelMode::Depth:
            return L"Depth";
        default:
            return L"DepthWhileDragging";
        }
    }

    const wchar_t* GetMouseButtonIniName(const ZoneMouseButton button) noexcept
    {
        switch (button) {
        case ZoneMouseButton::Middle:
            return L"Middle";
        case ZoneMouseButton::XButton1:
            return L"XButton1";
        case ZoneMouseButton::XButton2:
            return L"XButton2";
        default:
            return L"None";
        }
    }

    bool ParseMouseButton(const std::wstring_view value, ZoneMouseButton& button)
    {
        const std::wstring normalized = ToLower(value);

        if (normalized.empty() || normalized == L"none") {
            button = ZoneMouseButton::None;
            return true;
        }
        if (normalized == L"middle" || normalized == L"mouse3") {
            button = ZoneMouseButton::Middle;
            return true;
        }
        if (normalized == L"xbutton1" || normalized == L"mouse4" || normalized == L"back") {
            button = ZoneMouseButton::XButton1;
            return true;
        }
        if (normalized == L"xbutton2" || normalized == L"mouse5" || normalized == L"forward") {
            button = ZoneMouseButton::XButton2;
            return true;
        }
        return false;
    }

    // Parses "WxD,WxD,..." (whitespace ignored, each side 1..16) and counts the entries it skips.
    std::vector<ZoneParcelPreset> ParsePresets(const std::wstring_view value, int& invalidCount)
    {
        std::vector<ZoneParcelPreset> presets;
        invalidCount = 0;

        size_t start = 0;
        while (start <= value.size()) {
            size_t end = value.find(L',', start);
            if (end == std::wstring_view::npos) {
                end = value.size();
            }

            std::wstring entry;
            for (const wchar_t c : value.substr(start, end - start)) {
                if (!std::iswspace(c)) {
                    entry.push_back(static_cast<wchar_t>(std::towlower(c)));
                }
            }
            start = end + 1;

            if (entry.empty()) {
                continue;
            }

            const size_t separator = entry.find(L'x');
            if (separator != std::wstring::npos && separator != 0 && separator + 1 < entry.size() &&
                entry.find(L'x', separator + 1) == std::wstring::npos) {
                wchar_t* widthEnd = nullptr;
                wchar_t* depthEnd = nullptr;
                const long width = std::wcstol(entry.c_str(), &widthEnd, 10);
                const long depth = std::wcstol(entry.c_str() + separator + 1, &depthEnd, 10);
                if (widthEnd == entry.c_str() + separator &&
                    depthEnd == entry.c_str() + entry.size() &&
                    width >= 1 && width <= 16 && depth >= 1 && depth <= 16) {
                    presets.push_back({static_cast<int>(width), static_cast<int>(depth)});
                    continue;
                }
            }
            ++invalidCount;
        }

        return presets;
    }

    const wchar_t* GetZoneTypeIniSectionName(const cISC4ZoneManager::ZoneType zoneType) noexcept
    {
        switch (zoneType) {
        case cISC4ZoneManager::ZoneType::None:
            return L"Dezone";
        case cISC4ZoneManager::ZoneType::ResidentialLowDensity:
            return L"ResidentialLowDensity";
        case cISC4ZoneManager::ZoneType::ResidentialMediumDensity:
            return L"ResidentialMediumDensity";
        case cISC4ZoneManager::ZoneType::ResidentialHighDensity:
            return L"ResidentialHighDensity";
        case cISC4ZoneManager::ZoneType::CommercialLowDensity:
            return L"CommercialLowDensity";
        case cISC4ZoneManager::ZoneType::CommercialMediumDensity:
            return L"CommercialMediumDensity";
        case cISC4ZoneManager::ZoneType::CommercialHighDensity:
            return L"CommercialHighDensity";
        case cISC4ZoneManager::ZoneType::Agriculture:
            return L"Agriculture";
        case cISC4ZoneManager::ZoneType::IndustrialMediumDensity:
            return L"IndustrialMediumDensity";
        case cISC4ZoneManager::ZoneType::IndustrialHighDensity:
            return L"IndustrialHighDensity";
        case cISC4ZoneManager::ZoneType::Military:
            return L"Military";
        case cISC4ZoneManager::ZoneType::Airport:
            return L"Airport";
        case cISC4ZoneManager::ZoneType::Seaport:
            return L"Seaport";
        case cISC4ZoneManager::ZoneType::Spaceport:
            return L"Spaceport";
        case cISC4ZoneManager::ZoneType::Landfill:
            return L"Landfill";
        case cISC4ZoneManager::ZoneType::Plopped:
            return L"Plopped";
        }

        return L"Unknown";
    }

    ZoneInternalNetworkMode ParseNetworkMode(const std::wstring_view value,
                                             const ZoneInternalNetworkMode fallback) noexcept
    {
        if (_wcsicmp(value.data(), L"None") == 0) {
            return ZoneInternalNetworkMode::None;
        }
        if (_wcsicmp(value.data(), L"Street") == 0) {
            return ZoneInternalNetworkMode::Street;
        }
        if (_wcsicmp(value.data(), L"Road") == 0) {
            return ZoneInternalNetworkMode::Road;
        }
        if (_wcsicmp(value.data(), L"OneWayRoad") == 0) {
            return ZoneInternalNetworkMode::OneWayRoad;
        }
        if (_wcsicmp(value.data(), L"Avenue") == 0) {
            return ZoneInternalNetworkMode::Avenue;
        }

        return fallback;
    }

    const wchar_t* GetNetworkModeIniName(const ZoneInternalNetworkMode mode) noexcept
    {
        switch (mode) {
        case ZoneInternalNetworkMode::None:
            return L"None";
        case ZoneInternalNetworkMode::Street:
            return L"Street";
        case ZoneInternalNetworkMode::Road:
            return L"Road";
        case ZoneInternalNetworkMode::OneWayRoad:
            return L"OneWayRoad";
        case ZoneInternalNetworkMode::Avenue:
            return L"Avenue";
        }

        return L"Street";
    }
}

bool Settings::Load()
{
    SetBuiltInDefaults_();

    if (!ResolveIniPath_()) {
        LOG_WARN("Failed to resolve settings path, using built-in defaults");
        return false;
    }

    const DWORD attributes = GetFileAttributesW(iniPath_.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        WriteDefaultIniFile_();
    }

    LoadGeneralSettings_();
    LoadZoneDefaults_();
    LoadControls_();
    LOG_INFO("Loaded settings");
    return true;
}

const ZoneTypeDefaultsTable& Settings::GetZoneDefaults() const noexcept
{
    return zoneDefaults_;
}

const ZoneControlSettings& Settings::GetControls() const noexcept
{
    return controls_;
}

const std::wstring& Settings::GetIniPath() const noexcept
{
    return iniPath_;
}

spdlog::level::level_enum Settings::GetLogLevel() const noexcept
{
    return logLevel_;
}

bool Settings::GetLogToFile() const noexcept
{
    return logToFile_;
}

bool Settings::ResolveIniPath_()
{
    HMODULE moduleHandle = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetZoneTypeIniSectionName),
            &moduleHandle)) {
        return false;
    }

    std::array<wchar_t, MAX_PATH> modulePath{};
    const DWORD length = GetModuleFileNameW(moduleHandle, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length >= modulePath.size()) {
        return false;
    }

    std::wstring path(modulePath.data(), length);
    const size_t slashIndex = path.find_last_of(L"\\/");
    if (slashIndex == std::wstring::npos) {
        return false;
    }

    iniPath_ = path.substr(0, slashIndex + 1);
    iniPath_.append(kIniFileName);
    return true;
}

void Settings::SetBuiltInDefaults_() noexcept
{
    logLevel_ = spdlog::level::info;
    logToFile_ = true;
    controls_ = {};

    for (size_t i = 0; i < zoneDefaults_.size(); ++i) {
        zoneDefaults_[i].parcelWidth = 1;
        zoneDefaults_[i].parcelLength = 2;
        zoneDefaults_[i].streetInterval = 0;
        zoneDefaults_[i].networkMode = ZoneInternalNetworkMode::Street;
    }

    zoneDefaults_[static_cast<size_t>(cISC4ZoneManager::ZoneType::None)].networkMode = ZoneInternalNetworkMode::None;
    zoneDefaults_[static_cast<size_t>(cISC4ZoneManager::ZoneType::Plopped)].networkMode = ZoneInternalNetworkMode::None;
}

void Settings::LoadGeneralSettings_() noexcept
{
    std::array<wchar_t, 64> buffer{};

    GetPrivateProfileStringW(
        kGeneralSectionName,
        L"LogLevel",
        L"info",
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        iniPath_.c_str());
    bool valid = false;
    logLevel_ = ParseLogLevel(buffer.data(), valid);
    if (!valid) {
        logLevel_ = spdlog::level::info;
        LOG_ERROR("Invalid LogLevel value in [{}]. Using default info.", "SC4ZoningExtensions");
    }

    GetPrivateProfileStringW(
        kGeneralSectionName,
        L"LogToFile",
        L"true",
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        iniPath_.c_str());
    logToFile_ = ParseBool(buffer.data(), valid);
    if (!valid) {
        logToFile_ = true;
        LOG_ERROR("Invalid LogToFile value in [{}]. Using default true.", "SC4ZoningExtensions");
    }
}

void Settings::LoadZoneDefaults_()
{
    std::array<wchar_t, 64> buffer{};
    std::array<wchar_t, 512> presetsBuffer{};

    for (size_t i = 0; i < zoneDefaults_.size(); ++i) {
        const auto zoneType = static_cast<cISC4ZoneManager::ZoneType>(i);
        const wchar_t* section = GetZoneTypeIniSectionName(zoneType);
        ZoneTypeDefaults& defaults = zoneDefaults_[i];

        defaults.parcelWidth = GetPrivateProfileIntW(section, L"ParcelWidth", defaults.parcelWidth, iniPath_.c_str());
        // ParcelHeight is the old name of ParcelDepth.
        defaults.parcelLength = GetPrivateProfileIntW(section, L"ParcelHeight", defaults.parcelLength, iniPath_.c_str());
        defaults.parcelLength = GetPrivateProfileIntW(section, L"ParcelDepth", defaults.parcelLength, iniPath_.c_str());
        defaults.streetInterval = GetPrivateProfileIntW(
            section,
            L"StreetInterval",
            defaults.streetInterval,
            iniPath_.c_str());

        GetPrivateProfileStringW(
            section,
            L"Network",
            GetNetworkModeIniName(defaults.networkMode),
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            iniPath_.c_str());
        defaults.networkMode = ParseNetworkMode(buffer.data(), defaults.networkMode);

        GetPrivateProfileStringW(
            section,
            L"Presets",
            kDefaultPresets,
            presetsBuffer.data(),
            static_cast<DWORD>(presetsBuffer.size()),
            iniPath_.c_str());
        int invalidCount = 0;
        defaults.presets = ParsePresets(presetsBuffer.data(), invalidCount);
        if (invalidCount > 0) {
            LOG_WARN("Skipped {} invalid Presets entries (expected WxD, each 1..16)", invalidCount);
        }
    }
}

void Settings::LoadControls_()
{
    std::array<wchar_t, 64> buffer{};

    GetPrivateProfileStringW(
        kControlsSectionName,
        L"VerticalWheel",
        GetVerticalWheelIniName(controls_.verticalWheel),
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        iniPath_.c_str());
    const std::wstring verticalWheel = ToLower(buffer.data());
    if (verticalWheel == L"zoom") {
        controls_.verticalWheel = ZoneVerticalWheelMode::Zoom;
    }
    else if (verticalWheel == L"depthwhiledragging") {
        controls_.verticalWheel = ZoneVerticalWheelMode::DepthWhileDragging;
    }
    else if (verticalWheel == L"depth") {
        controls_.verticalWheel = ZoneVerticalWheelMode::Depth;
    }
    else {
        LOG_ERROR("Invalid VerticalWheel value in [Controls] (use Zoom, DepthWhileDragging or Depth). Keeping the default.");
    }

    GetPrivateProfileStringW(
        kControlsSectionName,
        L"HorizontalWheel",
        controls_.horizontalWheel ? L"Width" : L"None",
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        iniPath_.c_str());
    const std::wstring horizontalWheel = ToLower(buffer.data());
    if (horizontalWheel == L"width") {
        controls_.horizontalWheel = true;
    }
    else if (horizontalWheel == L"none") {
        controls_.horizontalWheel = false;
    }
    else {
        LOG_ERROR("Invalid HorizontalWheel value in [Controls] (use Width or None). Keeping the default.");
    }

    controls_.horizontalWheelUnitsPerStep = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(
            kControlsSectionName,
            L"HorizontalWheelUnitsPerStep",
            controls_.horizontalWheelUnitsPerStep,
            iniPath_.c_str())),
        0,
        kMaxHorizontalWheelUnitsPerStep);

    const std::pair<const char*, ZoneMouseButton*> buttons[] = {
        {"ZoomButton", &controls_.zoomButton},
        {"PresetPrevious", &controls_.presetPreviousButton},
        {"PresetNext", &controls_.presetNextButton},
        {"WidthWheelButton", &controls_.widthWheelButton},
        {"DepthWheelButton", &controls_.depthWheelButton},
        {"StreetIntervalWheelButton", &controls_.streetIntervalWheelButton},
    };
    for (const auto& [name, button] : buttons) {
        const std::wstring key(name, name + std::strlen(name));
        GetPrivateProfileStringW(
            kControlsSectionName,
            key.c_str(),
            GetMouseButtonIniName(*button),
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            iniPath_.c_str());
        if (!ParseMouseButton(buffer.data(), *button)) {
            LOG_ERROR("Invalid {} value in [Controls] (use None, Middle, XButton1 or XButton2). Keeping the default.", name);
        }
    }

    // A button can drive only one thing with the wheel. Sharing a wheel button with a preset
    // button is fine: a click only steps the preset when the wheel was not turned meanwhile.
    ZoneControlSettings& c = controls_;
    if (c.depthWheelButton != ZoneMouseButton::None && c.depthWheelButton == c.widthWheelButton) {
        LOG_WARN("DepthWheelButton duplicates WidthWheelButton, disabling it");
        c.depthWheelButton = ZoneMouseButton::None;
    }
    if (c.streetIntervalWheelButton != ZoneMouseButton::None &&
        (c.streetIntervalWheelButton == c.widthWheelButton || c.streetIntervalWheelButton == c.depthWheelButton)) {
        LOG_WARN("StreetIntervalWheelButton duplicates another wheel button, disabling it");
        c.streetIntervalWheelButton = ZoneMouseButton::None;
    }
    if (c.zoomButton != ZoneMouseButton::None &&
        (c.zoomButton == c.widthWheelButton ||
         c.zoomButton == c.depthWheelButton ||
         c.zoomButton == c.streetIntervalWheelButton)) {
        LOG_WARN("ZoomButton duplicates a wheel button, disabling it");
        c.zoomButton = ZoneMouseButton::None;
    }
    if (c.verticalWheel == ZoneVerticalWheelMode::Depth && c.zoomButton == ZoneMouseButton::None) {
        LOG_WARN("VerticalWheel=Depth without a ZoomButton: the camera cannot be zoomed while zoning");
    }
    if (c.presetNextButton != ZoneMouseButton::None && c.presetNextButton == c.presetPreviousButton) {
        LOG_WARN("PresetNext duplicates PresetPrevious, disabling it");
        c.presetNextButton = ZoneMouseButton::None;
    }
}

void Settings::WriteDefaultIniFile_() const
{
    WritePrivateProfileStringW(kGeneralSectionName, L"LogLevel", L"info", iniPath_.c_str());
    WritePrivateProfileStringW(kGeneralSectionName, L"LogToFile", L"true", iniPath_.c_str());

    const std::pair<const wchar_t*, ZoneMouseButton> buttons[] = {
        {L"ZoomButton", controls_.zoomButton},
        {L"PresetPrevious", controls_.presetPreviousButton},
        {L"PresetNext", controls_.presetNextButton},
        {L"WidthWheelButton", controls_.widthWheelButton},
        {L"DepthWheelButton", controls_.depthWheelButton},
        {L"StreetIntervalWheelButton", controls_.streetIntervalWheelButton},
    };
    WritePrivateProfileStringW(
        kControlsSectionName, L"VerticalWheel", GetVerticalWheelIniName(controls_.verticalWheel), iniPath_.c_str());
    WritePrivateProfileStringW(
        kControlsSectionName, L"HorizontalWheel", controls_.horizontalWheel ? L"Width" : L"None", iniPath_.c_str());
    WritePrivateProfileStringW(
        kControlsSectionName,
        L"HorizontalWheelUnitsPerStep",
        std::to_wstring(controls_.horizontalWheelUnitsPerStep).c_str(),
        iniPath_.c_str());
    for (const auto& [key, button] : buttons) {
        WritePrivateProfileStringW(kControlsSectionName, key, GetMouseButtonIniName(button), iniPath_.c_str());
    }

    for (size_t i = 0; i < zoneDefaults_.size(); ++i) {
        const auto zoneType = static_cast<cISC4ZoneManager::ZoneType>(i);
        const wchar_t* section = GetZoneTypeIniSectionName(zoneType);
        const ZoneTypeDefaults& defaults = zoneDefaults_[i];

        wchar_t widthBuffer[16] = {};
        wchar_t heightBuffer[16] = {};
        wchar_t streetIntervalBuffer[16] = {};
        _snwprintf_s(widthBuffer, _countof(widthBuffer), _TRUNCATE, L"%d", defaults.parcelWidth);
        _snwprintf_s(heightBuffer, _countof(heightBuffer), _TRUNCATE, L"%d", defaults.parcelLength);
        _snwprintf_s(streetIntervalBuffer, _countof(streetIntervalBuffer), _TRUNCATE, L"%d", defaults.streetInterval);

        WritePrivateProfileStringW(section, L"ParcelWidth", widthBuffer, iniPath_.c_str());
        WritePrivateProfileStringW(section, L"ParcelDepth", heightBuffer, iniPath_.c_str());
        WritePrivateProfileStringW(section, L"Presets", kDefaultPresets, iniPath_.c_str());
        WritePrivateProfileStringW(section, L"StreetInterval", streetIntervalBuffer, iniPath_.c_str());
        WritePrivateProfileStringW(section, L"Network", GetNetworkModeIniName(defaults.networkMode), iniPath_.c_str());
    }
}

void Settings::SaveParcelSizes(const ZoneTypeDefaultsTable& zoneDefaults)
{
    if (iniPath_.empty()) {
        return;
    }

    for (size_t i = 0; i < zoneDefaults_.size(); ++i) {
        ZoneTypeDefaults& saved = zoneDefaults_[i];
        const ZoneTypeDefaults& current = zoneDefaults[i];
        if (current.parcelWidth == saved.parcelWidth && current.parcelLength == saved.parcelLength) {
            continue;
        }

        const wchar_t* section = GetZoneTypeIniSectionName(static_cast<cISC4ZoneManager::ZoneType>(i));
        WritePrivateProfileStringW(section, L"ParcelWidth", std::to_wstring(current.parcelWidth).c_str(), iniPath_.c_str());
        WritePrivateProfileStringW(section, L"ParcelDepth", std::to_wstring(current.parcelLength).c_str(), iniPath_.c_str());
        saved.parcelWidth = current.parcelWidth;
        saved.parcelLength = current.parcelLength;
    }
}
