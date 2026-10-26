!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "nsDialogs.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!cd "${__FILEDIR__}"

!define APP_NAME "SC4 Zoning Extensions"
!ifndef APP_VERSION
  !define APP_VERSION "dev"
!endif
!define APP_TOOLS_SUBDIR "SC4ZoningExtensions"
!define RENDER_SERVICES_DLL_NAME "SC4RenderServices.dll"
!ifndef MIN_RENDER_SERVICES_VERSION_MAJOR
  !define MIN_RENDER_SERVICES_VERSION_MAJOR 0
!endif
!ifndef MIN_RENDER_SERVICES_VERSION_MINOR
  !define MIN_RENDER_SERVICES_VERSION_MINOR 0
!endif
!ifndef MIN_RENDER_SERVICES_VERSION_BUILD
  !define MIN_RENDER_SERVICES_VERSION_BUILD 1
!endif
!ifndef THIRD_PARTY_NOTICES_PATH
  !define THIRD_PARTY_NOTICES_PATH "THIRD_PARTY_NOTICES.txt"
!endif
!ifndef README_PATH
  !define README_PATH "README.txt"
!endif
!ifndef LICENSE_PATH
  !define LICENSE_PATH "..\LICENSE.txt"
!endif
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\SC4ZoningExtensions"
!define APP_REG_KEY "Software\SC4ZoningExtensions"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "SC4ZoningExtensions-${APP_VERSION}-Setup.exe"
Unicode True
RequestExecutionLevel admin
ShowInstDetails show
ShowUninstDetails show

Var Dialog
Var GameRoot
Var SC4PluginsDir
Var SC4ToolsDir
Var HGameRoot
Var HPluginsDir
Var HBrowseGameRoot
Var HBrowsePluginsDir
Var HSummaryText
Var GameExePath
Var RenderServicesDllPath

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Open SC4 Zoning Extensions output folder"
!define MUI_FINISHPAGE_RUN_FUNCTION OpenToolsFolder
!define MUI_FINISHPAGE_RUN_CHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${THIRD_PARTY_NOTICES_PATH}"
Page Custom ConfigurePathsPage ConfigurePathsPageLeave
Page Custom ConfigureSummaryPage
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Function .onInit
  SetShellVarContext current
  Call DetectDefaultGameRoot
  Push $GameRoot
  Call NormalizeDirPath
  Pop $GameRoot
  StrCpy $SC4PluginsDir "$DOCUMENTS\SimCity 4\Plugins"
FunctionEnd

Function NormalizeDirPath
  Exch $0
  Push $1
  Push $2

  ; Preserve drive roots such as C:\ while trimming trailing separators elsewhere.
  StrCpy $1 "$0" 3
normalize_dir_loop:
  StrLen $2 $0
  ${If} $2 <= 0
    Goto normalize_dir_done
  ${EndIf}

  StrCpy $2 $0 1 -1
  ${If} $2 != "\"
    Goto normalize_dir_done
  ${EndIf}

  ${If} $0 == $1
    Goto normalize_dir_done
  ${EndIf}

  StrCpy $0 $0 -1
  Goto normalize_dir_loop

normalize_dir_done:
  Pop $2
  Pop $1
  Exch $0
FunctionEnd

Function DetectDefaultGameRoot
  ; SC4 is a 32-bit app, so read from the 32-bit registry view.
  StrCpy $GameRoot "$PROGRAMFILES32\SimCity 4 Deluxe Edition"
  SetRegView 32
  ReadRegStr $0 HKLM "SOFTWARE\Maxis\SimCity 4" "Install Dir"
  ${If} $0 != ""
    StrCpy $GameRoot $0
  ${EndIf}
FunctionEnd

Function ValidateGameExecutable
  StrCpy $GameExePath "$GameRoot\Apps\SimCity 4.exe"

  ${IfNot} ${FileExists} "$GameExePath"
    MessageBox MB_OK|MB_ICONSTOP "Could not find '$GameExePath'.$\r$\n$\r$\n${APP_NAME} requires the SimCity 4 1.1.641.x executable in the selected game folder."
    Abort
  ${EndIf}

  ClearErrors
  GetDLLVersion "$GameExePath" $0 $1
  ${If} ${Errors}
    MessageBox MB_OK|MB_ICONSTOP "Could not read the version information from '$GameExePath'.$\r$\n$\r$\n${APP_NAME} requires SimCity 4 version 1.1.641.x."
    Abort
  ${EndIf}

  IntOp $2 $0 >> 16
  IntOp $2 $2 & 0xFFFF
  IntOp $3 $0 & 0xFFFF
  IntOp $4 $1 >> 16
  IntOp $4 $4 & 0xFFFF
  IntOp $5 $1 & 0xFFFF

  ${If} $2 != 1
  ${OrIf} $3 != 1
  ${OrIf} $4 != 641
    MessageBox MB_OK|MB_ICONSTOP "Unsupported SimCity 4 version detected in '$GameExePath'.$\r$\n$\r$\nFound: $2.$3.$4.$5$\r$\nRequired: 1.1.641.x$\r$\n$\r$\nPlease install the 1.1.641 update before continuing."
    Abort
  ${EndIf}
