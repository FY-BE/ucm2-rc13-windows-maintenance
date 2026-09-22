# Windows 客户端与 ARM 后端对接交接

日期：2026-09-14
范围：本仓库 `ucm_windows_config_next_ui` 的 Qt/QML 客户端、Windows WinUSB 适配层、海康相机桥及 FQX EtherCAT 模拟主站。此文档描述**当前代码已经实现的接口和待联调事项**；它不把离线测试视为真实设备验收。

## 已完成的工作

- 客户首页、工程师工作台和共用控件已统一为浅色卡片布局；中文统一使用 Windows 原生 `Microsoft YaHei UI`，避免可变字体在弹窗中的乱码。客户页保留设备线稿；离线模式显示等待数据，不制造设备读数或曲线。
- 四杆趋势的**显示曲线**改为穿过原始有效点的单调三次插值；无效区间仍断开，悬停值和 CSV 仍取原始数据。原始 ADC 波形保留采样值，仅优化描边和光标绘制，移动光标不重绘整张波形。对应提交：`c7a2e84`。
- 既有“标准力标定”页包含六个 ROI（S1–S4、Total、Other）拍照核对、多档采集、配对筛查、拟合过程图、候选确认，以及分开的 ARM 校验、运行应用和开机保存入口。Total/Other 只供画面核对，拟合使用 S1–S4。其现状和数据边界如下文；不要把这部分 UI 当成整机现场验收结论。
- 工程师端“模拟主站”已切换到 FQX V1.0 **小端应用 PDO**、固定 33/35 字节和 1 ms DC-SYNC0；多字节应用字段与 EtherCAT 协议结构均按小端处理。界面机器型号为完整 29 项协议下拉表，`GW1850R=0x0025`，机器状态默认下发 4。后端测量结果仍可每 20 ms 更新，期间重复最近完整结果。小端固件的构建、烧录和真机 OP/PDO 结果以最新交付证据为准；有效后端测量值仍需双方联合验收。详细合同和证据边界见 [`ETHERCAT_SIMULATED_MASTER_HANDOFF_20260914.md`](ETHERCAT_SIMULATED_MASTER_HANDOFF_20260914.md)。
- revision 9 运行参数页已改为由 msg17 目录生成，使用一个 CFG2 事务批量应用并回读；LNA/PGA 枚举档位由目录约束生成，VCNTL 设定值/运行回读/AGC上下界统一限制为2621..39322，HV、AGC范围与算法门限按目录范围编辑。数字增益字段17和数字TGC字段20在产品合同中固定为0且严格只读。Apply/SaveStartup分离，真正发送前 transport 重新确认 `HOST_MANAGED`、有效租约、硬件安全停止和 UHW2 明确卸载。
- 最近一次相关离线验证为 43/43 项 CTest 通过；本次 EtherCAT 现场验证另完成 100 个 OP 周期。真实相机 + USB 同步、真实轮询吞吐、ARM 参数 Apply/SaveStartup 与冷启动回读，以及带有效后端力数据的 TxPDO 业务字段仍需联合验收。

## 对接位置和数据流

```text
ARM USB revision 9
  → backend/usb_functionfs_transport.cpp
  → backend/config_session.cpp
  → dashboard_poll_worker.cpp（独立线程、串行 USB 会话）
  → dashboard_bridge.cpp（QML 属性/信号）
  → 客户页、工程师页、趋势/诊断/标定页

海康 MVS 相机
  → reference_force_camera/reference_force_bridge.py 或打包的 UcmReferenceForceBridge.exe
  → reference_force_source.cpp（逐行 JSON、进程管理）
  → calibration_controller.cpp（ROI、会话证据、拟合候选）
  → components/CalibrationPage.qml

Windows EtherCAT 网卡
  → ethercat_port_probe.cpp（只读发现和身份线索）
  → ethercat_master_session.cpp（SII、SM/FMMU、DC-SYNC0、AL 状态机、周期 PDO）
  → ethercat_master_controller.cpp（异步启停、证据落盘）
  → components/EthercatMasterPage.qml
```

