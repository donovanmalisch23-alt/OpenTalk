; SPDX-License-Identifier: GPL-3.0-only
; OpenTalk installer for Windows (NSIS 3).
;
; Installs for the current user only, into %LOCALAPPDATA%\Programs\OpenTalk, so no
; administrator rights are needed. Settings and server data stay in the user's
; application data folder and are kept when OpenTalk is uninstalled.
;
; Build (CI does this after packaging the portable folder):
;   makensis /DVERSION=1.0.0 /DDISPLAY_VERSION=1.0.0-rc.1 /DSOURCE=<portable folder> /DOUTFILE=<setup.exe> opentalk.nsi
; VERSION is numeric (Windows file versions need it); DISPLAY_VERSION is what people see.

Unicode true
!include "MUI2.nsh"

!ifndef VERSION
  !error "Pass /DVERSION=<x.y.z>"
!endif
!ifndef DISPLAY_VERSION
  !define DISPLAY_VERSION "${VERSION}"
!endif
!ifndef SOURCE
  !error "Pass /DSOURCE=<folder with the packaged files>"
!endif
!ifndef OUTFILE
  !define OUTFILE "OpenTalk-${DISPLAY_VERSION}-setup-x64.exe"
!endif
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenTalk"
!define UNINSTALLER "Uninstall OpenTalk.exe"

Name "OpenTalk ${DISPLAY_VERSION}"
OutFile "${OUTFILE}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\OpenTalk"
InstallDirRegKey HKCU "Software\OpenTalk" "InstallDir"
SetCompressor /SOLID lzma
ManifestDPIAware true

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "OpenTalk"
VIAddVersionKey "ProductVersion" "${DISPLAY_VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "OpenTalk installer"
VIAddVersionKey "CompanyName" "The OpenTalk contributors"
VIAddVersionKey "LegalCopyright" "GNU General Public License version 3"

!define MUI_ABORTWARNING
; makensis runs from this script's folder, so the path is relative to it.
!define MUI_ICON "..\..\src\app\resources\opentalk.ico"
!define MUI_UNICON "..\..\src\app\resources\opentalk.ico"
!define MUI_WELCOMEPAGE_TEXT "This installs OpenTalk ${DISPLAY_VERSION}, an accessible voice and text chat program, for you only. No administrator rights are needed.$\r$\n$\r$\nTo use OpenTalk from a USB stick or a folder without installing, download the portable zip instead."
!define MUI_FINISHPAGE_RUN "$INSTDIR\OpenTalk.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start OpenTalk now"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\README.md"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Show the read-me file"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SOURCE}\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "OpenTalk" SectionMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  ; Everything from the portable package except its portable-mode marker and data.
  File /r /x "OpenTalk.portable" /x "data" "${SOURCE}\*.*"
  WriteUninstaller "$INSTDIR\${UNINSTALLER}"

  CreateDirectory "$SMPROGRAMS\OpenTalk"
  CreateShortcut "$SMPROGRAMS\OpenTalk\OpenTalk.lnk" "$INSTDIR\OpenTalk.exe" "" "$INSTDIR\OpenTalk.exe" 0 \
      SW_SHOWNORMAL "" "Accessible voice and text chat"
  CreateShortcut "$SMPROGRAMS\OpenTalk\Uninstall OpenTalk.lnk" "$INSTDIR\${UNINSTALLER}"

  WriteRegStr HKCU "Software\OpenTalk" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "OpenTalk"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${DISPLAY_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "The OpenTalk contributors"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\OpenTalk.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/donovanmalisch23-alt/opentalk"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\${UNINSTALLER}"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\${UNINSTALLER}" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  ; Only what the installer put there, never "everything in the folder", so choosing an
  ; unusual install folder can never delete someone's other files.
  Delete "$INSTDIR\*.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\THIRD-PARTY-NOTICES.txt"
  Delete "$INSTDIR\qt.conf"
  RMDir /r "$INSTDIR\examples"
  RMDir /r "$INSTDIR\licenses"
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\tls"
  RMDir /r "$INSTDIR\sqldrivers"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\iconengines"
  RMDir /r "$INSTDIR\generic"
  RMDir /r "$INSTDIR\networkinformation"
  RMDir /r "$INSTDIR\translations"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\OpenTalk\OpenTalk.lnk"
  Delete "$SMPROGRAMS\OpenTalk\Uninstall OpenTalk.lnk"
  RMDir "$SMPROGRAMS\OpenTalk"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "Software\OpenTalk"
SectionEnd