FunctionEnd

Function WarnIfNo4GBPatch
  ClearErrors
  FileOpen $0 "$GameExePath" r
  ${If} ${Errors}
    Return
  ${EndIf}

  FileSeek $0 60 SET
  ClearErrors
  FileReadByte $0 $1
  FileReadByte $0 $2
  FileReadByte $0 $3
  FileReadByte $0 $4
  ${If} ${Errors}
    FileClose $0
    Return
  ${EndIf}

  IntOp $5 $2 << 8
  IntOp $5 $5 + $1
  IntOp $6 $3 << 16
  IntOp $5 $5 + $6
  IntOp $6 $4 << 24
  IntOp $5 $5 + $6

  IntOp $5 $5 + 22
  FileSeek $0 $5 SET
  ClearErrors
  FileReadByte $0 $1
  FileReadByte $0 $2
  ${If} ${Errors}
    FileClose $0
    Return
  ${EndIf}
  FileClose $0

  IntOp $3 $2 << 8
  IntOp $3 $3 + $1
  IntOp $3 $3 & 0x20

  ${If} $3 == 0
    MessageBox MB_OK|MB_ICONEXCLAMATION "The selected SimCity 4 executable does not appear to have the 4GB patch applied.$\r$\n$\r$\n${APP_NAME} can still be installed, but applying the 4GB patch is recommended for stability."
  ${EndIf}
FunctionEnd

Function WarnIfPluginsFolderEmpty
  ClearErrors
  FindFirst $0 $1 "$SC4PluginsDir\*"
  ${If} ${Errors}
    Return
  ${EndIf}

  StrCpy $2 "1"
  plugins_dir_scan:
    StrCmp $1 "." plugins_dir_next
    StrCmp $1 ".." plugins_dir_next
    StrCpy $2 "0"
    Goto plugins_dir_done
  plugins_dir_next:
    ClearErrors
    FindNext $0 $1
    ${IfNot} ${Errors}
      Goto plugins_dir_scan
    ${EndIf}

  plugins_dir_done:
  FindClose $0

  ${If} $2 == "1"
    MessageBox MB_OK|MB_ICONEXCLAMATION "The selected Plugins folder is empty:$\r$\n$SC4PluginsDir$\r$\n$\r$\nThis usually means the wrong folder was selected!"
  ${EndIf}
FunctionEnd

Function ValidateRenderServicesDependency
  StrCpy $RenderServicesDllPath "$SC4PluginsDir\${RENDER_SERVICES_DLL_NAME}"

  ${IfNot} ${FileExists} "$RenderServicesDllPath"
    MessageBox MB_OK|MB_ICONSTOP "${APP_NAME} requires SC4RenderServices to be installed first.$\r$\n$\r$\nCould not find '$RenderServicesDllPath'.$\r$\n$\r$\nPlease install SC4RenderServices, then run this installer again.$\r$\n$\r$\nDownload page:$\r$\nhttps://community.simtropolis.com/files/file/37372-sc4-render-services/"
    Abort
  ${EndIf}

  ClearErrors
  GetDLLVersion "$RenderServicesDllPath" $0 $1
  ${If} ${Errors}
    MessageBox MB_OK|MB_ICONSTOP "Could not read the version information from '$RenderServicesDllPath'.$\r$\n$\r$\nPlease reinstall SC4RenderServices before continuing.$\r$\n$\r$\nProject page:$\r$\nhttps://github.com/caspervg/sc4-render-services"
    Abort
  ${EndIf}

  IntOp $2 $0 >> 16
  IntOp $2 $2 & 0xFFFF
  IntOp $3 $0 & 0xFFFF
  IntOp $4 $1 >> 16
  IntOp $4 $4 & 0xFFFF
  IntOp $5 $1 & 0xFFFF

  StrCpy $6 "0"
  ${If} $2 > ${MIN_RENDER_SERVICES_VERSION_MAJOR}
    StrCpy $6 "1"
  ${ElseIf} $2 == ${MIN_RENDER_SERVICES_VERSION_MAJOR}
    ${If} $3 > ${MIN_RENDER_SERVICES_VERSION_MINOR}
      StrCpy $6 "1"
    ${ElseIf} $3 == ${MIN_RENDER_SERVICES_VERSION_MINOR}
      ${If} $4 >= ${MIN_RENDER_SERVICES_VERSION_BUILD}
        StrCpy $6 "1"
      ${EndIf}
    ${EndIf}
  ${EndIf}

  ${If} $6 != "1"
    MessageBox MB_OK|MB_ICONSTOP "Unsupported SC4RenderServices version detected in '$RenderServicesDllPath'.$\r$\n$\r$\nFound: $2.$3.$4.$5$\r$\nRequired: ${MIN_RENDER_SERVICES_VERSION_MAJOR}.${MIN_RENDER_SERVICES_VERSION_MINOR}.${MIN_RENDER_SERVICES_VERSION_BUILD}.x or newer$\r$\n$\r$\nPlease update SC4RenderServices before continuing.$\r$\n$\r$\nProject page:$\r$\nhttps://github.com/caspervg/sc4-render-services"
    Abort
  ${EndIf}
