[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$GameExe,[Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$GameExe=(Resolve-Path $GameExe).Path
$null=New-Item -ItemType Directory -Path $OutputRoot -Force
$OutputRoot=(Resolve-Path $OutputRoot).Path
$listener=[System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback,0)
$listener.Start()
$port=$listener.LocalEndpoint.Port
$listener.Stop()
$server=$client=$null
$oldPort=$env:PORT
$log=Join-Path $OutputRoot 'network.txt'
try {
    $env:PORT=[string]$port
    $server=Start-Process -FilePath (Get-Command node).Source -WorkingDirectory $repo -ArgumentList ('"'+(Join-Path $repo 'game/server.js')+'"') -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $OutputRoot 'room.txt') -RedirectStandardError (Join-Path $OutputRoot 'room-error.txt')
    $env:PORT=$oldPort
    $url="http://127.0.0.1:$port"
    $ready=$false
    for($i=0;$i -lt 30;$i++) {
        if($server.HasExited) { throw 'The test room stopped before opening.' }
        try { $ready=(Invoke-WebRequest $url -UseBasicParsing -TimeoutSec 1).StatusCode -eq 200; if($ready) { break } } catch { Start-Sleep -Milliseconds 300 }
    }
    if(-not $ready) { throw 'The test room did not open.' }
    $client=Start-Process -FilePath $GameExe -PassThru -ArgumentList ('--smoke '+$url+' "'+$log+'"')
    if(-not $client.WaitForExit(30000)) { throw 'The DX11 network test timed out.' }
    if(Test-Path $log) { Get-Content $log }
    if($client.ExitCode -ne 0 -or -not (Test-Path $log) -or -not (Select-String -Path $log -SimpleMatch 'PASS: Windows DX11 client')) { throw 'The built client did not pass join, movement, fire and reload.' }
    $client=Start-Process -FilePath $GameExe -PassThru -ArgumentList ('--render-smoke "'+$OutputRoot+'"')
    if(-not $client.WaitForExit(45000)) { throw 'The DX11 software rendering test timed out.' }
    $renderError=Join-Path $OutputRoot 'render-error.txt'
    if(Test-Path $renderError) { Get-Content $renderError }
    if($client.ExitCode -ne 0) { throw 'The software DX11 render test failed.' }
    foreach($samples in 1,2,4) {
        $image=Join-Path $OutputRoot "dx11-msaa-$samples.bmp"
        if(-not (Test-Path $image) -or (Get-Item $image).Length -lt 100000) { throw "Missing render evidence for MSAA $samples." }
        $bytes=[System.IO.File]::ReadAllBytes($image)
        $offset=[BitConverter]::ToInt32($bytes,10)
        $first=[BitConverter]::ToUInt32($bytes,$offset) -band 0x00FFFFFF
        $varied=$false
        for($at=$offset;$at -le $bytes.Length-4;$at+=400) {
            if(([BitConverter]::ToUInt32($bytes,$at) -band 0x00FFFFFF) -ne $first) { $varied=$true; break }
        }
        if(-not $varied) { throw "Flat render output for MSAA $samples." }
    }
    Write-Host 'PASS: Windows executable network smoke and DX11 software render (off, 2x, 4x). No physical GPU/exclusive test.'
} finally {
    $env:PORT=$oldPort
    foreach($process in @($client,$server)) {
        if($null -ne $process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}
