; LiteIME Windows x64/WOW64 TSF Installer (NSIS 3+ Unicode)
; Copyright (c) Over9rowth - #燧人计划

!ifndef APP_VERSION
  !define APP_VERSION "0.1.0"
!endif
!ifndef STAGE_DIR
  !define STAGE_DIR "..\release\stage"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "..\release\LiteIME-Setup-${APP_VERSION}-win64.exe"
!endif
!ifndef ROOT_DIR
  !define ROOT_DIR ".."
!endif

!define APP_NAME "LiteIME"
!define APP_DISPLAY_NAME "LiteIME 极简双语输入法"
!define APP_PUBLISHER "Over9rowth · #燧人计划"
!define APP_URL "https://github.com/Over9rowth/LiteIME"
!define TSF_TIP "0804:{A9B4C21E-7D3A-4F8B-9C12-5E7A1D3B8C40}{B8C5D32F-8E4B-409C-AD23-6F8B2E4C9D51}"
!define UNINST_REG_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\LiteIME"

Unicode True
Target amd64-unicode
SetCompressor /SOLID lzma
SetCompressorDictSize 32
RequestExecutionLevel admin
ManifestDPIAware true

!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

Name "${APP_DISPLAY_NAME} v${APP_VERSION}"
OutFile "${OUT_FILE}"
InstallDir "$PROGRAMFILES64\LiteIME"
InstallDirRegKey HKLM "Software\LiteIME" "InstallDir"
BrandingText "${APP_DISPLAY_NAME} v${APP_VERSION}"

VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey /LANG=2052 "ProductName" "${APP_DISPLAY_NAME}"
VIAddVersionKey /LANG=2052 "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey /LANG=2052 "FileDescription" "LiteIME 极简双语输入法安装程序"
VIAddVersionKey /LANG=2052 "FileVersion" "${APP_VERSION}.0"
VIAddVersionKey /LANG=2052 "ProductVersion" "${APP_VERSION}.0"
VIAddVersionKey /LANG=2052 "LegalCopyright" "Over9rowth"

!define MUI_ICON "${ROOT_DIR}\assets\liteime.ico"
!define MUI_UNICON "${ROOT_DIR}\assets\liteime.ico"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "欢迎安装 ${APP_DISPLAY_NAME} v${APP_VERSION}"
!define MUI_WELCOMEPAGE_TEXT "极简、纯净、零遥测的 Windows 中英文双语 TSF 输入法（C++20 + Direct2D）。$\r$\n$\r$\n本向导将安装 64 位与 32 位系统输入法组件、离线词库与设置中心，并自动注册至 Windows 输入法列表。$\r$\n$\r$\n点击「下一步」继续。"

!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "立即启动 LiteIME 候选框宿主程序"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchLiteIMEHost
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\docs\LiteIME_User_Manual.pdf"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "查看《LiteIME 使用说明书 (PDF)》"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

!macro SafeMoveIfLocked TargetPath
  ${If} ${FileExists} "${TargetPath}"
    Delete "${TargetPath}"
    ${If} ${FileExists} "${TargetPath}"
      GetTempFileName $R0 "$INSTDIR"
      Delete "$R0"
      Rename "${TargetPath}" "$R0"
      Delete /REBOOTOK "$R0"
    ${EndIf}
  ${EndIf}
!macroend

Function .onInit
  SetRegView 64
FunctionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

Function LaunchLiteIMEHost
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\LiteIMEHost.exe"'
FunctionEnd

Function EnableLiteIMETip
  System::Call 'input::InstallLayoutOrTip(w "${TSF_TIP}", i 0) i .r0'
  nsExec::Exec `"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -Command "try { $$tip='${TSF_TIP}'; $$langs = Get-WinUserLanguageList; $$zh = $$langs | Where-Object { $$_.LanguageTag -like 'zh*' } | Select-Object -First 1; if ($$zh -and -not ($$zh.InputMethodTips -contains $$tip)) { $$zh.InputMethodTips.Add($$tip); Set-WinUserLanguageList $$langs -Force } } catch {}"`
  Pop $0
