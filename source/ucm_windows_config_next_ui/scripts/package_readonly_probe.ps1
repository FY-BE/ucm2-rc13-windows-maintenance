[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$QtRoot='C:/msys64/ucrt64'
)
$ErrorActionPreference='Stop'
$source=[IO.Path]::GetFullPath((Join-Path $BuildDirectory 'UcmReadOnlyProbe.exe'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
$qtBin=Join-Path ([IO.Path]::GetFullPath($QtRoot)) 'bin'
if (!(Test-Path -LiteralPath $source) -or (Test-Path -LiteralPath $out)) {throw 'Source missing or output already exists'}
New-Item -ItemType Directory -Path $out | Out-Null
$exe=Join-Path $out 'UcmReadOnlyProbe.exe'
Copy-Item -LiteralPath $source -Destination $exe
$queue=[Collections.Generic.Queue[string]]::new()
$queue.Enqueue($exe)
$visited=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
while($queue.Count) {
    $binary=$queue.Dequeue()
    if(!$visited.Add($binary)){continue}
    $lines=& (Join-Path $qtBin 'objdump.exe') -p $binary
    if($LASTEXITCODE -ne 0){throw "Cannot inspect $binary"}
    foreach($line in ($lines | Select-String 'DLL Name:')) {
        $name=($line.Line -split 'DLL Name:',2)[1].Trim()
        $target=Join-Path $out $name
        if(Test-Path -LiteralPath $target){continue}
        $dependency=Join-Path $qtBin $name
        if(Test-Path -LiteralPath $dependency){Copy-Item -LiteralPath $dependency -Destination $target; $queue.Enqueue($target)}
        elseif($name -notmatch '^(api-ms-|ext-ms-)' -and !(Test-Path -LiteralPath (Join-Path "$env:SystemRoot/System32" $name))) {throw "Unresolved runtime dependency: $name"}
    }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../W2_READONLY_PROBE_20260909.md') -Destination (Join-Path $out 'README.md')
$sums=Get-ChildItem -LiteralPath $out -File | Sort-Object Name | ForEach-Object {"$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash) *$($_.Name)"}
[IO.File]::WriteAllLines((Join-Path $out 'SHA256SUMS.txt'),$sums,[Text.UTF8Encoding]::new($false))
Write-Output "Read-only probe closure: $($sums.Count) files, $out"