前端**没有另一个 HTTP API**。后端同事应对接现有 USB revision 9 消息和型号事务，Windows 到 QML 的稳定入口是 [`dashboard_bridge.h`](dashboard_bridge.h) 的 `DashboardBridge` 属性、信号与 `Q_INVOKABLE` 方法；相机入口是 [`reference_force_source.cpp`](reference_force_source.cpp)。不要绕过有效位，直接把原始数值塞给页面。

## ARM → Windows：显示及标定所需合同

| 数据/状态 | Windows 接收后的字段 | 单位与显示规则 | 后端需要保证或提供的证据 |
| --- | --- | --- | --- |
| 连接及能力 | `realUsbOpened`、`armReceiverContacted`、`protocolRevision`、能力/操作掩码 | 两个连接条件都成立才算连接；产品合同为 revision 9 | 枚举、协商和能力回读；型号 `modelValidate`/`modelApply`/`modelSave` 分别由能力位开放 |
| 正式总力、偏载 | `formalForceValid`、`formalTotalN`、`imbalanceIndex` | 力是 **N**；偏载在界面乘 100 显示为 %；正式无效显示 `--` | 正式有效门、原因码和同一帧数据；Windows 不计算正式力 |
| 四杆力 | `rod[i].forceN`、`forceAvailableMask`、`rodValidMask`、`forceReason` | 单杆力是 **N**；仅 `forceAvailableMask` 对应位置 1 才显示数值；正式杆有效和仅诊断可用要区分 | 四杆有效位和原因；缺失诊断字段不得用 0 伪装 |
| 四杆应变、低载偏载门 | 可选消息31的 `strainMicrostrain`、`lowLoadBiasInvalid`、`biasValidMinTotalForceN` | 应变是 **με**；低于默认20000 N时四杆/总力/应变仍显示，偏载率显示“低载无效” | feature bit13；消息31固定160 B；与消息30的 publisher/session/sequence/frame 四项身份匹配，错配只隐藏扩展 |
| 标定输入 | `rod[i].delayNs`、`measurementValidMask`、`pairedInputStatus.effectiveThicknessUm`、`moldValid` | 四路 `nccDeltaNs`/`delayNs` 是 **ns**；标定用测量有效位，不要求正式力已经有效 | 结果帧与同帧输入配对；有效模厚、活动型号文档和配置身份可回读 |
| 帧身份与时间 | `generation`、`publishedMonotonicNs`、`sessionId`、`sequence`、`frameCounter`、`captureRequestId`；Windows 另记 `observedUtcMs`、`observedMonotonicNs` | 六项身份用于去重/核对；ARM 单调时间只用于帧排序，**不直接与相机时钟相减** | 提供可核查的消息 30/45 原始记录及配对身份；重复、错配和断流可复现 |
| 运行状态 | 独立 runtime/compound status 与产品结果 | “消息完成身份配对”不等于完整运行链就绪；PLC 输入状态不代替 ARM 过程状态 | 状态读回、状态时间和故障原因 |
| 原始波形 | `WaveformSnapshot`：四路 `qint16[]`、采样率、窗口起点及帧身份 | 原始 ADC 采样不做数值平滑；仅画线和光标分层 | 若产品 revision 9 要显示此波形，先明确设备能力、读取合同和调度频率（见待对接项） |

关键实现：[`backend/config_transport.h`](backend/config_transport.h) 的 `TelemetrySnapshot`/`WaveformSnapshot`，[`backend/usb_product_wire_v9.h`](backend/usb_product_wire_v9.h)，[`dashboard_poll_worker.cpp`](dashboard_poll_worker.cpp)，[`dashboard_bridge.cpp`](dashboard_bridge.cpp)。

轮询**目标**为每 20 ms 一次；忙时跳过，不排队补采。产品状态约每 5 s、运行状态约每 500 ms 读取一次。趋势点使用 Windows 接收时刻，按四杆可用位画线；掉线/无效写入 gap，CSV 不填假值。需要现场测得实际收到的新帧率、重复帧率及延迟分布，不能用目标 50 Hz 代替实测。

## 相机、ROI 与候选拟合