FunctionEnd

Function un.DisableLiteIMETip
  System::Call 'input::InstallLayoutOrTip(w "${TSF_TIP}", i 1) i .r0'
  nsExec::Exec `"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -Command "try { $$tip='${TSF_TIP}'; $$langs = Get-WinUserLanguageList; $$zh = $$langs | Where-Object { $$_.LanguageTag -like 'zh*' } | Select-Object -First 1; if ($$zh -and ($$zh.InputMethodTips -contains $$tip)) { [void]$$zh.InputMethodTips.Remove($$tip); Set-WinUserLanguageList $$langs -Force } } catch {}"`
  Pop $0
FunctionEnd

Section "LiteIME 核心组件 (必选)" SecMain
  SectionIn RO
  SetRegView 64

  DetailPrint "正在停止运行中的 LiteIMEHost 进程..."
  nsExec::ExecToLog 'taskkill /F /IM LiteIMEHost.exe'
  Pop $0
  Sleep 300

  SetOutPath "$INSTDIR"
  ; Clean leftover .old files from prior in-place upgrades if unlocked
  Delete "$INSTDIR\*.old*"
  Delete "$INSTDIR\tmp*.tmp"

  !insertmacro SafeMoveIfLocked "$INSTDIR\LiteIMEHost.exe"
  !insertmacro SafeMoveIfLocked "$INSTDIR\LiteIME64.dll"
  !insertmacro SafeMoveIfLocked "$INSTDIR\LiteIME32.dll"
  !insertmacro SafeMoveIfLocked "$INSTDIR\dict_compiler.exe"

  DetailPrint "正在写入核心程序与词库文件..."
  File "${STAGE_DIR}\LiteIMEHost.exe"
  File "${STAGE_DIR}\LiteIME64.dll"
  File "${STAGE_DIR}\LiteIME32.dll"
  File "${STAGE_DIR}\dict_compiler.exe"
  File "${STAGE_DIR}\bigram.tsv"

  SetOutPath "$INSTDIR\data"
  File "${STAGE_DIR}\data\liteime.dict"

  SetOutPath "$INSTDIR\data\packs"
  File "${STAGE_DIR}\data\packs\common_en.dict"
  File "${STAGE_DIR}\data\packs\computer.dict"
  File "${STAGE_DIR}\data\packs\ext_cn.dict"
  File "${STAGE_DIR}\data\packs\pro_fields.dict"

  SetOutPath "$INSTDIR\docs"
  File "${STAGE_DIR}\docs\LiteIME_User_Manual.pdf"

  SetOutPath "$INSTDIR\assets"
  File "${STAGE_DIR}\assets\liteime.ico"

  SetOutPath "$INSTDIR"
  DetailPrint "正在配置 UWP / AppContainer 只读权限..."
  nsExec::ExecToLog 'icacls "$INSTDIR" /grant "*S-1-15-2-1:(OI)(CI)(RX)" /T'
  Pop $0

  DetailPrint "正在注册 64 位与 32 位 TSF 输入法组件..."
  nsExec::ExecToLog '"$SYSDIR\regsvr32.exe" /s "$INSTDIR\LiteIME64.dll"'
  Pop $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP|MB_OK "64 位 TSF 组件注册失败 (regsvr32 返回码: $0)。"
    Abort
  ${EndIf}

  ${If} ${FileExists} "$WINDIR\SysWOW64\regsvr32.exe"
    nsExec::ExecToLog '"$WINDIR\SysWOW64\regsvr32.exe" /s "$INSTDIR\LiteIME32.dll"'
    Pop $0
  ${EndIf}

  DetailPrint "正在启用 Windows 输入法配置..."
  Call EnableLiteIMETip

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Start Menu shortcuts
  CreateDirectory "$SMPROGRAMS\LiteIME"
  CreateShortCut "$SMPROGRAMS\LiteIME\启动 LiteIME 输入法宿主.lnk" "$INSTDIR\LiteIMEHost.exe" "" "$INSTDIR\assets\liteime.ico" 0
  CreateShortCut "$SMPROGRAMS\LiteIME\LiteIME 使用说明书 (PDF).lnk" "$INSTDIR\docs\LiteIME_User_Manual.pdf"
  CreateShortCut "$SMPROGRAMS\LiteIME\卸载 LiteIME.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\assets\liteime.ico" 0

  ; Registry & Add/Remove Programs metadata
  WriteRegStr HKLM "Software\LiteIME" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_REG_KEY}" "DisplayName" "${APP_DISPLAY_NAME}"
  WriteRegStr HKLM "${UNINST_REG_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "${UNINST_REG_KEY}" "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKLM "${UNINST_REG_KEY}" "DisplayIcon" "$INSTDIR\assets\liteime.ico"
  WriteRegStr HKLM "${UNINST_REG_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINST_REG_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegStr HKLM "${UNINST_REG_KEY}" "URLInfoAbout" "${APP_URL}"
  WriteRegDWORD HKLM "${UNINST_REG_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_REG_KEY}" "NoRepair" 1

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNINST_REG_KEY}" "EstimatedSize" "$0"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "LiteIMEHost" '"$INSTDIR\LiteIMEHost.exe"'
SectionEnd