FunctionEnd

Function ConfigurePathsPage
  nsDialogs::Create 1018
  Pop $Dialog
  ${If} $Dialog == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0u 0u 100% 18u "Choose where to install ${APP_NAME} files."
  ${NSD_CreateLabel} 0u 24u 100% 10u "SimCity 4 game root (contains Apps folder):"
  ${NSD_CreateDirRequest} 0u 36u 82% 12u "$GameRoot"
  Pop $HGameRoot
  ${NSD_CreateButton} 84% 36u 16% 12u "Browse..."
  Pop $HBrowseGameRoot
  ${NSD_OnClick} $HBrowseGameRoot OnBrowseGameRoot

  ${NSD_CreateLabel} 0u 56u 100% 10u "SimCity 4 Plugins directory:"
  ${NSD_CreateDirRequest} 0u 68u 82% 12u "$SC4PluginsDir"
  Pop $HPluginsDir
  ${NSD_CreateButton} 84% 68u 16% 12u "Browse..."
  Pop $HBrowsePluginsDir
  ${NSD_OnClick} $HBrowsePluginsDir OnBrowsePluginsDir

  nsDialogs::Show
FunctionEnd

Function OnBrowseGameRoot
  Pop $0
  nsDialogs::SelectFolderDialog "Select SimCity 4 game root folder" "$GameRoot"
  Pop $0
  ${If} $0 != error
    StrCpy $GameRoot $0
    Push $GameRoot
    Call NormalizeDirPath
    Pop $GameRoot
    ${NSD_SetText} $HGameRoot $GameRoot
  ${EndIf}
FunctionEnd

Function OnBrowsePluginsDir
  Pop $0
  nsDialogs::SelectFolderDialog "Select SimCity 4 Plugins folder" "$SC4PluginsDir"
  Pop $0
  ${If} $0 != error
    StrCpy $SC4PluginsDir $0
    Push $SC4PluginsDir
    Call NormalizeDirPath
    Pop $SC4PluginsDir
    ${NSD_SetText} $HPluginsDir $SC4PluginsDir
  ${EndIf}
FunctionEnd

Function WarnLegacyInstallLocations
  StrCpy $0 ""
  StrCpy $1 "0"

  !macro _CheckLegacyIn _dir _file
    ${If} ${FileExists} "${_dir}\${_file}"
      StrCpy $0 "$0$\r$\n  - ${_dir}\${_file}"
      IntOp $1 $1 + 1
    ${EndIf}
  !macroend

  !insertmacro _CheckLegacyIn "$GameRoot\Apps" "SC4ZoningExtensions.dll"

  ${If} $1 == "0"
    Return
  ${EndIf}

  MessageBox MB_YESNO|MB_ICONEXCLAMATION "The following old install-location files were found:$\r$\n$0$\r$\n$\r$\nThese may conflict with the Plugins-folder installation.$\r$\n$\r$\nDelete them now?" IDNO legacy_skip

  !macro _DeleteLegacyIn _dir _file
    ${If} ${FileExists} "${_dir}\${_file}"
      Delete "${_dir}\${_file}"
      DetailPrint "Deleted legacy ${_dir}\${_file}"
    ${EndIf}
  !macroend

  !insertmacro _DeleteLegacyIn "$GameRoot\Apps" "SC4ZoningExtensions.dll"

  legacy_skip:
FunctionEnd

