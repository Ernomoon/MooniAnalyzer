; Mooni Analyzer — Windows installer (Inno Setup 6)
; Created by Ernomoon.
; Build:  ISCC.exe /DAppVersion=1.0.0 /DBuildDir=<...>\build\MooniAnalyzer_artefacts\Release installer\MooniAnalyzer.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\MooniAnalyzer_artefacts\Release"
#endif

#define AppName "Mooni Analyzer"
#define Publisher "Ernomoon"

[Setup]
AppId={{6F0D2B3A-5C1E-4E7B-9A40-6D6F6F6E6931}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
AppCopyright=Created by Ernomoon
DefaultDirName={autopf}\{#Publisher}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputBaseFilename=MooniAnalyzer-{#AppVersion}-Windows-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName={#AppName} (by Ernomoon)
LicenseFile=..\LICENSE.txt
InfoAfterFile=..\docs\AFTER_INSTALL.txt
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#Publisher}
VersionInfoDescription={#AppName} — mastering assistant VST3 by Ernomoon

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "VST3 + Standalone"
Name: "vst3"; Description: "VST3"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 (C:\Program Files\Common Files\VST3)"; Types: full vst3 custom; Flags: fixed
Name: "standalone"; Description: "Standalone"; Types: full

[Files]
; The VST3 bundle is a folder; it goes to the standard system VST3 location that FL Studio scans.
Source: "{#BuildDir}\VST3\Mooni Analyzer.vst3\*"; DestDir: "{commoncf64}\VST3\Mooni Analyzer.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\Standalone\Mooni Analyzer.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Resources\Fonts\OFL-Nunito.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\Resources\Fonts\OFL-Unbounded.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\Mooni Analyzer.exe"; Components: standalone
Name: "{autoprograms}\{#AppName} — README"; Filename: "{app}\README.md"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Mooni Analyzer.vst3"
