# R2S USB revision 9 可写参数合同

状态：Windows R2S 收口候选，离线合同与 UI 已通过回归，待和同一 ARM 候选完成真实 USB HIL。ARM 必须逐字节实现并与本目录黄金向量对拍。多字节数均为 little-endian；USB wire revision 固定为 9；沿用现有 40 B frame header。

## 1. 消息、写入门与事务

| msg | 名称 | 请求 payload | 成功响应 payload |
|---:|---|---|---|
| 17 | `PARAMETER_CATALOG` | 16 B 分页请求 | 32 B头 + N×48 B descriptor |
| 18 | `CONFIG_STATE` | `UCQ2` 32 B | `UCR2` 768 B |
| 19 | `CONFIG_APPLY` | `CFG2` revision 4，384 B | `UCR2` 768 B |

同一时间最多一个写事务。重复 `transaction_id` 且 payload 相同必须返回原终态，不得重复执行；同 ID 配不同 payload 必须拒绝。msg19 只在 `HOST_MANAGED`、host owner、租约有效、硬件安全停止、UHW2 明确 `VALID=1/LOADED=0`、无过渡事务时接受。自主模式可读 msg17/msg18，msg19 返回 `INVALID_STATE`，不得暂存。

操作码：`1=APPLY_ACTIVE`、`2=SAVE_STARTUP`、`3=VALIDATE_ONLY`。Apply 只改变活动配置；SaveStartup 只保存已经应用且回读一致的活动对象；ValidateOnly 不改变活动或启动配置。查询：`1=LATEST`、`2=ACTIVE`、`3=STARTUP`、`4=TRANSACTION`。

`burst_cycles` 配置 field ID 固定为 **98**，CFG2 offset **136**，`uint32`，1..8，步进1。实际值 field ID **133**，严格只读，来自 UHW2 offset 24。msg19 成功必须同时满足：活动 CFG2 offset 136 等于请求值、field result index 6 为0、UHW2 offset 24 等于实际生效值；否则不得回 `APPLIED`。

## 2. msg17 参数目录

请求16 B：0 `start_index:u32`，4 `maximum_entries:u32`（1..64），8 `expected_catalog_crc32:u32`，12 reserved=0。

响应头32 B：0 token `0x32504355`，4 schema/header=`2/32`，8 catalog CRC32，12 total=`65`，16 start，20 count，24 flags（bit0 MORE），28 reserved=0。单页最多64项，因此65项目录必须分页读取。descriptor 48 B：0 field ID u32，4 group ID u16，6 value kind u8，7 scope u8，8 access flags u32，12 constraint flags u32，16/24/32 min/max/step binary64，40 enum mask u64。

access flags：bit0 READ，bit1 WRITE_SUPPORTED，bit2 WRITE_ACTIVE，bit3 ASSET_ONLY，bit4 SESSION_RESTART，bit5 RESEAL，bit6 FROZEN。`WRITE_SUPPORTED` 仅表示实现；实时可写性由控制权与 `active_config_group_mask` 决定。目录与 CRC 在自主/托管切换时不变。

固定65项如下：

wire 顺序固定为：`4,1,2,3,48..57,16..20,64..66,80..84,96..98,112,116..118,122..127,144..155,113..115,128..136,99`。顺序参与目录 CRC，不得按 field ID 重新排序。

