; audacious.nsi
; Copyright 2013-2016 Carlo Bramini and John Lindgren
;
; Redistribution and use in source and binary forms, with or without
; modification, are permitted provided that the following conditions are met:
;
; 1. Redistributions of source code must retain the above copyright notice,
;    this list of conditions, and the following disclaimer.
;
; 2. Redistributions in binary form must reproduce the above copyright notice,
;    this list of conditions, and the following disclaimer in the documentation
;    provided with the distribution.
;
; This software is provided "as is" and without any warranty, express or
; implied. In no event shall the authors be liable for any damages arising from
; the use of this software.

; Imports
!include "MUI2.nsh"

; Version
!define VERSION "3.7.2"

; Program name
Name "Audacious ${VERSION}"

; Installer file name
OutFile "audacious-${VERSION}-win32.exe"

; Installer icon
!define MUI_ICON "audacious.ico"
!define MUI_UNICON "audacious.ico"

; Installer options
RequestExecutionLevel admin
SetCompressor /SOLID lzma

; Default installation directory
InstallDir "$PROGRAMFILES\Audacious"

; Registry uninstall key
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Audacious"

; Path to uninstaller
!define UNINSTALLER "$INSTDIR\uninstall.exe"

; Installer pages
!insertmacro MUI_PAGE_LICENSE README.txt
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"

Section "Audacious" InstallSection
  SectionIn 1 2 RO

  SetOutPath "$INSTDIR"
  File "README.txt"

  SetOutPath "$INSTDIR\bin"
  File /r "bin\*.*"

  SetOutPath "$INSTDIR\etc"
  File /r "etc\*.*"

  SetOutPath "$INSTDIR\lib"
  File /r "lib\*.*"

  SetOutPath "$INSTDIR\share"
  File /r "share\*.*"

  ; create uninstaller
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "Audacious"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "Audacious developers"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "${UNINSTALLER}"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" "${UNINSTALLER}"
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1

  ; estimate installed size
  SectionGetSize InstallSection $0
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" $0

  WriteUninstaller ${UNINSTALLER}

SectionEnd

; Optional sections
Section "Add to Start Menu" StartMenuSection
  SectionIn 1 2

  SetShellVarContext all
  SetOutPath "$INSTDIR\bin" ; sets the shortcut's working directory
  CreateShortCut "$SMPROGRAMS\Audacious.lnk" "$INSTDIR\bin\audacious.exe"

SectionEnd

Section "Add to Desktop" DesktopSection
  SectionIn 1 2

  SetShellVarContext all
  SetOutPath "$INSTDIR\bin" ; sets the shortcut's working directory
  CreateShortCut "$DESKTOP\Audacious.lnk" "$INSTDIR\bin\audacious.exe"

SectionEnd

Section "Uninstall" UninstallSection

  RMDir /r "$INSTDIR"

  SetShellVarContext all
  Delete "$SMPROGRAMS\Audacious.lnk"
  Delete "$DESKTOP\Audacious.lnk"

  DeleteRegKey HKLM "${UNINST_KEY}"

SectionEnd
