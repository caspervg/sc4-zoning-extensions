cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(SC4ZoningExtensions VERSION 0.0.1 LANGUAGES C CXX)

# SimCity 4 only loads 32-bit DLLs.
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(STATUS "SC4ZoningExtensions DLL is only built for 32-bit Windows. Skipping target.")
    return()
endif()

set(SC4RS_ROOT "${CMAKE_SOURCE_DIR}/vendor/sc4-render-services" CACHE PATH "Path to sc4-render-services")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_compile_definitions(WIN32 _WINDOWS UNICODE _UNICODE)
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()

add_subdirectory(${SC4RS_ROOT}/vendor/spdlog ${CMAKE_CURRENT_BINARY_DIR}/spdlog)

set(GZCOM_DIR "${CMAKE_SOURCE_DIR}/vendor/gzcom-dll/gzcom-dll")
file(GLOB_RECURSE GZCOM_SOURCES CONFIGURE_DEPENDS
    "${GZCOM_DIR}/src/*.cpp"
)
list(REMOVE_ITEM GZCOM_SOURCES
    "${GZCOM_DIR}/src/main.cpp"
    "${GZCOM_DIR}/src/cGZCityHallUpgradeDllDirector.cpp"
    "${GZCOM_DIR}/src/cGZCustomDllDirector.cpp"
    "${GZCOM_DIR}/src/cGZExampleDllDirector.cpp"
    "${GZCOM_DIR}/src/cGZExtraCheatsDllDirector.cpp"
    "${GZCOM_DIR}/src/cGZExtraExtraCheatsDllDirector.cpp"
)

add_library(gzcom2 STATIC ${GZCOM_SOURCES})
target_include_directories(gzcom2 PUBLIC "${GZCOM_DIR}/include")

# Builds SC4RenderServices from the submodule, reusing gzcom2 and spdlog::spdlog from this project.
# EXCLUDE_FROM_ALL keeps its other targets out of the build.
add_subdirectory(${SC4RS_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/sc4-render-services EXCLUDE_FROM_ALL)

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
    "${SC4RS_ROOT}/src"
    "${SC4RS_ROOT}/src/public"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    gzcom2
    spdlog::spdlog
    version
)

# ImGui is compiled into SC4RenderServices.dll and exported from it, so link that. The import is
# delay-loaded: the game may load this plugin before SC4RenderServices.dll.
sc4rs_use_imgui(${PROJECT_NAME})

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
