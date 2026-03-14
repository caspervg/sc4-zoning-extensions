#pragma once

#include "../zoning/ZoneToolState.hpp"

#include <string>

class Settings
{
public:
    bool Load();

    [[nodiscard]] const ZoneTypeDefaultsTable& GetZoneDefaults() const noexcept;
    [[nodiscard]] const std::wstring& GetIniPath() const noexcept;

private:
    bool ResolveIniPath_();
    void SetBuiltInDefaults_() noexcept;
    void LoadZoneDefaults_() noexcept;
    void WriteDefaultIniFile_() const;

    ZoneTypeDefaultsTable zoneDefaults_{};
    std::wstring iniPath_{};
};
