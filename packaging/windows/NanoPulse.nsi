!include "MUI2.nsh"

!ifndef VERSION
!error "VERSION is required"
!endif
!ifndef STAGE_DIR
!error "STAGE_DIR is required"
!endif
!ifndef OUTPUT_FILE
!define OUTPUT_FILE "NanoPulse-installer-x64-v${VERSION}.exe"
!endif

Name "NanoPulse ${VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$PROGRAMFILES64\NanoPulse"
InstallDirRegKey HKLM "Software\NanoPulse" "InstallDir"
RequestExecutionLevel admin

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_FINISHPAGE_RUN "$INSTDIR\NanoPulse.exe"
!insertmacro MUI_LANGUAGE "English"

Section "NanoPulse" SEC_MAIN
  SetOutPath "$INSTDIR"
  File /r "${STAGE_DIR}\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\NanoPulse"
  CreateShortcut "$SMPROGRAMS\NanoPulse\NanoPulse.lnk" "$INSTDIR\NanoPulse.exe"
  CreateShortcut "$SMPROGRAMS\NanoPulse\Uninstall NanoPulse.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\NanoPulse" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NanoPulse" "DisplayName" "NanoPulse"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NanoPulse" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NanoPulse" "Publisher" "NanoPulse"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NanoPulse" "UninstallString" '"$INSTDIR\Uninstall.exe"'
SectionEnd

Section "Uninstall"
  RMDir /r "$INSTDIR"
  RMDir /r "$SMPROGRAMS\NanoPulse"
  DeleteRegKey HKLM "Software\NanoPulse"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NanoPulse"
SectionEnd
