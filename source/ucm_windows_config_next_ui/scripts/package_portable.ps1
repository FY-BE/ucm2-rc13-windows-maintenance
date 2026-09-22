[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [string]$QtRoot = 'C:\msys64\ucrt64',

    [string]$MvsRoot = 'C:\Program Files (x86)\Common Files\MVS'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$buildDirectoryPath = [IO.Path]::GetFullPath($BuildDirectory)
$outputDirectoryPath = [IO.Path]::GetFullPath($OutputDirectory)
$qtRootPath = [IO.Path]::GetFullPath($QtRoot)
$sourceExecutable = Join-Path $buildDirectoryPath 'UcmConfigStudioNext.exe'
$targetExecutable = Join-Path $outputDirectoryPath 'UcmConfigStudioNext.exe'
$referenceForceSource = Join-Path $PSScriptRoot '..\reference_force_camera'
$ethercatEsiSource = Join-Path $PSScriptRoot '..\packaging\EtherCAT_ESI'
$referenceForceExecutable = Join-Path $buildDirectoryPath `
    'reference_force_camera\UcmReferenceForceBridge.exe'
$qtBin = Join-Path $qtRootPath 'bin'
$qtQml = Join-Path $qtRootPath 'share\qt6\qml'
$deployTool = Join-Path $qtBin 'windeployqt6.exe'
$objectDump = Join-Path $qtBin 'objdump.exe'
$qtConfig = Join-Path $PSScriptRoot '..\packaging\qt.conf'
$mvsRootPath = [IO.Path]::GetFullPath($MvsRoot)
$mvsRuntimeSource = Join-Path $mvsRootPath 'Runtime\Win64_x64'
$mvsGigeSource = Join-Path $mvsRootPath 'Drivers\GigE'
$mvsUsb3Source = Join-Path $mvsRootPath 'Drivers\Usb3.0'
$mvsInstallRoot = Join-Path ([IO.Directory]::GetParent($mvsRootPath).Parent.FullName) 'MVS'
$mvsPythonSource = Join-Path $mvsInstallRoot 'Development\Samples\Python\MvImport'
$mvsLicenseSource = Join-Path $mvsRootPath 'Licenses'

foreach ($requiredPath in @(
    $sourceExecutable,
    $referenceForceExecutable,
    $referenceForceSource,
    $ethercatEsiSource,
    $deployTool,
    $objectDump,
    $qtQml,
    $qtConfig,
    $mvsRuntimeSource,
    $mvsGigeSource,
    $mvsUsb3Source,
    $mvsPythonSource,
    $mvsLicenseSource
)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required packaging input is missing: $requiredPath"
    }
}

if (Test-Path -LiteralPath $outputDirectoryPath) {
    throw "Output directory already exists: $outputDirectoryPath"
}

New-Item -ItemType Directory -Path $outputDirectoryPath | Out-Null
Copy-Item -LiteralPath $sourceExecutable -Destination $targetExecutable
$referenceForceDestination = Join-Path $outputDirectoryPath `
    'reference_force_camera'
New-Item -ItemType Directory -Path $referenceForceDestination | Out-Null
$referenceForceRoot = [IO.Path]::GetFullPath($referenceForceSource).TrimEnd('\')
Get-ChildItem -LiteralPath $referenceForceSource -File -Recurse |
    Where-Object { $_.FullName -notmatch '[\\/]__pycache__[\\/]' } |
    ForEach-Object {
        $relative = $_.FullName.Substring($referenceForceRoot.Length).TrimStart('\')
        $destination = Join-Path $referenceForceDestination $relative
        New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $destination
    }
Copy-Item -LiteralPath $referenceForceExecutable `
    -Destination (Join-Path $outputDirectoryPath 'reference_force_camera') `
    -Force
Copy-Item -LiteralPath $ethercatEsiSource `
    -Destination (Join-Path $outputDirectoryPath 'EtherCAT_ESI') `
    -Recurse

$savedPath = $env:Path
try {
    $env:Path = "$qtBin;$savedPath"
    & $deployTool `
        --release `
        --compiler-runtime `
        --no-translations `
        $targetExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE"
    }
}
finally {
    $env:Path = $savedPath
}

# MSYS2's windeployqt discovers the linked Qt libraries correctly, but its
# standalone invocation does not deploy the QML import tree or the UCRT64
# dependency closure. Keep the closure explicit and fail closed on omissions.
$qmlOutput = Join-Path $outputDirectoryPath 'qml'
New-Item -ItemType Directory -Path $qmlOutput -Force | Out-Null
Copy-Item -Path (Join-Path $qtQml '*') -Destination $qmlOutput -Recurse
Copy-Item -LiteralPath $qtConfig -Destination $outputDirectoryPath
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\packaging\Start-Offline.cmd') -Destination $outputDirectoryPath
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\packaging\Start-Engineer-Preview.cmd') -Destination $outputDirectoryPath
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\packaging\Install-Hikrobot-MVS-Drivers.cmd') -Destination $outputDirectoryPath
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\packaging\HIKROBOT_MVS_BUNDLED_README_CN.txt') -Destination $outputDirectoryPath
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\R2S_ENGINEER_COMMISSIONING.md') -Destination (Join-Path $outputDirectoryPath 'W2_联调清单.md')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\assets\fonts\OFL.txt') -Destination (Join-Path $outputDirectoryPath 'NotoSansSC-OFL.txt')
# Keep the no-screen test platform in the candidate for reproducible offline QA.
Copy-Item -LiteralPath (Join-Path $qtRootPath 'share\qt6\plugins\platforms\qoffscreen.dll') -Destination (Join-Path $outputDirectoryPath 'platforms')

$mvsOutput = Join-Path $outputDirectoryPath 'MVS'
New-Item -ItemType Directory -Path $mvsOutput -Force | Out-Null
$mvsRuntimeOutput = Join-Path $mvsOutput 'Runtime\Win64_x64'
$mvsGigeOutput = Join-Path $mvsOutput 'Drivers\GigE'
$mvsUsb3Output = Join-Path $mvsOutput 'Drivers\Usb3.0'
$mvsPythonOutput = Join-Path $mvsOutput 'Development\Samples\Python\MvImport'
$mvsLicenseOutput = Join-Path $mvsOutput 'Licenses'
foreach ($mvsDirectory in @($mvsRuntimeOutput, $mvsGigeOutput, $mvsUsb3Output, $mvsPythonOutput, $mvsLicenseOutput)) {
    New-Item -ItemType Directory -Path $mvsDirectory -Force | Out-Null
}
Copy-Item -Path (Join-Path $mvsRuntimeSource '*') -Destination $mvsRuntimeOutput -Recurse
Copy-Item -Path (Join-Path $mvsGigeSource '*') -Destination $mvsGigeOutput -Recurse
Copy-Item -Path (Join-Path $mvsUsb3Source '*') -Destination $mvsUsb3Output -Recurse
Copy-Item -Path (Join-Path $mvsPythonSource '*') -Destination $mvsPythonOutput -Recurse
Copy-Item -Path (Join-Path $mvsLicenseSource '*') -Destination $mvsLicenseOutput -Recurse

foreach ($runtimeName in @(
    'libusb-1.0.dll',
    'libgcc_s_seh-1.dll',
    'libstdc++-6.dll',
    'libwinpthread-1.dll'
)) {
    $runtimePath = Join-Path $qtBin $runtimeName
    if (-not (Test-Path -LiteralPath $runtimePath)) {
        throw "Required runtime is missing: $runtimePath"
    }
    Copy-Item -LiteralPath $runtimePath -Destination $outputDirectoryPath -Force
}

$queue = [Collections.Generic.Queue[string]]::new()
Get-ChildItem -LiteralPath $outputDirectoryPath -Recurse -File |
    Where-Object { $_.Extension -in '.exe', '.dll' } |
    ForEach-Object { $queue.Enqueue($_.FullName) }

$visited = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
while ($queue.Count -gt 0) {
    $binaryPath = $queue.Dequeue()
    if (-not $visited.Add($binaryPath)) {
        continue
    }

    $dependencyLines = & $objectDump -p $binaryPath 2>$null |
        Select-String 'DLL Name:'
    foreach ($dependencyLine in $dependencyLines) {
        $dependencyName = ($dependencyLine.Line -split 'DLL Name:', 2)[1].Trim()
        $packagedDependency = Join-Path $outputDirectoryPath $dependencyName
        if (Test-Path -LiteralPath $packagedDependency) {
            if (-not $visited.Contains($packagedDependency)) {
                $queue.Enqueue($packagedDependency)
            }
            continue
        }

        $qtDependency = Join-Path $qtBin $dependencyName
        if (Test-Path -LiteralPath $qtDependency) {
            Copy-Item -LiteralPath $qtDependency -Destination $packagedDependency
            $queue.Enqueue($packagedDependency)
        }
    }
}

$readmePath = Join-Path $outputDirectoryPath 'README_先读我.txt'
$readme = @'
启真传感 UCM Windows 产品候选版 — revision 9 / 待真机验收

1. 本包为本地联调候选，实际测试结果以随包证据为准，不是已完成实物验收的正式发布。
2. 安全离线预览：运行 UcmConfigStudioNext.exe --offline。直接运行程序会自动尝试只读 USB 连接。
3. 启动进入客户模式。客户可编辑批准的基础机械草稿；工程师 PIN 仅解锁本地操作界面，不代表取得 ARM 控制权。
4. 协议握手只接受 revision 9，不使用旧 CFG2 初始化。断线清空当前读数并有界重连，手动断开后停止重连。
5. 首页总力、四杆力及偏载只显示 ARM 正式结果；ARM 未提供字段显示 --。本地不生成替代正式结果或波形。
6. 设置分开显示编辑草稿、当前运行、开机配置。校验、应用与保存分别操作。DE168 几何和 phi=0.46 保持候选/未标定含义。
7. 配置写入、控制切换及升级按工程权限、设备 active 能力和实时控制状态开放，离线模式始终禁止写入。升级要求 ARM 处于主机托管且硬件安全状态，激活需单独确认。Windows 每秒续租；失联超过五秒后，ARM 安全停机成功才恢复自主模式，否则保持故障安全态。
8. 客户可导出 CSV；工程师可导出诊断包。无效值留空并保留原因；不完整采集包含 READS_INCOMPLETE 标记。可变文件不冒充不可变快照。
9. SHA256SUMS.txt 只用于核对本地文件完整性，不代表发布签名。R2S U2P1 升级包的本地检查不替代 ARM 对实际板卡的验收。USB 监测目标为 50 Hz（20 ms）；长稳仍需真板证据。
10. 标准力相机功能需要已安装对应 Hikrobot MVS 驱动。Windows 单帧采图和 ROI 大图已在本机用真实相机验证；当前仪表 S1–S3 仍显示状态字符，四路有效力值识别、连续采集及 USB 联合标定尚待现场验收。工程师“标准力标定”页在采集中显示以 ARM DET_T（ns）为横轴、相机力（kN）为纵轴的实时预拟合图：四色细实线为各杆独立诊断，深色粗实线为共享 Kmat 和分段修正的候选预测，剔除点打叉。该预览不能下发。ARM 391 标定使用 candidate/v3，保留 ARM 四路耦合偏置，仅下发共享 Kmat 和分段修正；拟合 b 仅留诊断，不写入偏置。标定页支持回读 ARM 当前运行/开机参数，并在应用或保存后自动核对；本地候选仍需经人工确认、ARM 校验，再分别确认应用和保存，不代表计量批准。时间戳配对上限为前后各 500 ms，实际差值另在报告中展示。
11. 此包的离板构建不代表已连接或部署板卡；实际枚举、拔插重连、持续监测和配置/升级回执须由真板证据验收。
12. Start-Engineer-Preview.cmd 打开离线工程界面，仅用于查看布局，不绕过工程师登录，也不授予设备控制权。中文表单需要明确载入 ARM schema 2 / BODY_REFERENCE_V1 当前文档。工程师可独立编辑参考坐标、杆径与等效面积；杆径不自动改写面积。旧 schema 1 和 scale 映射不能发送。
13. 工程师“模拟主站”按 FQX V1.0 小端应用 PDO 工作：DC-SYNC0 默认/最小周期 1 ms，后端测量可每 20 ms 更新；机器型号使用 29 项协议表，GW1850R=0x0025，机器状态默认下发 4。对应 ESI XML 位于 EtherCAT_ESI 目录，多字节字段可直接映射为 UINT/UDINT/ULINT。当前仍沿用 LAN9252 实验身份，量产前必须替换正式 Vendor/Product/Revision。
'@
[IO.File]::WriteAllText($readmePath, $readme,
    [Text.UTF8Encoding]::new($true))

$checksumPath = Join-Path $outputDirectoryPath 'SHA256SUMS.txt'
$checksumLines = Get-ChildItem -LiteralPath $outputDirectoryPath -Recurse -File |
    Where-Object { $_.FullName -ne $checksumPath } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = [IO.Path]::GetRelativePath(
            $outputDirectoryPath, $_.FullName).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        "$hash *$relative"
    }
[IO.File]::WriteAllLines($checksumPath, $checksumLines,
    [Text.UTF8Encoding]::new($false))

$fileSummary = Get-ChildItem -LiteralPath $outputDirectoryPath -Recurse -File |
    Measure-Object -Property Length -Sum
Write-Output ("Portable closure: {0} files, {1:N0} bytes" -f `
    $fileSummary.Count, $fileSummary.Sum)
Write-Output "Output: $outputDirectoryPath"
