#pragma once

#include "../zoning/ZoneToolState.hpp"

#include "spdlog/spdlog.h"

#include <string>

class Settings
{
public:
    bool Load();

    [[nodiscard]] const ZoneTypeDefaultsTable& GetZoneDefaults() const noexcept;
    [[nodiscard]] const std::wstring& GetIniPath() const noexcept;
    [[nodiscard]] spdlog::level::level_enum GetLogLevel() const noexcept;
    [[nodiscard]] bool GetLogToFile() const noexcept;

private:
    bool ResolveIniPath_();
    void SetBuiltInDefaults_() noexcept;
    void LoadGeneralSettings_() noexcept;
    void LoadZoneDefaults_() noexcept;
    void WriteDefaultIniFile_() const;

    ZoneTypeDefaultsTable zoneDefaults_{};
    std::wstring iniPath_{};
    spdlog::level::level_enum logLevel_ = spdlog::level::info;
    bool logToFile_ = true;
};