相机桥默认以 10 Hz 工作。其 stdout 是逐行 JSON；实时有效帧必须满足 `schema_version=forceInput_v1`、`source=evidence_level=force_input_camera`、`status=ok`、有效 `timestamp_ms`、`confidence≥0.80`，且 `force_kN` 恰好为四个有限非负数。会话采样另记录 Windows `monotonic_ns`、识别状态及原图信息；相机进程由 Qt 管理。桥可用 `UcmReferenceForceBridge.exe` 或 Python 脚本，现场仍需海康 MVS 驱动。相机不可用时可改用人工四杆标准力和现场照片，仍按同一 USB 帧身份与会话中断规则保存证据。详见 [`reference_force_source.cpp`](reference_force_source.cpp) 和 [`reference_force_camera/reference_force_bridge.py`](reference_force_camera/reference_force_bridge.py)。

ARM 运行进度使用 revision 9 消息 48 的 256 字节、CRC 封装对象，Windows 每 200 ms 最多读取一次且忙时跳过；顶部显示阶段和总体进度，维护页显示等待原因、耗时、AGC 当前/最佳点、PLC、模板、零载参考、动作和故障。消息 47 使用 64 字节动作请求和 128 字节回执，只接受刷新零载时延参考（动作1）和重启 AGC（动作3）；旧的力域清零动作2已经停用。Windows 同时核对外层和对象内事务号，不把断线结果猜成成功。

先拍照并确认六个 ROI，再单独开始标定；确认门只检查 S1–S4 的识别质量，Total/Other 是辅助显示。标定会话要求操作者、ARM 活动型号文档和配置身份，起始磁盘可用空间至少 1 GiB。采集中分别写 `camera.jsonl`、`arm.jsonl`、档位和上下文；USB 记录四路 ns、测量掩码、六项帧身份、Windows UTC/单调时间、同帧模厚和配置身份。USB/相机断开、配置身份或会话几何量变化、Windows 时钟跳变、证据写入失败会令会话成为 `incomplete`，不跨中断拼接。实现见 [`calibration_controller.cpp`](calibration_controller.cpp)。

拟合工具在 [`reference_force_camera/calibration_candidate.py`](reference_force_camera/calibration_candidate.py)。按 Windows 时间配对，最大差 **500 ms**；每档至少 **2 s**，首尾各裁剪 **0.25 s**，裁剪后至少 **8** 组完整配对。筛查低置信度、无效掩码、重复帧和 6 倍 MAD 离群；持续漂移写入告警但不自动拒绝整档。每档每杆先形成稳健代表值，再拟合正的共享 `Kmat` 和共同 `b`（仅作拟合诊断，不能复制到 ARM 耦合偏置）。相机 kN 转 N，拟合证据公式为 `DET_Tᵢ(ns) = 2 × Kmat × G × 10⁹ × Fᵢ(N) + b(ns)`。ARM 391 输入是相对于卸载模板的 `Δt`，先独立扣除当前 `coupling_bias_ns[i]` 一次；拟合使用同一修正坐标，诊断 `b` 不下发。先用共享 `Kmat` 得到基础力，再通过共享、连续、单调且固定经过 `(0,0)` 的分段线性曲线修正真实非线性残差。候选及残差、配对差、载荷范围、不确定度、分段节点和验证情况写在会话目录；只有两档等验证不足时不能称为独立验证通过。拟合成功和操作者“确认候选”都**不代表**计量批准或 ARM 已写入。

## Windows → ARM：候选下发顺序

[`components/CalibrationPage.qml`](components/CalibrationPage.qml) 已提供四个不同动作，不能合并为单次“确认即下发”：

1. **确认拟合候选**：只在本地写 `candidate-review.json`，核对 `candidate.json` 哈希；不发送 USB。
2. **请 ARM 校验**：操作码 `3`，提交完整型号文档及当前活动配置身份，要求 ARM 返回成功回执；不应改变运行配置。
3. **应用到当前运行**：操作码 `1`，仅在本会话校验成功、活动配置身份仍与拟合时一致后发送；等待 ARM 成功回执，并再次读取活动配置，确认参数确实变为本次候选。
4. **保存为开机配置**：操作码 `2`，仅在本次候选已经应用并回读匹配后执行；再读取 startup/active 文档和身份确认一致。保存与运行应用分开确认。

