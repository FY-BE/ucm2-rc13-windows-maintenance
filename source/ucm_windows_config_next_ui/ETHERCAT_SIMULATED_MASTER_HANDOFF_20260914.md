# Windows EtherCAT 模拟主站交接

更新日期：2026-09-15

当前协议：FQX V1.0 LE
状态：Windows 主站已切换到 FQX 小端应用 PDO、固定 33/35 字节和 1 ms DC-SYNC0；后端测量仍可每 20 ms 更新。小端固件已完成构建、烧录、全 Flash 回读和 100 周期 OP/PDO 真机验证。

## 1. 系统边界

Windows 程序管理三条彼此独立的连接：

- 标准力相机网口：拍照、六个 ROI、S1–S4 识别和标定采样；
- EtherCAT 设备网口：Windows 作为调试模拟主站，直连 LAN9252 从站；
- USB：工程配置、诊断和标定所需 `nccDeltaNs`。

相机、EtherCAT、USB 不要求同时存在。只有一个物理网口时可分时换线；任一链路缺失不应阻塞其他链路。

## 2. 协议身份与来源

上位机实现依据 ARM 提交 `b8109b8e07f47f035a346627ba01bd8f02e0585a`：

- ESI：`EtherCAT/ESI/HangzhouQizhen_FQX_V1.0_LE_ESI.xml`；
- 交付 ESI：`交付_富强鑫_FQX_V1.0_小端/HangzhouQizhen_FQX_V1.0_LE_ESI.xml`；
- ESI SHA-256：`FF0D7AA4EA4D7DD23956CE1AE1462E744FEDA0C2A3B885CC9D34A4CA08BA7A0C`；
- ARM HEX：`lan9252_modbus_fqx_v1.hex`；
- HEX SHA-256：`86123A2567440A62C57E5128ED77FEC8F942291DA2E4B2FE35E425E803FB57EC`；
- 当前实验室身份：Vendor `0x000004D8`、Product `0x00009252`、Revision `0x00000001`；
- RxPDO/TxPDO 内的多字节字段：little-endian；ESI 使用 `UINT`、`UDINT`、`ULINT` 原生数值类型，可直接映射 PLC 变量；
- 正式同步模式：1 ms DC-SYNC0，不声明 SYNC1；从站最小 PDO 周期为 1 ms；
- ESI 与 ARM 序列化一致性脚本：`tools/validate_fqx_artifacts.py` 已通过。

该 Vendor/Product/Revision 仍是 LAN9252 实验室身份，不可直接作为量产身份发布。

## 3. 固定 PDO 契约

### RxPDO：主站到从站

对象 `0x1600 / 0x7000`，SM2，物理地址 `0x1100`，固定 33 字节。

| 偏移 | 字段 | 类型 | 上位机说明 |
|---:|---|---|---|
| 0 | machineModel | UINT | 合法范围由 FQX 协议定义 |
| 2 | currentUtcMs | ULINT | 界面输入 0 时每周期自动填 Windows UTC ms |
| 10 | clampingForceSetpointKn | UDINT | kN，最大 180000 |
| 14 | moldThickness | UINT | 0.1 mm，不能为 0 或 0xFFFF；页面输入物理 mm 后乘10编码 |
| 16 | machineState | USINT | 0～4 |
| 17 | deviceEnabledUtcMs | ULINT | UTC ms |
| 25 | accumulatedRuntimeMs | ULINT | ms |

### TxPDO：从站到主站

对象 `0x1A00 / 0x6000`，SM3，物理地址 `0x1400`，固定 35 字节。

| 偏移 | 字段 | 类型 | 上位机显示 |
|---:|---|---|---|
| 0 | dataTimestampUtcMs | ULINT | 数据 UTC ms |
| 8 | tieBar1ForceKn | UDINT | 拉杆 1，kN |
| 12 | tieBar2ForceKn | UDINT | 拉杆 2，kN |
| 16 | tieBar3ForceKn | UDINT | 拉杆 3，kN |
| 20 | tieBar4ForceKn | UDINT | 拉杆 4，kN |
| 24 | totalClampingForceKn | UDINT | 设备独立 Total，kN |
| 28 | loadImbalanceRate | UINT | 0.01% |
| 30 | faultFlag | USINT | 0/1 |
| 31 | equipmentErrorCode | UINT | 设备错误码 |
| 33 | configurationErrorCode | UINT | 配置错误码 |

