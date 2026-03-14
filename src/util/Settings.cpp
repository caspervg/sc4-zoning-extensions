#include "util/Settings.hpp"

#include "Logger.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <cwchar>
#include <string>
#include <string_view>

namespace
{
    constexpr wchar_t kIniFileName[] = L"SC4ZoningExtensions.ini";
    constexpr wchar_t kGeneralSectionName[] = L"SC4ZoningExtensions";

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
    LOG_INFO("Loaded settings");
    return true;
}

const ZoneTypeDefaultsTable& Settings::GetZoneDefaults() const noexcept
{
    return zoneDefaults_;
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

    for (size_t i = 0; i < zoneDefaults_.size(); ++i) {
        zoneDefaults_[i].parcelWidth = 3;
        zoneDefaults_[i].parcelLength = 3;
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

void Settings::LoadZoneDefaults_() noexcept
{
    std::array<wchar_t, 64> buffer{};

    for (size_t i = 0; i < zoneDefaults_.size(); ++i) {
        const auto zoneType = static_cast<cISC4ZoneManager::ZoneType>(i);
        const wchar_t* section = GetZoneTypeIniSectionName(zoneType);
        ZoneTypeDefaults& defaults = zoneDefaults_[i];

        defaults.parcelWidth = GetPrivateProfileIntW(section, L"ParcelWidth", defaults.parcelWidth, iniPath_.c_str());
        defaults.parcelLength = GetPrivateProfileIntW(section, L"ParcelHeight", defaults.parcelLength, iniPath_.c_str());
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
    }
}

void Settings::WriteDefaultIniFile_() const
{
    WritePrivateProfileStringW(kGeneralSectionName, L"LogLevel", L"info", iniPath_.c_str());
    WritePrivateProfileStringW(kGeneralSectionName, L"LogToFile", L"true", iniPath_.c_str());

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
        WritePrivateProfileStringW(section, L"ParcelHeight", heightBuffer, iniPath_.c_str());
        WritePrivateProfileStringW(section, L"StreetInterval", streetIntervalBuffer, iniPath_.c_str());
        WritePrivateProfileStringW(section, L"Network", GetNetworkModeIniName(defaults.networkMode), iniPath_.c_str());
    }
}
