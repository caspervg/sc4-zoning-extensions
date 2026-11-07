# SC4ZoningExtensions

This release zip includes:
- the Windows installer
- LICENSE.txt
- THIRD_PARTY_NOTICES.txt
- SC4ZoningExtensions.spdx.json
- SHA256SUMS.txt

Installation instructions:
- Install SC4RenderServices first so SC4RenderServices.dll is already present in your Plugins folder.
- SC4RenderServices project page: https://github.com/caspervg/sc4-render-services
- Install the Microsoft Visual C++ 2015-2022 Redistributable for x86: https://aka.ms/vs/17/release/vc_redist.x86.exe
- Run the included SC4 Zoning Extensions installer.
- The installer will ask for your SimCity 4 game root and Plugins folder, verify the SC4RenderServices dependency, and install the plugin files. If SC4RenderServices is missing or too old, the installer will stop and tell you where to download it.

Usage instructions:
- While a zoning tool is active, a status panel shows the current parcel size, street interval and embedded network.
- Tab/Shift+Tab cycles the embedded network: None -> Street -> Road -> OneWayRoad -> Avenue.
- -/+ changes the parcel width, [/] the parcel depth, and ,/. the street interval.
- By default the mouse wheel changes the parcel depth while you drag a zone, and tilting the wheel changes the width.
- Mouse4/Mouse5 step through the zone's parcel size presets (Presets in the INI); hold Mouse5 and turn the wheel to zoom.
- Where diagonal zoning is supported, D toggles it and the mouse wheel adjusts its width.
- Per-zone defaults, presets, mouse controls and logging are set in SC4ZoningExtensions.ini next to the DLL; it is created with defaults on first run.
- If something looks off, check <My Documents>/SimCity 4/SC4ZoningExtensions.log.
