; TouchSplit installer (NSIS 3)
; Per-user install - no admin rights / UAC prompt needed.
; Build:  make installer      (or: makensis -DVERSION=1.1.0 installer/touchsplit.nsi)

Unicode true
SetCompressor /SOLID lzma

!ifndef VERSION
  !error "Pass the version, e.g. makensis -DVERSION=1.1.0 touchsplit.nsi"
!endif
!ifndef EXE_PATH
  !define EXE_PATH "..\TouchSplit.exe"
!endif
!ifndef OUT_DIR
  !define OUT_DIR "..\dist"
!endif

!define APPNAME   "TouchSplit"
!define EXE       "TouchSplit.exe"
!define WNDCLASS  "TouchSplitWnd"
!define REGKEY    "Software\TouchSplit"
!define RUNKEY    "Software\Microsoft\Windows\CurrentVersion\Run"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\TouchSplit"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "Sections.nsh"
!include "WinMessages.nsh"
!include "FileFunc.nsh"

Name "${APPNAME} ${VERSION}"
OutFile "${OUT_DIR}\TouchSplit-Setup-${VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\TouchSplit"
InstallDirRegKey HKCU "${REGKEY}" "InstallDir"
RequestExecutionLevel user
BrandingText "TouchSplit ${VERSION}"
ShowInstDetails nevershow
ShowUninstDetails nevershow

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName"     "${APPNAME}"
VIAddVersionKey "FileDescription" "${APPNAME} installer"
VIAddVersionKey "FileVersion"     "${VERSION}"
VIAddVersionKey "ProductVersion"  "${VERSION}"
VIAddVersionKey "LegalCopyright"  "MIT License"

Var WasRunning

; ---------------------------------------------------------------- UI
!define MUI_ICON   "..\res\on.ico"
!define MUI_UNICON "..\res\off.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Welcome to TouchSplit ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "TouchSplit lets you use a touchscreen and a mouse at the same time.$\r$\n$\r$\nTapping the touchscreen no longer moves your mouse pointer or takes focus away from your game or app.$\r$\n$\r$\nNo administrator rights are needed - TouchSplit installs just for you.$\r$\n$\r$\nClick Next to continue."
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start TouchSplit now"
!define MUI_FINISHPAGE_TEXT "TouchSplit is installed.$\r$\n$\r$\nLook for the blue icon in the system tray (you may need to click the ^ arrow). Click it for settings, or press Ctrl+Alt+F12 to pause/resume."
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\README.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Show the README"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------- helpers
; Ask any running TouchSplit to exit cleanly (it restores the windows it changed),
; then make sure it's gone so its files can be replaced.
!macro CLOSE_TOUCHSPLIT UN
  StrCpy $WasRunning "0"
  StrCpy $1 0
  ${Do}
    FindWindow $0 "${WNDCLASS}" ""
    ${If} $0 == 0
      ${Break}
    ${EndIf}
    StrCpy $WasRunning "1"
    SendMessage $0 ${WM_CLOSE} 0 0 /TIMEOUT=2000
    Sleep 300
    IntOp $1 $1 + 1
    ${If} $1 > 10
      ${Break}
    ${EndIf}
  ${Loop}
  nsExec::Exec 'taskkill /im ${EXE} /f'
  Pop $0
  Sleep 300
!macroend

; ---------------------------------------------------------------- sections
Section "TouchSplit (required)" SecMain
  SectionIn RO
  !insertmacro CLOSE_TOUCHSPLIT ""

  SetOutPath "$INSTDIR"
  File "/oname=${EXE}" "${EXE_PATH}"
  File "/oname=README.txt" "..\docs\README.txt"
  File "/oname=LICENSE.txt" "..\LICENSE"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "${REGKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${REGKEY}" "Version" "${VERSION}"

  ; Entry in Settings > Apps / Programs and Features
  WriteRegStr   HKCU "${UNINSTKEY}" "DisplayName"          "TouchSplit"
  WriteRegStr   HKCU "${UNINSTKEY}" "DisplayVersion"       "${VERSION}"
  WriteRegStr   HKCU "${UNINSTKEY}" "Publisher"            "TouchSplit"
  WriteRegStr   HKCU "${UNINSTKEY}" "DisplayIcon"          "$INSTDIR\${EXE},0"
  WriteRegStr   HKCU "${UNINSTKEY}" "InstallLocation"      "$INSTDIR"
  WriteRegStr   HKCU "${UNINSTKEY}" "UninstallString"      '"$INSTDIR\Uninstall.exe"'
  WriteRegStr   HKCU "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  !ifdef REPO_URL
  WriteRegStr   HKCU "${UNINSTKEY}" "URLInfoAbout"         "${REPO_URL}"
  WriteRegStr   HKCU "${UNINSTKEY}" "HelpLink"             "${REPO_URL}/issues"
  !endif
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINSTKEY}" "EstimatedSize" "$0"
SectionEnd

Section "Start Menu shortcut" SecStartMenu
  CreateShortCut "$SMPROGRAMS\TouchSplit.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
SectionEnd

Section "Start with Windows" SecAutostart
  WriteRegStr HKCU "${RUNKEY}" "TouchSplit" '"$INSTDIR\${EXE}"'
SectionEnd

Section /o "Desktop shortcut" SecDesktop
  CreateShortCut "$DESKTOP\TouchSplit.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
SectionEnd

Section "-post"
  ${IfNot} ${SectionIsSelected} ${SecAutostart}
    DeleteRegValue HKCU "${RUNKEY}" "TouchSplit"
  ${EndIf}
  ${IfNot} ${SectionIsSelected} ${SecStartMenu}
    Delete "$SMPROGRAMS\TouchSplit.lnk"
  ${EndIf}
  ; Silent upgrades (e.g. setup.exe /S) restart TouchSplit if it was running.
  ${If} ${Silent}
  ${AndIf} $WasRunning == "1"
    Exec '"$INSTDIR\${EXE}"'
  ${EndIf}
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain}      "The TouchSplit app."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Add TouchSplit to the Start menu."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAutostart} "Start TouchSplit automatically when you sign in to Windows."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop}   "Put a TouchSplit shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------------------------------------------------------------- uninstall
Section "Uninstall"
  !insertmacro CLOSE_TOUCHSPLIT "un."

  Delete "$INSTDIR\${EXE}"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\TouchSplit.ini"
  Delete "$INSTDIR\TouchSplit.log"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir  "$INSTDIR"

  Delete "$SMPROGRAMS\TouchSplit.lnk"
  Delete "$DESKTOP\TouchSplit.lnk"

  ; only remove the autostart entry if it points at this install
  ReadRegStr $0 HKCU "${RUNKEY}" "TouchSplit"
  ${If} $0 == '"$INSTDIR\${EXE}"'
    DeleteRegValue HKCU "${RUNKEY}" "TouchSplit"
  ${EndIf}

  DeleteRegKey HKCU "${UNINSTKEY}"
  DeleteRegKey HKCU "${REGKEY}"
SectionEnd
