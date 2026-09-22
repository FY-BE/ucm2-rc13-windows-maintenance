param(
    [Parameter(Mandatory=$true)][string]$ArmSource,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$WindowsOperationTests,
    [string]$Compiler='C:/msys64/ucrt64/bin/gcc.exe'
)
$ErrorActionPreference='Stop'
$env:PATH=(Split-Path $Compiler)+';'+$env:PATH
$arm=(Resolve-Path -LiteralPath $ArmSource).Path
$include=@(Get-ChildItem "$arm/src/linux/ucm_candidate_r0" -Directory | ForEach-Object { '-I'+$_.FullName })
$include+='-I'+$arm+'/src/linux/algorithmcore/adapters/ucm2_capi/linux'
$sources=@(
    'control/ucm_device_model_control_wire_v2.c',
    'control/ucm_device_model_configuration_json_v1.c',
    'control/ucm_system_input_policy_control_wire_v1.c',
    'control/ucm_system_input_policy_v1.c',
    'control/ucm_usb_extended_wire_v2.c',
    'control/ucm_usb_log_wire_v2.c',
    'runtime/ucm_measurement_publication_v1.c',
    'runtime/ucm_result_input_status_v1.c',
    'peripherals/ucm_process_state_v1.c',
    'acquisition/ucm_acquisition_abi_v1.c'
) | ForEach-Object { "$arm/src/linux/ucm_candidate_r0/$_" }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$program=Join-Path $OutputDirectory 'generate_arm_fixtures.exe'
# LTO internalizes unused pure-C functions so no Linux service dependencies run.
& $Compiler -std=c11 -O2 -flto -fwhole-program @include "$PSScriptRoot/generate_arm_fixtures.c" @sources "$arm/src/linux/algorithmcore/adapters/ucm2_capi/linux/ucm2_sha256.c" -lm -o $program
if($LASTEXITCODE -ne 0) { throw 'ARM fixture generator compilation failed' }
Push-Location $PSScriptRoot
try {
    & $program
    if($LASTEXITCODE -ne 0) { throw 'ARM encoder/validator fixture generation failed' }
} finally { Pop-Location }
if ($WindowsOperationTests) {
    $windowsTest=(Resolve-Path -LiteralPath $WindowsOperationTests).Path
    $crosscheck=Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) 'windows-crosscheck'
    & $windowsTest --emit-crosscheck $crosscheck
    if($LASTEXITCODE -ne 0) { throw 'Windows request emitter tests failed' }
    $requests=@(Get-ChildItem -LiteralPath $crosscheck -Filter 'model-*.request')
    if($requests.Count -ne 3) { throw 'Expected three Windows request vectors' }
    foreach($request in $requests) {
        & $program --check-model-request $request.FullName
        if($LASTEXITCODE -ne 0) { throw 'ARM rejected Windows request serialization or identity' }
    }
}
$sourcePaths=@($sources)+@("$arm/src/linux/algorithmcore/adapters/ucm2_capi/linux/ucm2_sha256.c")
$sourcePaths | ForEach-Object { [pscustomobject]@{source=[IO.Path]::GetRelativePath($arm,$_);sha256=(Get-FileHash -LiteralPath $_).Hash} } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'arm-codec-source-hashes.json') -Encoding utf8
