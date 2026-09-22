[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,

    [string]$Python = 'python'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$buildDirectoryPath = [IO.Path]::GetFullPath($BuildDirectory)
$sourceRoot = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\reference_force_camera'))
$entryPoint = Join-Path $sourceRoot 'reference_force_bridge.py'
$vendorRoot = Join-Path $sourceRoot 'vendor'
$outputDirectory = Join-Path $buildDirectoryPath 'reference_force_camera'
$pyinstallerRoot = Join-Path $buildDirectoryPath 'pyinstaller-reference-force'

foreach ($requiredPath in @($entryPoint, $vendorRoot)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required reference-force input is missing: $requiredPath"
    }
}

& $Python -m PyInstaller --version
if ($LASTEXITCODE -ne 0) {
    throw 'PyInstaller is not installed for the selected Python interpreter.'
}

New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $pyinstallerRoot -Force | Out-Null

& $Python -m PyInstaller `
    --noconfirm `
    --clean `
    --onefile `
    --console `
    --noupx `
    --name UcmReferenceForceBridge `
    --distpath $outputDirectory `
    --workpath (Join-Path $pyinstallerRoot 'work') `
    --specpath (Join-Path $pyinstallerRoot 'spec') `
    --paths $vendorRoot `
    --add-data "$vendorRoot;vendor" `
    --hidden-import windows.force_input_camera.force_input_mapper `
    --hidden-import windows.force_input_camera.hikrobot_camera `
    --hidden-import windows.force_input_camera.recognition_quality `
    --hidden-import windows.force_input_camera.seven_segment_recognize `
    --hidden-import calibration_candidate `
    $entryPoint
if ($LASTEXITCODE -ne 0) {
    throw "Reference-force bridge build failed with exit code $LASTEXITCODE"
}

$executable = Join-Path $outputDirectory 'UcmReferenceForceBridge.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Reference-force bridge output is missing: $executable"
}

& $executable --verify-vendor
if ($LASTEXITCODE -ne 0) {
    throw "Frozen reference-force bridge self-check failed with exit code $LASTEXITCODE"
}

Get-FileHash -LiteralPath $executable -Algorithm SHA256 |
    Select-Object Path, Hash
