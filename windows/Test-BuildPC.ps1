[CmdletBinding()]
param([string]$EngineRoot=$env:UE_ROOT,[switch]$PassThru)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Toolchain.ps1')
$toolchain=Get-BasicFPSToolchain
if ($env:OS -ne 'Windows_NT') { throw '이 점검은 빌드할 Windows 노트북/PC에서 실행해 주세요.' }
if (-not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { throw 'Windows x64의 64비트 PowerShell에서 실행해 주세요.' }
if (-not $EngineRoot) {
    $key='HKLM:\SOFTWARE\EpicGames\Unreal Engine\'+$toolchain.engineAssociation
    if (Test-Path $key) { $EngineRoot=(Get-ItemProperty $key).InstalledDirectory }
}
if (-not $EngineRoot) { $EngineRoot=Join-Path $env:ProgramFiles ('Epic Games\UE_'+$toolchain.engineAssociation) }
$failures=[System.Collections.Generic.List[string]]::new()
$warnings=[System.Collections.Generic.List[string]]::new()
$engineVersion=$null
$versionFile=Join-Path $EngineRoot 'Engine\Build\Build.version'
if (-not (Test-Path $versionFile)) {
    $failures.Add('언리얼 '+$toolchain.engineVersion+'을 찾지 못했어요. Epic Games Launcher에서 설치하거나 UE_ROOT에 설치 폴더를 지정해 주세요.')
} else {
    $version=Get-Content $versionFile -Raw | ConvertFrom-Json
    $engineVersion='{0}.{1}.{2}' -f $version.MajorVersion,$version.MinorVersion,$version.PatchVersion
    if (-not (Test-BasicFPSEngineVersion $version $toolchain.engineVersion)) { $failures.Add('이 프로젝트의 빌드 기준은 언리얼 '+$toolchain.engineVersion+'이에요. 현재 설치: '+$engineVersion) }
}
foreach ($tool in @('Engine\Build\BatchFiles\Build.bat','Engine\Build\BatchFiles\RunUAT.bat','Engine\Binaries\Win64\UnrealEditor-Cmd.exe')) {
    if (-not (Test-Path (Join-Path $EngineRoot $tool))) { $failures.Add('언리얼 설치에 빌드 도구가 빠져 있어요: '+$tool) }
}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=$null
$vsVersion=$null
$msvcVersion=$null
$installations=@()
if (Test-Path $vswhere) {
    $json=& $vswhere -all -products '*' -version $toolchain.visualStudioRange -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio 설치 정보를 읽지 못했어요.' }
    if ($json) { $installations=@(($json -join "`n") | ConvertFrom-Json) }
}
foreach ($installation in ($installations | Sort-Object { [version]$_.installationVersion } -Descending)) {
    $compilers=@(Get-ChildItem (Join-Path $installation.installationPath 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue | Where-Object {
        (Test-Path (Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe')) -and
        (Test-Path (Join-Path $_.FullName 'include\vcruntime.h')) -and
        (Test-Path (Join-Path $_.FullName 'lib\x64\libcmt.lib'))
    })
    $selected=Select-BasicFPSVersion -Versions @($compilers | ForEach-Object { $_.Name }) -Family $toolchain.msvcFamily
    if ($selected) {
        $vs=$installation.installationPath
        $vsVersion=$installation.installationVersion
        $msvcVersion=$selected
        break
    }
}
if (-not $installations.Count) { $failures.Add('Visual Studio 2026 정식판과 C++ 데스크톱/게임 개발 도구를 설치해 주세요. Preview는 선택하지 않아요.') }
elseif (-not $msvcVersion) { $failures.Add('Visual Studio Installer의 개별 구성 요소에서 MSVC '+$toolchain.msvcFamily+' x64/x86 도구를 추가해 주세요. 더 최신인 컴파일러만 있으면 UE 5.8 호환 문제를 피할 수 없어요.') }
$sdkKey='HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots'
$sdk=if (Test-Path $sdkKey) {(Get-ItemProperty $sdkKey).KitsRoot10} else {$null}
$sdkVersion=$null
if ($sdk) {
    $sdks=@(Get-ChildItem (Join-Path $sdk 'Include') -Directory -ErrorAction SilentlyContinue | Where-Object {
        (Test-Path (Join-Path $_.FullName 'um\Windows.h')) -and
        (Test-Path (Join-Path $_.FullName 'ucrt\stdio.h')) -and
        (Test-Path (Join-Path $sdk ('Lib\'+$_.Name+'\um\x64\kernel32.lib'))) -and
        (Test-Path (Join-Path $sdk ('Lib\'+$_.Name+'\ucrt\x64\ucrt.lib'))) -and
        (Test-Path (Join-Path $sdk ('bin\'+$_.Name+'\x64\rc.exe')))
    })
    $sdkVersion=Select-BasicFPSVersion -Versions @($sdks | ForEach-Object { $_.Name }) -Minimum $toolchain.minimumWindowsSdk
}
if (-not $sdkVersion) { $failures.Add('Windows SDK '+$toolchain.minimumWindowsSdk+' 이상의 x64 헤더·라이브러리·빌드 도구를 설치해 주세요.') }
$nodeVersion=$null
if (Get-Command node -ErrorAction SilentlyContinue) { $nodeVersion=(& node -p 'process.versions.node' | Select-Object -Last 1) }
if (-not (Test-BasicFPSNodeVersion $nodeVersion $toolchain.NodeVersion)) { $failures.Add('Node.js '+$toolchain.NodeVersion+' 이상인 26 LTS 정식판을 설치해 주세요. Alpha/다음 메이저 버전은 사용하지 않아요.') }
$memory=[math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB,1)
if ($memory -lt 16) { $failures.Add('빌드 PC의 메모리가 16GB 미만이에요. 이 구성으로 자동 빌드를 진행하지 않아요.') }
elseif ($memory -lt 32) { $warnings.Add('메모리 32GB 이상을 권장해요. 빌드 중 다른 작업을 줄여 주세요.') }
$drive=Get-PSDrive -Name ([System.IO.Path]::GetPathRoot($PSScriptRoot).Substring(0,1))
if ($drive.Free -lt 40GB) { $failures.Add('작업 드라이브에 여유 공간 40GB 이상을 확보해 주세요. 이는 이 빌드 절차의 안전 기준이에요.') }
$result=[pscustomobject]@{EngineRoot=$EngineRoot;EngineVersion=$engineVersion;VisualStudio=$vs;VisualStudioVersion=$vsVersion;MSVCVersion=$msvcVersion;WindowsSDK=$sdk;WindowsSDKVersion=$sdkVersion;NodeVersion=$nodeVersion;MemoryGB=$memory;FreeGB=[math]::Round($drive.Free/1GB,1);Failures=$failures.ToArray();Warnings=$warnings.ToArray();CheckedAt=(Get-Date).ToUniversalTime().ToString('o')}
$reportDir=Join-Path $env:LOCALAPPDATA 'BasicFPSBuild'
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
$result | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $reportDir 'preflight.json') -Encoding UTF8
foreach ($warning in $warnings) { Write-Warning $warning }
foreach ($failure in $failures) { Write-Host ('미준비: '+$failure) -ForegroundColor Yellow }
if ($failures.Count) { throw '빌드 PC 준비가 끝나지 않았어요. 위 항목을 먼저 해결해 주세요. 점검 결과는 LocalAppData\BasicFPSBuild에 저장했어요.' }
Write-Host '빌드 PC 사전 점검을 통과했어요. 실제 컴파일과 화면 동작이 확인됐다는 뜻은 아니에요.' -ForegroundColor Green
Write-Host ('선택한 도구: UE '+$engineVersion+' / VS '+$vsVersion+' / MSVC '+$msvcVersion+' / SDK '+$sdkVersion+' / Node '+$nodeVersion)
if ($PassThru) { return $result }
