[CmdletBinding()]
param([string]$EngineRoot=$env:UE_ROOT,[string]$OutputRoot,[switch]$SkipPackage)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$check=& (Join-Path $PSScriptRoot 'Test-BuildPC.ps1') -EngineRoot $EngineRoot -PassThru
$EngineRoot=$check.EngineRoot
if (-not $OutputRoot) { $OutputRoot=Join-Path $env:LOCALAPPDATA ('BasicFPSBuild\runs\'+(Get-Date -Format 'yyyyMMdd-HHmmss')) }
if (Test-Path $OutputRoot) { throw '출력 폴더가 이미 있어요. 이전 결과를 덮어쓰지 않도록 새 폴더를 지정해 주세요.' }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$OutputRoot=(Resolve-Path $OutputRoot).Path
$check | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputRoot 'preflight.json') -Encoding UTF8
$project=Join-Path $repo 'unreal\BasicFPS.uproject'
$build=Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$editor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$uat=Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$compilerArgument='-CompilerVersion='+$check.MSVCVersion
$sdkArgument='-WindowsSdkVersion='+$check.WindowsSDKVersion
$log=Join-Path $OutputRoot 'build-transcript.txt'
Start-Transcript -Path $log | Out-Null
try {
    Push-Location $repo
    try {
        & node scripts/export-unreal-rules.cjs --check
        if ($LASTEXITCODE -ne 0) { throw '공유 판정 규칙이 최신 상태가 아니에요.' }
        & node scripts/copy-three.cjs
        if ($LASTEXITCODE -ne 0) { throw '기존 게임 시험에 필요한 Three.js를 준비하지 못했어요.' }
        & npm.cmd test
        if ($LASTEXITCODE -ne 0) { throw '기존 게임/업데이트 시험에 실패했어요.' }
        & $build BasicFPSEditor Win64 Development "-Project=$project" $compilerArgument $sdkArgument -WaitMutex -NoHotReloadFromIDE
        if ($LASTEXITCODE -ne 0) { throw '언리얼 에디터용 코드 컴파일에 실패했어요.' }
        & $editor $project -run=BasicFPSContent -unattended -nop4 -nullrhi -nosound -stdout -FullStdOutLogOutput "-abslog=$(Join-Path $OutputRoot 'content.log')"
        if ($LASTEXITCODE -ne 0) { throw '맵·머티리얼 생성에 실패했어요.' }
        $report=Join-Path $OutputRoot 'Automation'
        $arguments='"'+$project+'" -unattended -nop4 -nullrhi -nosound -stdout -FullStdOutLogOutput -ExecCmds="Automation RunTests BasicFPS" -TestExit="Automation Test Queue Empty" -ReportExportPath="'+$report+'" -abslog="'+(Join-Path $OutputRoot 'automation.log')+'"'
        $process=Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -NoNewWindow
        if (-not $process.WaitForExit(20*60*1000)) { & taskkill.exe /PID $process.Id /T /F | Out-Null;throw '언리얼 자동 시험 시간이 초과됐어요.' }
        if ($process.ExitCode -ne 0) { throw '언리얼 자동 시험 실행이 실패했어요.' }
        $index=Join-Path $report 'index.json'
        if (-not (Test-Path $index)) { throw '자동 시험 결과가 없어요. 시험을 통과했다고 처리하지 않아요.' }
        $tests=Get-Content $index -Raw | ConvertFrom-Json
        if ($tests.failed -gt 0 -or ([int]$tests.succeeded+[int]$tests.succeededWithWarnings) -lt 2) { throw '필수 언리얼 시험이 모두 통과하지 않았어요.' }
        if (-not $SkipPackage) {
            $archive=Join-Path $OutputRoot 'Package'
            & $uat BuildCookRun "-project=$project" -target=BasicFPS -platform=Win64 -clientconfig=Development "-ubtargs=$compilerArgument $sdkArgument" -noP4 -build -cook -stage -pak -iostore -prereqs -archive "-archivedirectory=$archive" -map=/Game/Maps/Arena -unattended -utf8output
            if ($LASTEXITCODE -ne 0) { throw 'Windows 게임 패키지 제작에 실패했어요.' }
            $binary=Get-ChildItem $archive -Recurse -Filter 'BasicFPS*.exe' | Where-Object { $_.FullName -match 'BasicFPS\\Binaries\\Win64\\BasicFPS(-Win64-(Development|Shipping))?\.exe$' } | Select-Object -First 1
            if (-not $binary) { throw '제작된 게임 실행 파일이 없어요.' }
            & (Join-Path $PSScriptRoot 'Test-NativeClient.ps1') -GameExe $binary.FullName -OutputRoot $OutputRoot
            [pscustomobject]@{Engine=$check.EngineVersion;MSVC=$check.MSVCVersion;WindowsSDK=$check.WindowsSDKVersion;Node=$check.NodeVersion;RHI='DX11';Configuration='Development';Executable=$binary.FullName;BuiltAt=(Get-Date).ToUniversalTime().ToString('o');AutomatedTestsPassed=$true;HeadlessNetworkingVerified=$true;InteractiveGraphicsVerified=$false} | ConvertTo-Json | Set-Content (Join-Path $OutputRoot 'build-result.json') -Encoding UTF8
            Write-Host ('빌드 완료: '+$binary.FullName)
            Write-Host '실제 화면 모드·MSAA 성능·MUX는 아직 확인하지 않았어요. Test-Graphics.ps1로 시험해 주세요.'
        }
    } finally { Pop-Location }
} finally { Stop-Transcript | Out-Null }
