# 标准力相机桥

该目录把冻结的海康七段码识别工具接到 Windows Qt 客户端。相机通过
Hikrobot MVS（GigE/USB）采图，桥接程序只向标准输出发布严格的
`forceInput_v1` JSONL：四根杆的标准力、采集时间、置信度和来源。

- `vendor/` 来自 `HIKROBOT_SEVEN_SEGMENT_CAMERA_DELIVERY_20260813_r3.zip`，
  外层 ZIP SHA-256 为
  `E2A6EF5F2DAF495D1E8F1326AF95C8CFCD76CC5732A0632DFF4967C21B27E1FC`。
- `reference_force_bridge.py` 是新增的实时适配层，不修改冻结识别数学。
- 相机原始识别值不会直接写入 ARM。标定会话拟合出的共享 `Kmat` 和共同 `b`（写入四槽）
  可在工程师页经人工确认、ARM 校验、单独确认应用后写入当前型号配置；
  开机保存需要再次确认，并核对 ARM 回执与回读。此流程不写 PLC，
  也不自动授予正式计量批准。

验证冻结文件：

```powershell
python reference_force_bridge.py --verify-vendor
```

运行相机桥：

```powershell
python -u reference_force_bridge.py --hz 10 --min-confidence 0.8
```

源码模式需要本机已安装 Python、OpenCV、NumPy 和 Hikrobot MVS SDK。正式
便携包使用 `scripts/build_reference_force_bridge.ps1` 生成独立
`UcmReferenceForceBridge.exe`，因此目标电脑不需要 Python；海康 MVS
运行库/驱动仍是与相机通信所必需的厂家组件。

工程师模式的“标准力标定”页先拍照并核对六个 ROI 与 S1–S4，确认后才可
单独开始标定。沿用的 ROI 位于当前 Windows 用户的应用数据目录；窗口中可重画，
取消不会覆盖上次确认值。相机尺寸变化须重新核对。每个档位由操作者分别开始、
结束；建议仅在载荷稳定后划定。会话数据保存在应用数据目录下的
`calibration/sessions/<UTC时间-随机号>`，界面可导出整个证据目录。

候选拟合使用相机四路 kN 和 USB 四路 `nccDeltaNs`，以 Windows 时间匹配，
每档裁掉首尾各 0.25 秒，至少 2 秒及 8 组有效配对。`calibration_candidate.py`
负责稳定性筛查和共享 `Kmat`、共同 `b` 的拟合。至少需要两档有足够载荷变化的稳定数据；
同一个 `b` 复制到 ARM 的四个 `b_total_ns` 槽位，作为拟合证据和配置身份的一部分。运行时的
`nccDeltaNs` 已相对于卸载模板归零，因此不会再次减 `b`；程序从各档代表值生成一条共享、
连续、单调且经过 `(0,0)` 的分段线性校正曲线。另算四杆独立 `kᵢ、bᵢ` 仅用于诊断图，
不进入下发候选。配对容差为相机时间戳前后各 500 ms，报告保留实际配对时间差；
离群点使用 6 倍 MAD 筛除。持续漂移写入档位告警和最终报告，但不再自动拒绝整档。
相机不可用时可从标定页启动人工模式：每档输入四杆标准力（kN）并选择现场照片，
程序把操作者、UTC、照片 SHA 和每个 USB 配对帧一并保存；USB 中断仍令整场会话失效。
这不是现场同步精度承诺。
`test_calibration_candidate.py` 提供合成数据回归验证。候选与残差保存在
`candidate.json`，逐组配对在 `matched-pairs.csv`，采集原图在 `camera/`。
原图仅在“开始记录本档”到“结束本档”之间落盘；`candidate.json` 的
`used_images` 列明实际进入拟合的图像，哈希与原图逐张核对。
会话中断会写入 `session-state.json` 的 `incomplete` 状态并保留原始证据。
该候选不执行 ARM/PLC 写入，也不表示计量批准；真实相机与 USB 联合采集仍需
现场验收。

标定页在记录档位时约每 2 秒更新一次 `DET_T`–力图：横轴为配对的 ARM
`nccDeltaNs`（ns），纵轴为相机识别的力（kN）。S1–S4 分色显示原始配对点，
每档稳健代表值用菱形标出；可绘制的剔除点打叉。四色实线为四杆独立诊断拟合，
深色粗线为共享 `Kmat` 和零锚定分段校正的运行预测；共同 `b` 只显示为拟合证据。没有完整横纵坐标的剔除样本
只在原因统计中显示。
采集中的预拟合仅用于观察趋势，不生成 `candidate.json`，也不能用于 ARM 下发。
结束并拟合时重新核对参与拟合原图的 SHA-256，正式结果和残差仍以会话报告为准。
