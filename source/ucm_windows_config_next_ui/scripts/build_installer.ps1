[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PortableDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true)]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [string]$OutputBaseName,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
    [string]$NumericVersion,

    [string]$CertificateThumbprint,

    [ValidatePattern('^https?://')]
    [string]$TimestampUrl,

    [string]$InnoCompiler =
        "$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$portablePath = [IO.Path]::GetFullPath($PortableDirectory)
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
$compilerPath = [IO.Path]::GetFullPath($InnoCompiler)
$definition = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\packaging\installer.iss'))
$brandIcon = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\assets\qizhen-ucm.ico'))
$expectedInstaller = Join-Path $outputPath "$OutputBaseName.exe"

foreach ($requiredPath in @(
    $portablePath,
    (Join-Path $portablePath 'UcmConfigStudioNext.exe'),
    (Join-Path $portablePath 'qt.conf'),
    (Join-Path $portablePath 'README_先读我.txt'),
    (Join-Path $portablePath 'SHA256SUMS.txt'),
    (Join-Path $portablePath `
        'reference_force_camera\UcmReferenceForceBridge.exe'),
    $compilerPath,
    $definition,
    $brandIcon
)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required installer input is missing: $requiredPath"
    }
}
if (Test-Path -LiteralPath (Join-Path $portablePath 'PACKAGE_INCOMPLETE')) {
    throw 'Portable input is marked PACKAGE_INCOMPLETE.'
}
if (([string]::IsNullOrWhiteSpace($Version)) -or
    ([string]::IsNullOrWhiteSpace($OutputBaseName)) -or
    ([string]::IsNullOrWhiteSpace($NumericVersion))) {
    throw 'Version, NumericVersion and OutputBaseName must not be empty.'
}
if ([string]::IsNullOrWhiteSpace($CertificateThumbprint) -ne
    [string]::IsNullOrWhiteSpace($TimestampUrl)) {
    throw 'CertificateThumbprint and TimestampUrl must be supplied together.'
}
if (Test-Path -LiteralPath $expectedInstaller) {
    throw "Installer output already exists: $expectedInstaller"
}
if (-not (Test-Path -LiteralPath $outputPath)) {
    New-Item -ItemType Directory -Path $outputPath | Out-Null
}

$compilerArguments = @(
    "/DPortableSource=$portablePath",
    "/DInstallerOutput=$outputPath",
    "/DAppVersion=$Version",
    "/DNumericVersion=$NumericVersion",
    "/DOutputBaseName=$OutputBaseName",
    "/DBrandIcon=$brandIcon"
)
if (-not [string]::IsNullOrWhiteSpace($CertificateThumbprint)) {
    $signTool = Get-ChildItem -LiteralPath `
        'C:\Program Files (x86)\Windows Kits\10\bin' `
        -Filter signtool.exe -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.DirectoryName -match '\\x64$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($null -eq $signTool) {
        throw 'Windows SDK x64 signtool.exe was not found.'
    }
    $thumbprint = $CertificateThumbprint.Replace(' ', '').ToUpperInvariant()
    $certificate = Get-ChildItem Cert:\CurrentUser\My,Cert:\LocalMachine\My |
        Where-Object { $_.Thumbprint -eq $thumbprint } |
        Select-Object -First 1
    if ($null -eq $certificate -or -not $certificate.HasPrivateKey) {
        throw "Signing certificate/private key not available: $thumbprint"
    }
    $codeSigningOid = '1.3.6.1.5.5.7.3.3'
    if (-not ($certificate.EnhancedKeyUsageList.ObjectId.Value -contains
        $codeSigningOid)) {
        throw 'The selected certificate does not have the Code Signing EKU.'
    }
    $storeArgument = if ($certificate.PSPath -match 'LocalMachine') {
        ' /sm'
    } else {
        ''
    }
    $payloadSignature = Get-AuthenticodeSignature -LiteralPath
        (Join-Path $portablePath 'UcmConfigStudioNext.exe')
    if ($payloadSignature.Status -ne 'Valid' -or
        $payloadSignature.SignerCertificate.Thumbprint -ne $thumbprint) {
        throw 'Portable UcmConfigStudioNext.exe must be signed with the selected certificate before installer compilation.'
    }
    $signCommand = '"{0}" sign /sha1 {1}{2} /fd SHA256 /tr "{3}" ' +
        '/td SHA256 /d "启真传感 UCM" $f'
    $signCommand = $signCommand -f $signTool.FullName, $thumbprint,
        $storeArgument, $TimestampUrl
    $compilerArguments += "/Srelease=$signCommand"
    $compilerArguments += '/DReleaseSignTool=release'
}
$compilerArguments += $definition

& $compilerPath @compilerArguments
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup compiler failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $expectedInstaller)) {
    throw "Expected installer was not generated: $expectedInstaller"
}

$installer = Get-Item -LiteralPath $expectedInstaller
$sha256 = (Get-FileHash -LiteralPath $expectedInstaller -Algorithm SHA256).Hash
$signature = Get-AuthenticodeSignature -LiteralPath $expectedInstaller
if (-not [string]::IsNullOrWhiteSpace($CertificateThumbprint) -and
    $signature.Status -ne 'Valid') {
    throw "Installer signature verification failed: $($signature.Status)"
}
Write-Output ("Installer: {0}" -f $installer.FullName)
Write-Output ("Bytes: {0}" -f $installer.Length)
Write-Output ("SHA256: {0}" -f $sha256)
Write-Output ("Signature: {0}" -f $signature.Status)
