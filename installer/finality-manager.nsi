; NSIS installer script for Finality Manager.
;
; Build with:  make nsis
; or directly: makensis -DBUNDLE_DIR=... -DOUTFILE=... -DAPP_VERSION=... \
;                       -DAPP_ICON=... installer/finality-manager.nsi
;
; The bundle directory (build/windows/bundle) is created by `make bundle-windows`
; and already contains the executable plus the full GTK4 runtime.

Unicode true

!ifndef BUNDLE_DIR
  !define BUNDLE_DIR "build\windows\bundle"
!endif
!ifndef OUTFILE
  !define OUTFILE "build\windows\Finality-Manager-Setup.exe"
!endif
!ifndef APP_VERSION
  !define APP_VERSION "1.0.0"
!endif
!ifndef APP_ICON
  !define APP_ICON "res\logo.ico"
!endif
; Optional license file shown on a license page when present.
!ifndef APP_LICENSE
  !define /ifndef APP_LICENSE ""
!endif

!define APP_NAME "Finality Manager"
!define APP_PUBLISHER "Finality Manager"
!define APP_EXE "finality-manager.exe"
!define APP_ID "FinalityManager"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}"

Name "${APP_NAME}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "Software\${APP_ID}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
SetCompressorDictSize 32

VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "FileDescription" "${APP_NAME} Setup"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey "LegalCopyright" "${APP_PUBLISHER}"

!include "MUI2.nsh"
!include "FileFunc.nsh"

!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "${APP_NAME} 安装向导"
!define MUI_WELCOMEPAGE_TEXT "本向导将引导你完成 ${APP_NAME} 的安装。$\r$\n$\r$\n点击“下一步”继续。"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "运行 ${APP_NAME}"
!define MUI_FINISHPAGE_TITLE "${APP_NAME} 安装完成"
!define MUI_FINISHPAGE_TEXT "${APP_NAME} 已安装到你的电脑。$\r$\n$\r$\n点击“完成”关闭向导。"

!insertmacro MUI_PAGE_WELCOME
!if "${APP_LICENSE}" != ""
  !insertmacro MUI_PAGE_LICENSE "${APP_LICENSE}"
!endif
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"

; Localized section names and descriptions.
LangString DESC_Main       ${LANG_ENGLISH}   "Install Finality Manager and its runtime."
LangString DESC_Main       ${LANG_SIMPCHINESE} "安装 Finality Manager 主程序及运行库。"
LangString DESC_Desktop    ${LANG_ENGLISH}   "Create a desktop shortcut."
LangString DESC_Desktop    ${LANG_SIMPCHINESE} "在桌面创建快捷方式。"
LangString SEC_Main        ${LANG_ENGLISH}   "Finality Manager"
LangString SEC_Main        ${LANG_SIMPCHINESE} "Finality Manager"
LangString SEC_Desktop     ${LANG_ENGLISH}   "Desktop shortcut"
LangString SEC_Desktop     ${LANG_SIMPCHINESE} "桌面快捷方式"

Section "$(SEC_Main)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"

  ; Ship the whole self-contained bundle (exe + GTK4 runtime + resources).
  File /r "${BUNDLE_DIR}\*.*"

  ; Shortcuts.
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\卸载 ${APP_NAME}.lnk" "$INSTDIR\Uninstall.exe"

  ; Uninstaller and Add/Remove Programs entry.
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\${APP_ID}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1

  ; Estimated install size for the Add/Remove Programs list.
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "$(SEC_Desktop)" SecDesktop
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_Main)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_Desktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR"

  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\卸载 ${APP_NAME}.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"
  Delete "$DESKTOP\${APP_NAME}.lnk"

  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "Software\${APP_ID}"
SectionEnd