五个力值为 `0xFFFFFFFF` 时表示无效，上位机显示 `--`。上位机另外用 64 位整数计算四杆和，仅用于与设备独立 Total 做一致性复核，不能替代设备 Total。

## 4. Windows 主站实现

- `UcmEthercatProbe.exe`：通过 Npcap/WinPcap 只读 BRD，发现网卡、从站数、WKC 和 AL 状态；
- `MasterSession`：读取 SII 身份，配置 station address、SM、FMMU、DC-SYNC0，执行 INIT→PREOP→SAFEOP→OP；
- DC-SYNC0 顺序：从站先进入 SAFEOP并交换有效 PDO，再写 `0x0980=0`、`0x0981=0`，读取 `0x0910`，写 `0x09A0=periodMs×10^6 ns`、`0x09A4=0`、`0x0990=至少 100 ms 后的整周期起点`，最后 `0x0981=0x03` 激活 SYNC0；1 ms 时提前 100 个周期，并在 SAFEOP 持续交换 PDO 至少 120 ms 后再请求 OP；会话结束先停用 DC 再退回 INIT；
- 周期帧把 LWR RxPDO 与 LRD TxPDO 合并在同一个 EtherCAT Ethernet 帧中，Npcap 立即交付捕获帧；AL 状态约每 20 ms 读取一次，避免把诊断帧塞进每个 1 ms 周期；
- 周期交换：逻辑地址 `0x01000000`，独立 `LWR 33 bytes + LRD 35 bytes`，单从站期望合计 `WKC=2`；
- 周期调度使用 Windows 单调时钟；自动 UTC 值在每次 RxPDO 编码时刷新；
- OP 中持续核对 AL 状态；退出 OP 或连续 3 个周期失败时立即停止并请求 INIT；
- 每轮在 `%LOCALAPPDATA%/<Qt AppLocalDataLocation>/ethercat-sessions` 原子写入不可覆盖 JSON 证据；
- 证据 schema 为 `ucm-ethercat-master-hil/v2`，包含协议/固件身份、输出快照、最后输入、AL 历史、WKC、周期统计和失败寄存器快照。

工程师页“模拟主站”已经同步 FQX 七个 RxPDO 输入字段，并显示四杆、设备 Total、偏载率、数据时间戳、故障标志、设备错误码、配置错误码及 Windows 四杆和。EtherCAT 周期默认且最小为 1 ms；机器型号使用完整 29 项协议下拉表，`GW1850R` 对应 `0x0025`，不允许发送表外编码；机器状态默认下发 4。

## 5. QML 接口

- `ethercatProbe.scan()`：只读发现；成功后提供 `adapterId` 和 `macAddress`；
- `ethercatMaster.start(adapterId, macAddress, settings)`：启动真实主站；
- `ethercatMaster.stop()`：可取消状态切换或停止周期通信；
- `settings`：`periodMs`、`machineModel`、`currentUtcMs`、`clampingForceSetpointKn`、`moldThickness`、`machineState`、`deviceEnabledUtcMs`、`accumulatedRuntimeMs`；
- 只读状态：`running`、`profile`、`alState`、`alStatusCode`、`workingCounter`、`completedCycles`、`droppedCycles`、`rod1..rod4`、`total`、`windowsTotal`、`dataTimestampUtcMs`、`imbalancePercent`、`faultFlag`、`deviceErrorCode`、`configErrorCode`、`evidencePath`。

## 6. 历史现场结果

旧固件曾表现为 30 字节 RxPDO、22 字节 TxPDO，可到 SAFEOP，但 OP 报 `0x001B SyncManager watchdog`。当时 `AL Event Request 0x0220=0x0400`，说明 LAN9252 收到过程输出而 PDI 未消费。该结果只属于旧现场固件，证据保存在 `evidence/ethercat/20260914-live-master-30x22.json`，不能用来证明 FQX V1.0 成功或失败。

FQX 固件修改了对象字典和 PDO 应用映射；必须烧录上述精确 SHA-256 的 HEX。上位机已移除旧 30/20、30/22 自动兼容分支，避免把旧固件误识别成 FQX。