写入入口为 `DashboardBridge::submitCalibrationCandidate(operation, authorized)`，复用 `productDocumentRequested(domain=0, operation, json, authorized, expectedIdentity)` 和后台型号事务。Windows 还检查工程师权限、在线非离线模式、设备能力、主机托管/硬件安全停止、UHW2明确 `VALID=1/LOADED=0`、无未闭合事务、候选证据完整，以及型号合同为 A001/DE168 的 `BODY_REFERENCE_V1` 或 GW1850R schema 3 的 `GW_DRAWING_FE_ENGINEERING_V1`。回执状态通过 `calibrationOperation`（`operation`、`outcome`、`busy`、`message`、`evidenceWarning`）返给页面，并保存为 `arm-operations.json`；断线时结果标成 `unresolved`，应先查询/对账，禁止盲目重发。后台应分别提供 Validate、Apply、SaveStartup 能力和可回读的结果；不能仅凭 USB 命令已发送就报成功。实现见 [`dashboard_bridge.cpp`](dashboard_bridge.cpp)、[`dashboard_poll_worker.cpp`](dashboard_poll_worker.cpp)。

## 后端同事下一步要交付和共同确认

1. 一组真实 revision 9 USB 记录：能力协商、产品结果消息 30、可选诊断消息31、同帧输入消息 45、身份、测量/力有效掩码、正式原因码、应变、低载偏载门、模厚与活动配置身份；包含有效帧、无效帧、扩展错配、重复帧、掉线和重新连接。
2. 明确 ARM `modelValidate`、`modelApply`、`modelSave` 的能力位、前置安全状态、回执字段及 active/startup 回读行为；在受控现场逐步验证“校验不改值、应用改运行值、保存后冷启动仍一致”。
3. 和 Windows 一起采集相机与 USB 的真实时间差分布、丢帧率、USB 实际轮询吞吐；确认 500 ms 配对上限在现场是否合理。所有原图、JSONL、候选、回执及哈希留在同一会话证据中。
4. 确认产品 revision 9 是否开放原始波形能力。**当前产品路径不发波形请求**；若要展示真实波形，需要共同定义并验证读取合同及对轮询的影响。
5. 对齐趋势时窗：当前缓冲限制为 **600 点且最多 60 s**。达到 50 Hz 时只保留约 12 s，和页面“最近 60 秒”不一致；由前后端共同确定实际时窗、下采样或缓冲策略后再改口径。

离线 UI/协议测试是软件回归证据；相机识别、USB 时间配对、ARM 参数生效与计量准确度各需独立的现场记录。


## C392 对齐 ARM 391：真实标定参数回读

候选只接受 `candidate/v3`：下发 `kmat_unified` 与共享 PWL 修正，保留拟合开始时 ARM 的四路 `coupling_bias_ns`，不写旧 `b_total_ns`。相机原始时延与拟合修正坐标分别存储，图上仍显示原始时延。A001/DE168 使用 `BODY_REFERENCE_V1`；GW1850R 使用 DeviceModel schema 3 和 `GW_DRAWING_FE_ENGINEERING_V1`，按每帧 PLC 模厚、冻结图纸尺寸及 `H=288 mm、eta=0.35` 计算结构因子。完整语义以 [`GW1850R_CALIBRATION_AUTHORITY_V1.md`](GW1850R_CALIBRATION_AUTHORITY_V1.md) 为准。

标定页新增“回读 ARM 当前参数”：不需要本地拟合候选，读取 active/startup 实际文档，显示 Kmat、四路耦合偏置、所有 PWL 节点、配置身份及两份参数是否一致。校验/应用/保存成功后自动读取；断线或读取失败清空旧读数。

C392 真 USB 证据验证 Validate → Apply → SaveStartup → active/startup 回读一致，并恢复原参数。验证的是物理 USB 写入链路，未授予标准力计量精度或冷启动持久性信用。