| field ID | 含义 | 通道 / wire位置 |
|---:|---|---|
| 1/2/3/4 | 拉杆总长/A端测点/纵波速度/型号 | 型号文档；CFG2 120/124/128/112是绑定快照 |
| 16 | 四路LNA dB（逐杆） | 运行配置；CFG2 264/268/272/276 |
| 17 | 四路数字增益步（逐杆） | 产品固定0、严格只读；CFG2 280/284/288/292 |
| 18/19 | PGA/VCNTL | 运行配置；CFG2 140/144；VCNTL物理范围2621..39322 |
| 20 | 数字TGC | 产品固定0 dB、严格只读；CFG2 148 |
| 48–50 | B/D/E段长度 | 型号文档 |
| 51–53 | AB/AC/AD等效面积 | 型号文档 |
| 54–57 | B/D角度、A段偏置/量化步长 | 型号文档 |
| 64/65/66 | 最小NCC/峰比/SNR | 运行配置；CFG2 152/156/160；峰比1.1=1100000，SNR signed milli-dB |
| 80/81 | 模板确认帧/最小有效力N | 运行配置；CFG2 164/168 |
| 82 | 偏载率报警阈值 | 运行配置；CFG2 172，ppm；20%=200000 |
| 83/84 | 报警确认帧/偏载率有效最小总力N | 运行配置；CFG2 180/176；默认20000 N |
| 96 | 测量节拍Hz | 运行配置；CFG2 116，固定50 |
| 97 | 标称HV V | 运行配置；CFG2 132，50..250，步进5 |
| 98 | 每帧burst周期数 | 运行配置；CFG2 136，1..8，步进1 |
| 99 | PL采集窗口起点 | 运行配置；CFG2 304，0..91808，步进1；改变后重新确认AGC与模板 |
| 112 | AGC enabled | 运行配置；CFG2 184 bit0 |
| 113/114/115 | 受力冻结/接收先于HV/HV先于burst | FROZEN只读；CFG2 184 bits1/2/3，必须1 |
| 116–118 | AGC peak low/high/emergency | 运行配置；CFG2 188/192/196，permille |
| 122 | AGC最大时延抖动ps | 运行配置；CFG2 200 |
| 123/124 | AGC最小有效率/最大削顶率 | 运行配置；CFG2 204/208，ppm |
| 125–127 | AGC确认帧/稳定帧/最大调整次数 | 运行配置；CFG2 212/216/220 |
| 128 | 当前四路LNA | 运行态只读；UHW2 40..55 |
| 129/130/131 | 当前PGA/VCNTL/数字TGC | 运行态只读；UHW2 28/32/36；VCNTL为2621..39322 |
| 132/133 | 当前标称HV/当前burst | 运行态只读；UHW2 20/24 |
| 134/135/136 | 当前AGC阶段/受力状态/实测节拍 | 运行态只读；UHW2 12/flags bit1/16 |
| 144/145 | VCNTL自动范围min/max | 运行配置；CFG2 224/228；均限制在2621..39322 |
| 146–148 | VCNTL fine/medium/coarse步长 | 运行配置；CFG2 232/236/240 |
| 149 | PGA允许掩码 | 运行配置；CFG2 296；bits0/1=24/30 dB |
| 150 | LNA允许掩码 | 运行配置；CFG2 300；bits0/1/2=12/18/24 dB |
| 151–153 | 自动HV min/max/step V | 运行配置；CFG2 244/248/252；step=5 |
| 154/155 | 自动burst min/max | 运行配置；CFG2 256/260；1..8 |

IDs 119–121 不分配。NCC、峰比、SNR 只使用 IDs 64–66，测量质量门与AGC共用，避免同一个wire值有两套可写身份。

## 3. CFG2 revision 4（384 B）

| offset | bytes | 字段 |
|---:|---:|---|
| 0 | 4 | token `0x32474643` |
| 4/6 | 各2 | schema=2 / bytes=384 |
| 8 | 4 | CRC32；计算时清零 |
| 12/16/20 | 各4 | flags=0 / operation / revision=4 |
| 24/32/40 | 各8 | transaction / base / candidate generation |
| 48/56 | 各8 | present / changed group mask |
| 64/68 | 各4 | catalog / device profile CRC32 |
| 72 | 32 | object SHA256；计算时CRC和SHA均清零 |
| 104 | 8 | reserved=0 |
| 112/116 | 各4 | device model / measurement Hz |
| 120/124/128 | 各4 | rod length / measurement point / velocity |
| 132/136 | 各4 | nominal HV / burst cycles |
| 140/144/148 | 各4 | PGA / VCNTL / digital TGC |
| 152/156/160 | 各4 | NCC ppm / peak ratio ppm / SNR signed milli-dB |
| 164/168/172/176/180 | 各4 | template frames / min force / bias ppm / low-load / alarm frames |
| 184 | 4 | AGC flags |
| 188/192/196 | 各4 | peak low/high/emergency permille |
| 200/204/208 | 各4 | jitter ps / valid ppm / clipping ppm |
| 212/216/220 | 各4 | confirm / settle / max adjustments |
| 224/228 | 各4 | VCNTL min/max |
| 232/236/240 | 各4 | VCNTL fine/medium/coarse step |
| 244/248/252 | 各4 | automatic HV min/max/step |
| 256/260 | 各4 | automatic burst min/max |
| 264 | 16 | four LNA dB；每项12/18/24 |
| 280 | 16 | four digital gain steps；产品每项固定0 |
| 296/300 | 各4 | PGA/LNA allowed mask |
| 304 | 4 | capture window start；0..91808 |
| 308 | 76 | reserved=0 |

AGC flags bit0可配置；bits1–3固定为1。自动顺序固定为“接收增益→标称电压→burst”；受力时不得写AFE、HV或burst。

Windows 参数页只从 msg17 的目录生成正式运行参数编辑器。LNA/PGA 等枚举字段使用目录枚举掩码生成下拉选项，其余整数参数使用范围和步进；字段17/20不进入编辑器。每次真实发送仍由 transport 重新读取控制权与 UHW2，只有 `HOST_MANAGED`、租约有效、硬件安全停止且明确卸载才提交。Apply 后同时核对活动 CFG2 与 UHW2 实际控制量；SaveStartup 与 Apply 分开，并在保存后独立读取启动配置。

Apply/Validate要求 candidate=base+1 且 changed mask非零。SaveStartup要求 candidate=base、changed mask=0，全部配置值与活动对象一致。CFG2型号字段是CAS绑定快照，msg19不得借此修改型号文档，且必须匹配 `device_profile_crc32` 指向的活动型号。