源码复核确认 `DC_SUPPORTED=1`，PA1/EXTI1 配置为 SYNC0 下降沿中断，`EXTI1_IRQHandler()` 调用原 `Sync0_Isr()`。SII 脉冲长度为 0，使用 LAN9252 Acknowledge Mode；ISR 完成后读取 `0x098E` 清除锁存。Windows 端按 ESI 的 DC-SYNC0 正式模式启动，不使用 SYNC1。

2026-09-14 小端基线现场闭环结果：真实 LAN9252 从站完成 INIT→PREOP→SAFEOP→OP，连续 100 个 20 ms 周期，`WKC min/max=2/2`、丢周期 0，并正常退回 INIT。证据位于 `work/ethercat_fqx_flash_20260914/ethercat_master_final_identity_100_cycles.json`；该记录只证明切换大端前的链路和状态机基线，不能证明当前大端应用 PDO。大端版本必须生成新的 Flash 读回和 OP/PDO 证据。该次 TxPDO 显示无效力值及设备错误码 1，符合尚未接入有效后端力数据的状态，不代表业务测量链已验收。

2026-09-14 大端版本现场闭环结果：构建 HEX SHA-256 为 `3A77B7897DACF23EB3BC7D34D525EB8C5221A5CACA79E86E16135C01417FB3A0`，整片 512 KiB Flash 读回和由该 HEX 展开的镜像逐字节一致，摘要证据为 `evidence/ethercat/20260914-fqx-be-flash-readback.json`。真实从站随后完成 INIT→PREOP→SAFEOP→OP，连续 100 个 20 ms 周期，`WKC min/max=2/2`、丢周期 0，并正常退回 INIT；完整主站证据为 `evidence/ethercat/20260914-fqx-be-master-100-cycles.json`。设备错误码按大端正确解码为 1，配置错误码为 0；五个力值仍为无效哨兵，符合后端尚未提供有效力数据的当前状态。

2026-09-15 已构建并烧录当前小端 1 ms 固件 `86123A2567440A62C57E5128ED77FEC8F942291DA2E4B2FE35E425E803FB57EC`。整片 512 KiB Flash 读回与 HEX 展开镜像逐字节一致，展开镜像与读回镜像 SHA-256 均为 `37F3C2CF25CEB0BC2BE96496383A57813FDA5D6CFF66532FFEE289BC0D4BA455`。Windows 模拟主站以 `GW1850R (0x0025)`、机器状态 4、1 ms DC-SYNC0 完成 INIT→PREOP→SAFEOP→OP，连续 100 个周期 `WKC min/max=2/2`、丢周期 0、AL 状态码 0，并正常退回 INIT；完整证据为 `evidence/ethercat/20260915-fqx-le-1ms-master-gw1850r-state4-100-cycles.json`。配置错误码、设备错误码和故障标志均为 0；从站有效回传四杆力 69/68/69/70 kN、设备 Total 275 kN、偏载率 1.10%，当前小端双向 PDO 和后端测量链路均已验证。

## 7. 闭环验收

1. 烧录前记录板卡、目标芯片、现有 Flash 备份或可恢复基线；烧录后记录 HEX SHA-256；
2. 只读发现一个从站并核对 Vendor/Product/Revision；
3. 以 1 ms DC-SYNC0 成功完成 INIT→PREOP→SAFEOP→OP，过程 `WKC=2` 稳定；
4. 改变七个 RxPDO 输入，核对 ARM/后端收到的字段、范围校验和 UTC 行为；
5. 后端提交已知四杆、Total、偏载率和错误码，核对 35 字节 TxPDO 每一字段；
6. 后端停止更新超过 100 ms 时，核对五个力值变为 `0xFFFFFFFF` 且设备错误码为 1；
7. 断网、停止和异常时不保留伪在线状态，并安全回退 INIT；
8. 保存完整 JSON 证据，记录周期数、丢失周期、WKC 范围、AL 历史和最终状态。

只有上述真机步骤全部通过，才可表述为“FQX EtherCAT 主站闭环完成”。离线 codec、构建或发现从站均不能替代现场闭环验收。