Section "Uninstall"
  SetRegView 64

  DetailPrint "正在停止 LiteIMEHost 进程..."
  nsExec::ExecToLog 'taskkill /F /IM LiteIMEHost.exe'
  Pop $0
  Sleep 300

  DetailPrint "正在从 Windows 输入法列表移除 LiteIME..."
  Call un.DisableLiteIMETip

  DetailPrint "正在注销 64 位与 32 位 TSF 输入法组件..."
  ${If} ${FileExists} "$INSTDIR\LiteIME64.dll"
    nsExec::ExecToLog '"$SYSDIR\regsvr32.exe" /s /u "$INSTDIR\LiteIME64.dll"'
    Pop $0
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\LiteIME32.dll"
    ${If} ${FileExists} "$WINDIR\SysWOW64\regsvr32.exe"
      nsExec::ExecToLog '"$WINDIR\SysWOW64\regsvr32.exe" /s /u "$INSTDIR\LiteIME32.dll"'
      Pop $0
    ${EndIf}
  ${EndIf}

  Delete "$SMPROGRAMS\LiteIME\启动 LiteIME 输入法宿主.lnk"
  Delete "$SMPROGRAMS\LiteIME\LiteIME 使用说明书 (PDF).lnk"
  Delete "$SMPROGRAMS\LiteIME\卸载 LiteIME.lnk"
  RMDir "$SMPROGRAMS\LiteIME"

  Delete "$INSTDIR\LiteIMEHost.exe"
  Delete /REBOOTOK "$INSTDIR\LiteIMEHost.exe"
  Delete "$INSTDIR\LiteIME64.dll"
  Delete /REBOOTOK "$INSTDIR\LiteIME64.dll"
  Delete "$INSTDIR\LiteIME32.dll"
  Delete /REBOOTOK "$INSTDIR\LiteIME32.dll"
  Delete "$INSTDIR\dict_compiler.exe"
  Delete "$INSTDIR\bigram.tsv"
  Delete "$INSTDIR\Uninstall.exe"

  RMDir /r "$INSTDIR\data"
  RMDir /r "$INSTDIR\docs"
  RMDir /r "$INSTDIR\assets"
  Delete "$INSTDIR\*.old*"
  Delete "$INSTDIR\tmp*.tmp"
  RMDir /REBOOTOK "$INSTDIR"

  DeleteRegKey HKLM "${UNINST_REG_KEY}"
  DeleteRegKey HKLM "Software\LiteIME"
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "LiteIMEHost"
SectionEnd