## 4. UCQ2、UCR2、UHW2和字段回执

`UCQ2` 32 B：0 token `0x32514355`，4 schema/bytes=`2/32`，8 CRC32，12 query kind，16 transaction ID，24..31 reserved=0。仅TRANSACTION查询要求非零transaction ID。

`UCR2` 768 B：0 token `0x32524355`，4 schema/bytes=`2/768`，8 CRC32，12 receipt kind，16 result，20 persisted，24 operation，28 field count=48，32 transaction ID，40/48/56 base/requested/active generation，64 changed mask，72..79 reserved，80..463 active CFG2，464..655为48个int32 field results，656..767为UHW2。

field result index：0..18=`device_model, Hz, rod_length, point, velocity, HV, burst, PGA, VCNTL, TGC, NCC, peak_ratio, SNR, template_frames, min_force, bias, low_load, alarm_frames, AGC_flags`；19..37=`peak_low, peak_high, emergency, jitter, valid_rate, clipping, confirm, settle, max_adjustments, vcntl_min, vcntl_max, vcntl_fine, vcntl_medium, vcntl_coarse, hv_min, hv_max, hv_step, burst_min, burst_max`；38..41=四路LNA；42..45=四路数字增益；46=PGA mask；47=LNA mask。0表示接受，1..12沿用revision 9 status；AGC_flags一项覆盖IDs112–115。

receipt kind：1 active、2 applied、3 rejected、4 accepted、5 validated、6 saved-startup。saved-startup必须persisted=1；applied/validated必须persisted=0。异步执行先回accepted，Windows用msg18 transaction查询终态。

`UHW2` 112 B：0 token `0x32574855`，4 schema/bytes=`2/112`，8 flags（bit0 VALID、bit1 LOADED），12 AGC stage，16 actual Hz，20 actual HV，24 actual burst，28 actual PGA，32 actual VCNTL，36 actual TGC，40..55 four LNA，56..71 four digital gain，72 valid-channel mask，76 hardware result，80 published monotonic ns，88 action sequence，96 hardware generation，104 AGC reason，108..111 reserved=0。AGC stage为0..12，AGC reason为0..11。VALID=0时flags和12..111全零；VALID=1时实际值通过同一范围校验。

## 5. 黄金向量

目录：`tests/fixtures/arm_product_v9_parameter_config`；生成器：`tools/generate_usb_parameter_config_v9_vectors.py`。C++ codec测试与Python向量逐字节比较。

| 文件 | SHA256 |
|---|---|
| `parameter_catalog_descriptors.bin` | `e394b6411475a32df7928bafc4ed935c45371b01e2473793b80091f37520e96d`（CRC32=`0xa6dc1ee6`） |
| `config_apply.bin` | `cc48cdcaeda891f813f7eaecda630bfdfe939bf66394a85b82dea7a05e48be0e` |
| `config_save_startup.bin` | `a41795a2e8678de63bc815afa09f34c4d9d3345d262cc5edfe8fd069fc92d187` |
| `config_validate.bin` | `82cdf8f8e69d551041e394d24a19f5ff5acd779b0d0038ac5fe44d897d97a1e8` |
| `query_active.bin` | `44b31349f5d03ba0c03137cc8d73576c07ba9e99aef64eb25433fd85d4dab0b7` |
| `query_transaction.bin` | `fe84fd3f02b5ca820972ec80ab81307f15815dd499d7dd96b4413ee57af7eb73` |
| `receipt_applied.bin` | `465e7a9c196eed6e69cc4a217d6b44bea8d33c9ac38977da4f49c840dad889d5` |

## 6. RESULT_DIAGNOSTICS（msg31，只读可选扩展）

能力位为 `active_feature_mask bit13`。未开放时 Windows 不发送 msg31；开放后每个 msg30 后读取一次，并只在 `publisher_generation/session_id/sequence/frame_counter` 与 msg30 一致时合并。身份不一致或扩展读取失败只隐藏应变与诊断扩展，不影响已验证的 msg30 正式力。

固定160 B：0 token `URD9` (`0x39445255`)；4 schema=1；6 bytes=160；8 CRC32（计算时清零）；12 flags，允许掩码严格为 `0x000FFF0F`：bit0 VALID、bit1 LOW_LOAD_BIAS_INVALID、bit2 PLC_STALE、bit3 ANY_PREDICTION，bits8..11逐杆预测掩码，bits12..15逐杆质量降级掩码，bits16..19逐杆共同趋势异常掩码；bit3必须与bits8..11是否非零一致。16 publisher generation；24 published monotonic ns；32 session；40 sequence；48 frame；56 rod valid mask；60 `bias_valid_min_total_force_n`；64四杆 `strain_microstrain` binary64；96四杆 invalid reason；112 PLC state；116 AGC state（0..12）；120 AGC reason（0..11）；124 actual PGA dB；128四杆 actual LNA dB；144 actual VCNTL DAC；148 actual nominal HV；152 actual burst；156 reserved=0。
