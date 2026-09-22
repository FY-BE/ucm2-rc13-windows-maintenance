[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [switch]$Engineer
)
$ErrorActionPreference='Stop'
$executablePath=[IO.Path]::GetFullPath($Executable)
$outputPath=[IO.Path]::GetFullPath($OutputDirectory)
if (!(Test-Path -LiteralPath $executablePath)) { throw 'Executable missing' }
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$records=@()
$pages=if($Engineer){@(2,3,4)}else{@(0,1,5)}
foreach ($size in @('1280x800','1366x768','1440x810','1920x1080')) {
    foreach ($page in $pages) {
        $target=Join-Path $outputPath "page-$page-$size.png"
        $start=[Diagnostics.ProcessStartInfo]::new($executablePath)
        $start.UseShellExecute=$false
        $start.CreateNoWindow=$true
        $start.Environment['QT_QPA_PLATFORM']='offscreen'
        $start.Environment['QT_SCALE_FACTOR']='1'
        $start.Environment['QT_SCREEN_SCALE_FACTORS']='1'
        $start.Environment['QT_FONT_DPI']='96'
        foreach ($argument in @('--offline','--no-system-backdrop','--window-size',$size,'--page',"$page",'--screenshot',$target)) {
            $start.ArgumentList.Add($argument)
        }
        if ($Engineer) { $start.ArgumentList.Add('--engineer-preview') }
        $process=[Diagnostics.Process]::Start($start)
        if (!$process.WaitForExit(20000)) { $process.Kill(); throw "Screenshot timed out: $target" }
        if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $target)) { throw "Screenshot failed: $target" }
        $png=[IO.File]::ReadAllBytes($target)
        $width=[Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($png,16))
        $height=[Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($png,20))
        if ("${width}x${height}" -ne $size) { throw "Unexpected PNG dimensions ${width}x${height}: $target" }
        $records += [pscustomobject]@{file=[IO.Path]::GetFileName($target);width=$width;height=$height;offline=$true;sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash}
        $process.Dispose()
    }
}
$records | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputPath 'screenshots.json') -Encoding utf8
Write-Output "Verified $($records.Count) offline screenshots at exact requested dimensions."
