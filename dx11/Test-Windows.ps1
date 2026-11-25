[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$GameExe,[Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
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
    $room=Join-Path (Split-Path $GameExe) 'room'
    $server=Start-Process -FilePath (Join-Path $room 'node.exe') -WorkingDirectory $room -ArgumentList ('"'+(Join-Path $room 'server.js')+'"') -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $OutputRoot 'room.txt') -RedirectStandardError (Join-Path $OutputRoot 'room-error.txt')
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
    Stop-Process -Id $server.Id -Force
    $server=$null
    # The app must open its own room from the bundled files: no Node on PATH or NODE_PATH helps it.
    $hostLog=Join-Path $OutputRoot 'host.txt'
    $oldPath=$env:PATH; $oldNodePath=$env:NODE_PATH
    $env:PATH="$env:SystemRoot\System32;$env:SystemRoot"; $env:NODE_PATH=$null
    try { $client=Start-Process -FilePath $GameExe -PassThru -ArgumentList ('--host-smoke "'+$hostLog+'"') }
    finally { $env:PATH=$oldPath; $env:NODE_PATH=$oldNodePath }
    if(-not $client.WaitForExit(45000)) { throw 'The self-hosted room test timed out.' }
    if(Test-Path $hostLog) { Get-Content $hostLog }
    if($client.ExitCode -ne 0 -or -not (Select-String -Path $hostLog -SimpleMatch 'PASS: Windows DX11 client')) { throw 'The app could not open its own room and play in it.' }
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
    $off=Join-Path $OutputRoot 'dx11-af-1.bmp'; $sixteen=Join-Path $OutputRoot 'dx11-af-16.bmp'
    if(-not (Test-Path $off) -or -not (Test-Path $sixteen)) { throw 'Missing anisotropic filtering render evidence.' }
    if((Get-FileHash $off).Hash -eq (Get-FileHash $sixteen).Hash) { throw 'Anisotropic filtering 16x did not change the textured floor.' }
    Write-Host 'PASS: Windows executable joined another room, opened and played in its own bundled room, and DX11 software render (MSAA off, 2x, 4x; anisotropic off vs 16x). No physical GPU/exclusive test.'
} finally {
    $env:PORT=$oldPort
    foreach($process in @($client,$server)) {
        if($null -ne $process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}
