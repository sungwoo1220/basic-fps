[CmdletBinding()]
param([string]$EngineRoot=$env:UE_ROOT,[switch]$PassThru)
$ErrorActionPreference='Stop'
if ($env:OS -ne 'Windows_NT') { throw '이 점검은 Windows 빌드 PC에서 실행해 주세요.' }
if (-not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { throw 'Windows x64의 64비트 PowerShell에서 실행해 주세요.' }
if (-not $EngineRoot) {
    $key='HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.6'
    if (Test-Path $key) { $EngineRoot=(Get-ItemProperty $key).InstalledDirectory }
}
if (-not $EngineRoot) { $EngineRoot=Join-Path $env:ProgramFiles 'Epic Games\UE_5.6' }
$failures=[System.Collections.Generic.List[string]]::new()
$warnings=[System.Collections.Generic.List[string]]::new()
$versionFile=Join-Path $EngineRoot 'Engine\Build\Build.version'
if (-not (Test-Path $versionFile)) {
    $failures.Add('언리얼 5.6을 찾지 못했어요. Epic Games Launcher에서 설치하거나 UE_ROOT에 설치 폴더를 지정해 주세요.')
} else {
    $version=Get-Content $versionFile -Raw | ConvertFrom-Json
    if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 6) { $failures.Add('이 프로젝트는 재현 가능한 빌드를 위해 언리얼 5.6으로 고정돼 있어요.') }
}
foreach ($tool in @('Engine\Build\BatchFiles\Build.bat','Engine\Build\BatchFiles\RunUAT.bat','Engine\Binaries\Win64\UnrealEditor-Cmd.exe')) {
    if (-not (Test-Path (Join-Path $EngineRoot $tool))) { $failures.Add('언리얼 설치에 빌드 도구가 빠져 있어요: '+$tool) }
}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=$null
if (Test-Path $vswhere) { $vs=& $vswhere -latest -products '*' -version '[17.8,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath }
if (-not $vs) { $failures.Add('Visual Studio 2022 17.8 이상의 C++ 개발 도구가 필요해요. 17.14를 권장해요.') }
elseif (-not (Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' -and [version]$_.Name -ge [version]'14.38.33130' })) { $failures.Add('MSVC 14.38.33130 이상의 x64/x86 C++ 도구를 설치해 주세요.') }
$sdkKey='HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots'
$sdk=if (Test-Path $sdkKey) {(Get-ItemProperty $sdkKey).KitsRoot10} else {$null}
if (-not $sdk -or -not (Get-ChildItem (Join-Path $sdk 'Include') -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' -and [version]$_.Name -ge [version]'10.0.19041.0' -and (Test-Path (Join-Path $_.FullName 'um\Windows.h')) })) { $failures.Add('Windows SDK 10.0.19041.0 이상을 설치해 주세요. 10.0.22621.0 이상을 권장해요.') }
if (-not (Get-Command node -ErrorAction SilentlyContinue)) { $failures.Add('Node.js 22 이상이 필요해요. 자동 빌드에서는 준비 단계가 설치해 줘요.') }
elseif ([int]((& node -p "process.versions.node.split('.')[0]") | Select-Object -Last 1) -lt 22) { $failures.Add('Node.js 22 이상으로 업데이트해 주세요.') }
$memory=[math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB,1)
if ($memory -lt 16) { $failures.Add('빌드 PC의 메모리가 16GB 미만이에요. 이 구성으로 자동 빌드를 진행하지 않아요.') }
elseif ($memory -lt 32) { $warnings.Add('메모리 32GB 이상을 권장해요. 빌드 중 다른 작업을 줄여 주세요.') }
$drive=Get-PSDrive -Name ([System.IO.Path]::GetPathRoot($PSScriptRoot).Substring(0,1))
if ($drive.Free -lt 40GB) { $failures.Add('작업 드라이브에 여유 공간 40GB 이상을 확보해 주세요. 이는 이 빌드 절차의 안전 기준이에요.') }
$result=[pscustomobject]@{EngineRoot=$EngineRoot;VisualStudio=$vs;WindowsSDK=$sdk;MemoryGB=$memory;FreeGB=[math]::Round($drive.Free/1GB,1);Failures=$failures.ToArray();Warnings=$warnings.ToArray();CheckedAt=(Get-Date).ToUniversalTime().ToString('o')}
$reportDir=Join-Path $env:LOCALAPPDATA 'BasicFPSBuild'
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
$result | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $reportDir 'preflight.json') -Encoding UTF8
foreach ($warning in $warnings) { Write-Warning $warning }
foreach ($failure in $failures) { Write-Host ('미준비: '+$failure) -ForegroundColor Yellow }
if ($failures.Count) { throw '빌드 PC 준비가 끝나지 않았어요. 위 항목을 먼저 해결해 주세요. 점검 결과는 LocalAppData\BasicFPSBuild에 저장했어요.' }
Write-Host '빌드 PC 사전 점검을 통과했어요. 실제 컴파일과 화면 동작이 확인됐다는 뜻은 아니에요.' -ForegroundColor Green
if ($PassThru) { return $result }
