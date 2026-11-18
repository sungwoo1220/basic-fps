[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Toolchain.ps1')
$config=Get-BasicFPSToolchain
$repo=Split-Path $PSScriptRoot -Parent
$checks=0
function Assert-Toolchain {
    param([bool]$Condition,[string]$Message)
    if (-not $Condition) { throw ('도구 버전 회귀 검사 실패: '+$Message) }
    $script:checks++
}

foreach ($script in (Get-ChildItem $PSScriptRoot -Filter '*.ps1')) {
    $bytes=[System.IO.File]::ReadAllBytes($script.FullName)
    Assert-Toolchain ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) ($script.Name+'에 Windows PowerShell 5.1용 UTF-8 BOM이 없어요.')
    $tokens=$null
    $parseErrors=$null
    $null=[System.Management.Automation.Language.Parser]::ParseFile($script.FullName,[ref]$tokens,[ref]$parseErrors)
    Assert-Toolchain ($parseErrors.Count -eq 0) ($script.Name+': '+($parseErrors.Message -join '; '))
}
$project=Get-Content (Join-Path $repo 'unreal/BasicFPS.uproject') -Raw | ConvertFrom-Json
Assert-Toolchain ($project.EngineAssociation -eq $config.engineAssociation) '엔진 연결값이 빌드 기준과 달라요.'
$engineParts=$config.engineVersion.Split('.')
$engine=[pscustomobject]@{MajorVersion=[int]$engineParts[0];MinorVersion=[int]$engineParts[1];PatchVersion=[int]$engineParts[2]}
Assert-Toolchain (Test-BasicFPSEngineVersion $engine $config.engineVersion) '정확한 엔진 패치를 거부했어요.'
$engine.PatchVersion--
Assert-Toolchain (-not (Test-BasicFPSEngineVersion $engine $config.engineVersion)) '이전 엔진 패치를 허용했어요.'
$engine.PatchVersion++
$engine.MinorVersion--
Assert-Toolchain (-not (Test-BasicFPSEngineVersion $engine $config.engineVersion)) '이전 엔진 버전을 허용했어요.'
foreach ($target in @('BasicFPS.Target.cs','BasicFPSEditor.Target.cs')) {
    $source=Get-Content (Join-Path $repo ('unreal/Source/'+$target)) -Raw
    Assert-Toolchain ($source.Contains('EngineIncludeOrderVersion.Unreal'+$config.engineAssociation.Replace('.','_'))) ($target+'의 엔진 헤더 순서가 달라요.')
}
Assert-Toolchain (Test-BasicFPSNodeVersion $config.NodeVersion $config.NodeVersion) '지정한 Node LTS를 거부했어요.'
Assert-Toolchain (Test-BasicFPSNodeVersion '26.99.0' $config.NodeVersion) '같은 LTS의 새 패치를 거부했어요.'
foreach ($version in @('22.0.0','24.0.0','26.0.0','27.0.0','26.13.1-rc.1','27.0.0-alpha.0.1.1','not-a-version','')) {
    Assert-Toolchain (-not (Test-BasicFPSNodeVersion $version $config.NodeVersion)) ('지원하지 않는 Node 버전을 허용했어요: '+$version)
}
$compilers=@('14.50.35717','14.51.36248','14.50.35720','14.44.35207','14.50.99999-preview')
$selected=Select-BasicFPSVersion -Versions $compilers -Family $config.msvcFamily
Assert-Toolchain ($selected -eq '14.50.35720') '새 비호환 도구 대신 최신 14.50을 선택해야 해요.'
Assert-Toolchain (-not (Select-BasicFPSVersion -Versions @('14.51.36248','14.44.35207') -Family $config.msvcFamily)) '호환 도구가 없을 때 최신 도구로 조용히 바꿨어요.'
Assert-Toolchain (-not (Select-BasicFPSVersion -Versions @() -Family $config.msvcFamily)) '도구가 없는 상태를 허용했어요.'
Assert-Toolchain ((Select-BasicFPSVersion -Versions @('10.0.22621.0','10.0.26100.0') -Minimum $config.minimumWindowsSdk) -eq '10.0.26100.0') 'SDK 최소 버전 선택이 잘못됐어요.'
Assert-Toolchain (-not (Select-BasicFPSVersion -Versions @('10.0.19041.0','10.0.22621.0') -Minimum $config.minimumWindowsSdk)) '이전 SDK를 허용했어요.'
Assert-Toolchain ((Select-BasicFPSVersion -Versions @('14.50.9','14.50.10') -Family $config.msvcFamily) -eq '14.50.10') '버전을 문자열 순서로 정렬했어요.'
foreach ($workflow in @('build.yml','test.yml','unreal.yml')) {
    $source=Get-Content (Join-Path $repo ('.github/workflows/'+$workflow)) -Raw
    Assert-Toolchain ($source.Contains("node-version-file: '.node-version'")) ($workflow+'가 공통 Node 버전을 쓰지 않아요.')
    Assert-Toolchain (-not ($source -match 'node-version:')) ($workflow+'에 별도 Node 버전이 남았어요.')
}
$build=Get-Content (Join-Path $PSScriptRoot 'Build-Unreal.ps1') -Raw
Assert-Toolchain ($build.Contains("'-CompilerVersion='+`$check.MSVCVersion")) '실제 빌드에 검사한 컴파일러를 전달하지 않아요.'
Assert-Toolchain ($build.Contains("'-WindowsSdkVersion='+`$check.WindowsSDKVersion")) '실제 빌드에 검사한 SDK를 전달하지 않아요.'
Assert-Toolchain ($build.Contains('"-ubtargs=$compilerArgument $sdkArgument"')) '패키징에 검사한 도구를 전달하지 않아요.'
Write-Host ('통과: 도구 버전 선택·스크립트 문법·설정 일치 '+$checks+'개 검사. 실제 Windows/UE 빌드 시험은 아니에요.')
