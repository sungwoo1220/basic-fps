function Get-BasicFPSToolchain {
    $config=Get-Content (Join-Path $PSScriptRoot 'toolchain.json') -Raw | ConvertFrom-Json
    $nodeVersion=(Get-Content (Join-Path (Split-Path $PSScriptRoot -Parent) '.node-version') -Raw).Trim()
    $config | Add-Member -NotePropertyName NodeVersion -NotePropertyValue $nodeVersion
    return $config
}

function Select-BasicFPSVersion {
    param([string[]]$Versions,[string]$Minimum,[string]$Family)
    $versions=@($Versions | Where-Object {
        $_ -match '^\d+\.\d+\.\d+(\.\d+)?$' -and
        (-not $Family -or $_.StartsWith($Family+'.')) -and
        (-not $Minimum -or [version]$_ -ge [version]$Minimum)
    } | Sort-Object { [version]$_ } -Descending)
    if ($versions.Count) { return $versions[0] }
}

function Test-BasicFPSNodeVersion {
    param([string]$Version,[string]$Minimum)
    return ($Version -match '^\d+\.\d+\.\d+$' -and
        ([version]$Version).Major -eq ([version]$Minimum).Major -and
        [version]$Version -ge [version]$Minimum)
}

function Test-BasicFPSEngineVersion {
    param($Version,[string]$Expected)
    return (('{0}.{1}.{2}' -f $Version.MajorVersion,$Version.MinorVersion,$Version.PatchVersion) -eq $Expected)
}
