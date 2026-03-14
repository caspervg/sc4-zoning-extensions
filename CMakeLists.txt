cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# SimCity 4 plugins must be 32-bit DLLs.
set(CMAKE_GENERATOR_PLATFORM "Win32" CACHE STRING "Target platform" FORCE)

# Enable vcpkg manifest mode before project().
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
set(VCPKG_MANIFEST_MODE ON CACHE BOOL "Enable vcpkg manifest mode")
set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE STRING "vcpkg manifest directory")
set(VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed" CACHE STRING "vcpkg installed directory")
set(VCPKG_TARGET_TRIPLET "x86-windows-static" CACHE STRING "vcpkg target triplet" FORCE)

project(SC4ZoningExtensions VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_compile_definitions(WIN32 _WINDOWS UNICODE _UNICODE)
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()

find_package(spdlog CONFIG REQUIRED)

add_subdirectory(vendor/gzcom-dll)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sc4-render-services/vendor/d3d7imgui/ImGui")
set(IMGUI_CORE_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
)
set(IMGUI_BACKENDS
    "${IMGUI_DIR}/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/imgui_impl_dx7.cpp"
)

add_library(imgui SHARED
    ${IMGUI_CORE_SOURCES}
    ${IMGUI_BACKENDS}
)

target_include_directories(imgui PUBLIC
    "${IMGUI_DIR}"
)

target_compile_definitions(imgui PRIVATE IMGUI_DLL_EXPORT)

if(WIN32)
    target_link_libraries(imgui PRIVATE user32 gdi32 ddraw dxguid)
endif()

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
        src/util/Settings.cpp
    src/SC4ZoningExtensionsDirector.cpp
    src/util/Logger.cpp
    src/util/VersionDetection.cpp
    src/zoning/ZoneDeveloperHooks.cpp
    src/zoning/ZoneStatusPanel.cpp
    src/zoning/ZoneToolState.cpp
    src/zoning/ZoneViewInputControl.cpp
    SC4ZoningExtensions.def
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/gzcom-dll/gzcom-dll/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sc4-render-services/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sc4-render-services/src/public"
    "${IMGUI_DIR}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    gzcom_dll
    imgui
    spdlog::spdlog
    delayimp
    version
)

if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE "/DELAYLOAD:imgui.dll")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "SC4ZoningExtensions"
    PREFIX ""
    SUFFIX ".dll"
)

if(WIN32 AND NOT DEFINED ENV{CI})
    set(SC4_INSTALL_PATHS
        "C:/Program Files (x86)/SimCity 4 Deluxe Edition"
        "C:/Program Files (x86)/Maxis/SimCity 4 Deluxe"
        "C:/Program Files/Maxis/SimCity 4 Deluxe"
        "C:/Program Files (x86)/Steam/steamapps/common/SimCity 4 Deluxe"
        "C:/Program Files/Steam/steamapps/common/SimCity 4 Deluxe"
        "C:/Program Files (x86)/GOG Galaxy/Games/SimCity 4 Deluxe Edition"
    )

    foreach(SC4_PATH ${SC4_INSTALL_PATHS})
        if(EXISTS "${SC4_PATH}/Apps/SimCity 4.exe")
            set(SC4_INSTALL_DIR "${SC4_PATH}")
            break()
        endif()
    endforeach()

    set(USERPROFILE_DIR "$ENV{USERPROFILE}")
    set(SC4_PLUGINS_DIR "${USERPROFILE_DIR}/Documents/SimCity 4/Plugins")

    if(SC4_INSTALL_DIR)
        message(STATUS "Found SC4 installation at: ${SC4_INSTALL_DIR}")

        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SC4_PLUGINS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${PROJECT_NAME}>
                "${SC4_PLUGINS_DIR}/"
            COMMENT "Deploying SC4ZoningExtensions.dll to Plugins folder"
        )

        message(STATUS "Deployment target configured:")
        message(STATUS "  - SC4ZoningExtensions.dll -> ${SC4_PLUGINS_DIR}/")
    else()
        message(WARNING "SC4 installation not found. Manual deployment required.")
    endif()
endif()
