[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$GameExe,[Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$listener=[System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback,0)
$listener.Start()
$port=$listener.LocalEndpoint.Port
$listener.Stop()
$server=$null
$client=$null
$oldPort=$env:PORT
$log=Join-Path $OutputRoot 'native-smoke.log'
try {
    $env:PORT=[string]$port
    $server=Start-Process -FilePath (Get-Command node).Source -WorkingDirectory $repo -ArgumentList ('"'+(Join-Path $repo 'game\server.js')+'"') -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $OutputRoot 'smoke-server.log') -RedirectStandardError (Join-Path $OutputRoot 'smoke-server-error.log')
    $env:PORT=$oldPort
    $ready=$false
    for ($attempt=0;$attempt -lt 30;$attempt++) {
        if ($server.HasExited) { throw '통신 시험용 방을 열지 못했어요.' }
        try { $response=Invoke-WebRequest "http://127.0.0.1:$port/" -UseBasicParsing -TimeoutSec 1; if ($response.StatusCode -eq 200) { $ready=$true;break } } catch { Start-Sleep -Milliseconds 300 }
    }
    if (-not $ready) { throw '통신 시험용 방의 준비 시간이 초과됐어요.' }
    $arguments='-nullrhi -nosound -unattended -stdout -FullStdOutLogOutput -BasicFPSSmokeTest -ServerUrl=http://127.0.0.1:'+$port+' -UserDir="'+(Join-Path $OutputRoot 'SmokeUser')+'" -abslog="'+$log+'"'
    $client=Start-Process -FilePath $GameExe -WorkingDirectory (Split-Path $GameExe) -ArgumentList $arguments -PassThru -NoNewWindow
    if (-not $client.WaitForExit(60000)) { throw '제작된 게임의 통신 시험 시간이 초과됐어요.' }
    if ($client.ExitCode -ne 0 -or -not (Test-Path $log) -or -not (Select-String -Path $log -SimpleMatch 'BasicFPSSmokeTest PASS:')) { throw '제작된 게임의 접속·이동·사격·재장전 시험이 통과되지 않았어요. native-smoke.log를 확인해 주세요.' }
    Write-Host '제작된 게임의 화면 없는 통신 시험을 통과했어요. GPU·MSAA·전체화면은 시험하지 않았어요.'
} finally {
    $env:PORT=$oldPort
    foreach ($process in @($client,$server)) { if ($null -ne $process -and -not $process.HasExited) { & taskkill.exe /PID $process.Id /T /F | Out-Null } }
}
