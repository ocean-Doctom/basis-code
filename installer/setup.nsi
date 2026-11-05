Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
Name "Basis Code"
OutFile "${OUTPUT}\BasisCode-Setup-1.1.0-x64.exe"
InstallDir "$LOCALAPPDATA\Programs\Basis Code"
InstallDirRegKey HKCU "Software\BasisCode" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
Icon "..\icons\basis-code.ico"
UninstallIcon "..\icons\basis-code.ico"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"
Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "Basis Code requires 64-bit Windows."
    Abort
  ${EndIf}
FunctionEnd
Section "Basis Code"
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD}\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\Basis Code"
  CreateShortcut "$SMPROGRAMS\Basis Code\Basis Code.lnk" "$INSTDIR\BasisCode.exe"
  CreateShortcut "$SMPROGRAMS\Basis Code\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\Basis Code.lnk" "$INSTDIR\BasisCode.exe"
  WriteRegStr HKCU "Software\BasisCode" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BasisCode" "DisplayName" "Basis Code"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BasisCode" "DisplayVersion" "1.1.0"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BasisCode" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BasisCode" "DisplayIcon" "$INSTDIR\BasisCode.exe"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BasisCode" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BasisCode" "NoRepair" 1
SectionEnd
Section "Uninstall"
  !include "${PAYLOAD}\..\uninstall-files.nsh"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$DESKTOP\Basis Code.lnk"
  Delete "$SMPROGRAMS\Basis Code\Basis Code.lnk"
  Delete "$SMPROGRAMS\Basis Code\Uninstall.lnk"
  RMDir "$SMPROGRAMS\Basis Code"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BasisCode"
  DeleteRegKey HKCU "Software\BasisCode"
SectionEnd