Function ConfigurePathsPageLeave
  ${NSD_GetText} $HGameRoot $GameRoot
  ${NSD_GetText} $HPluginsDir $SC4PluginsDir
  Push $GameRoot
  Call NormalizeDirPath
  Pop $GameRoot
  Push $SC4PluginsDir
  Call NormalizeDirPath
  Pop $SC4PluginsDir

  ${If} $GameRoot == ""
    MessageBox MB_OK|MB_ICONEXCLAMATION "Game root cannot be empty."
    Abort
  ${EndIf}

  ${If} $SC4PluginsDir == ""
    MessageBox MB_OK|MB_ICONEXCLAMATION "Plugins directory cannot be empty."
    Abort
  ${EndIf}

  ${GetParent} "$SC4PluginsDir" $SC4ToolsDir
  ${If} $SC4ToolsDir == ""
    MessageBox MB_OK|MB_ICONEXCLAMATION "Could not determine parent folder of Plugins directory."
    Abort
  ${EndIf}
  StrCpy $SC4ToolsDir "$SC4ToolsDir\${APP_TOOLS_SUBDIR}"

  ${IfNot} ${FileExists} "$GameRoot\Apps\*.*"
    MessageBox MB_OK|MB_ICONSTOP "Could not find '$GameRoot\Apps'.$\r$\n$\r$\nPlease select your SimCity 4 game root folder (the folder that contains 'Apps')."
    Abort
  ${EndIf}

  Call ValidateGameExecutable
  Call WarnIfNo4GBPatch
  Call WarnIfPluginsFolderEmpty
  Call ValidateRenderServicesDependency
  Call WarnLegacyInstallLocations
FunctionEnd

Function ConfigureSummaryPage
  nsDialogs::Create 1018
  Pop $Dialog
  ${If} $Dialog == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0u 0u 100% 12u "Review settings before installation:"
  ${NSD_CreateLabel} 0u 16u 100% 70u "Game root:$\r$\n$GameRoot$\r$\n$\r$\nPlugins dir:$\r$\n$SC4PluginsDir$\r$\n$\r$\nSupport/output dir:$\r$\n$SC4ToolsDir"
  Pop $HSummaryText

  nsDialogs::Show
FunctionEnd

Section "Install"
  SetShellVarContext current

  CreateDirectory "$SC4PluginsDir"
  SetOutPath "$SC4PluginsDir"
  File "SC4ZoningExtensions.dll"

  CreateDirectory "$SC4ToolsDir"
  SetOutPath "$SC4ToolsDir"
  File "${README_PATH}"
  File "${LICENSE_PATH}"
  File "${THIRD_PARTY_NOTICES_PATH}"

  WriteUninstaller "$SC4ToolsDir\Uninstall-SC4ZoningExtensions.exe"

  WriteRegStr HKCU "${APP_REG_KEY}" "GameRoot" "$GameRoot"
  WriteRegStr HKCU "${APP_REG_KEY}" "PluginsDir" "$SC4PluginsDir"
  WriteRegStr HKCU "${APP_REG_KEY}" "ToolsDir" "$SC4ToolsDir"

  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME} ${APP_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "SC4 Zoning Extensions"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$SC4ToolsDir"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" "$\"$SC4ToolsDir\Uninstall-SC4ZoningExtensions.exe$\""
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Function OpenToolsFolder
  SetShellVarContext current
  ExecShell "open" "$SC4ToolsDir"
FunctionEnd

Function un.onInit
  SetShellVarContext current
  ReadRegStr $GameRoot HKCU "${APP_REG_KEY}" "GameRoot"
  ReadRegStr $SC4PluginsDir HKCU "${APP_REG_KEY}" "PluginsDir"
  ReadRegStr $SC4ToolsDir HKCU "${APP_REG_KEY}" "ToolsDir"

  ${If} $GameRoot == ""
    ; SC4 is a 32-bit app, so read from the 32-bit registry view.
    StrCpy $GameRoot "$PROGRAMFILES32\SimCity 4 Deluxe Edition"
    SetRegView 32
    ReadRegStr $0 HKLM "SOFTWARE\Maxis\SimCity 4" "Install Dir"
    ${If} $0 != ""
      StrCpy $GameRoot $0
    ${EndIf}
  ${EndIf}
  ${If} $SC4PluginsDir == ""
    StrCpy $SC4PluginsDir "$DOCUMENTS\SimCity 4\Plugins"
  ${EndIf}
  ${If} $SC4ToolsDir == ""
    ${GetParent} "$SC4PluginsDir" $SC4ToolsDir
    StrCpy $SC4ToolsDir "$SC4ToolsDir\${APP_TOOLS_SUBDIR}"
  ${EndIf}
FunctionEnd

Section "Uninstall"
  SetShellVarContext current

  Delete "$SC4PluginsDir\SC4ZoningExtensions.dll"

  Delete "$SC4ToolsDir\README.txt"
  Delete "$SC4ToolsDir\LICENSE.txt"
  Delete "$SC4ToolsDir\THIRD_PARTY_NOTICES.txt"
  Delete "$SC4ToolsDir\Uninstall-SC4ZoningExtensions.exe"
  RMDir "$SC4ToolsDir"

  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "${APP_REG_KEY}"
SectionEnd
