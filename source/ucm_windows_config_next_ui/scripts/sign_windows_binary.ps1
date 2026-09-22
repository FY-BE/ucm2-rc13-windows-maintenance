[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string[]]$File,

    [Parameter(Mandatory = $true)]
    [string]$CertificateThumbprint,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^https?://')]
    [string]$TimestampUrl
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

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
if ($certificate.NotAfter -le (Get-Date)) {
    throw "The selected signing certificate expired at $($certificate.NotAfter)."
}

$storeArguments = @()
if ($certificate.PSPath -match 'LocalMachine') {
    $storeArguments += '/sm'
}

foreach ($inputFile in $File) {
    $path = [IO.Path]::GetFullPath($inputFile)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Signing input is missing: $path"
    }
    $arguments = @(
        'sign',
        '/sha1', $thumbprint
    ) + $storeArguments + @(
        '/fd', 'SHA256',
        '/tr', $TimestampUrl,
        '/td', 'SHA256',
        '/d', '启真传感 UCM',
        $path
    )
    & $signTool.FullName @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "SignTool failed with exit code $LASTEXITCODE for $path"
    }
    & $signTool.FullName verify /pa /v $path
    if ($LASTEXITCODE -ne 0) {
        throw "SignTool verification failed for $path"
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $path
    if ($signature.Status -ne 'Valid' -or
        $signature.SignerCertificate.Thumbprint -ne $thumbprint) {
        throw "Authenticode verification failed for $path"
    }
    Write-Output ("SIGNED {0}" -f $path)
    Write-Output ("SHA256 {0}" -f
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash)
}
