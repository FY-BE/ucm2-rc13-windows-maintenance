# UCM2 R2S GW1850R 力值换算与标准力标定权威规范 V1

状态：`FROZEN_FOR_IMPLEMENTATION`  
适用范围：GW1850R、ARM `ucm-runtime`、Windows `UcmConfigStudioNext`  
计量信用：工程模型；完成 GW1850R 标准力验收前不得宣称最终绝对精度。

本文件是 GW1850R 从超声相对时延到正式力值、标准力标定、人工参数维护、
下发和回读的唯一产品语义。代码、界面、测试或旧文档与本文件冲突时，以本文件为准。

## 1. 测量坐标与正式处理顺序

每通道输入 `dett_i` 是当前回波相对于本次有效零载模板的双程时延变化，单位 ns：

```text
dett_i = current_delay_i - unloaded_template_delay_i
```

它不是绝对传播时间。正式处理顺序固定为：

```text
dett_i
-> 每通道只扣一次 coupling_bias_ns[i]
-> Kmat + 当前PLC模厚 + 冻结结构模型换算基础力
-> 可选的共享零锚定单调PWL修正
-> 正式单杆力
-> 四杆总力和偏载率
```

禁止再次扣除绝对 T0、历史 `b_total_ns`、`relative_delay_intercept_ns` 或标定共同截距。

## 2. 冻结结构变量

| 符号/字段 | 数值 | 单位 | 来源与权限 |
|---|---:|---:|---|
| `D_total` | 5230 | mm | 图纸，只读 |
| `L_tooth_zone` | 1440 | mm | 图纸，只读 |
| `x_tooth_start` | 3790 | mm | `5230-1440`，只读 |
| `d_smooth` | 280 | mm | 图纸，只读 |
| `d_root` | 250 | mm | 图纸，只读 |
| `p_tooth` | 30 | mm | 图纸，只读 |
| `w_crest` | 13 | mm | 图纸，只读 |
| `w_root` | 17 | mm | `30-13`，只读 |
| `H_lock` | 288 | mm | 图纸 H，锁模机构名义传力长度，只读 |
| `eta_lock` | 0.35 | 1 | 一维载荷传递有限元工程估算，只读 |
| `L_smooth` | 1320 | mm | 图纸边界推导，只读 |
| `L_tooth_offset` | 490 | mm | 使 `L_tooth=h+490`，只读 |
| `h_ref` | 662 | mm | 当前图纸/PLC原始参考模厚，只读 |
| `h_min..h_max` | 1..950 | mm | 工程准入范围，只读 |
| `Kmat_default` | `2.1619923995787173e-15` | s·m/N | 历史工程值，可由标准力标定替换 |

截面积：

```text
A280 = pi*280^2/4 = 61575.216 mm^2
A250 = pi*250^2/4
1/Atooth = (13/30)/A280 + (17/30)/A250
Atooth = 53816.963 mm^2
```

`Atooth` 是一个齿距内光杆截面和齿根截面的串联柔度等效面积，不是把整个
1440 mm 齿区都假设为恒定满载。

## 3. PLC 模厚与受力长度

PLC 原始模厚字段使用 0.1 mm/LSB；ARM 在输入边界只换算一次：

```text
h_mm = plc_raw_mold_thickness * 0.1
```

当前名义齿段长度与模厚保持严格 1:1：

```text
L_tooth_nominal(h) = h + 490
```

右端 `H=288 mm` 锁紧段通过齿逐步把载荷传给模板，轴力从拉杆侧向模板侧递减。
一维有限元名义结果为 `eta=0.352938`，产品首版冻结为 `0.35`：

```text
L_tooth_effective(h)
  = L_tooth_nominal(h) - (1-eta_lock)*H_lock
  = (h + 490) - 0.65*288
  = h + 302.8
```

该修正只作用于固定 288 mm 锁紧传力段；`h` 增加 1 mm 时有效长度仍增加 1 mm。

几何柔度因子：

```text
G(h) = 1000 * [1320/A280 + (h+302.8)/Atooth]   (1/m)
```

若 PLC 模厚缺失、过期、非有限、机型不匹配或超出 1..950 mm，GW 正式力必须无效；
不得使用未经标识的缓存旧模厚冒充当前值。

## 4. dett 到力的正式公式

每通道先做一次残余偏置修正：

```text
dett_corr_i = dett_i - coupling_bias_ns[i]
```

基础力：

```text
K0_ns_per_N(h) = 2 * abs(Kmat) * G(h) * 1e9
F_base_i_N = dett_corr_i / K0_ns_per_N(h)
```

正式力：

```text
F_formal_i_N = PWL(F_base_i_N)
```

这里的 `Kmat` 是代码中的有效材料声弹性系数，量纲为 `s·m/N`；
`Cae_effective=2*Kmat` 已包含脉冲回波往返因子。它不是直接以 `1/Pa` 表示的
相对波速系数，不能把两种定义混用，也不能再额外乘二。

没有合格分段修正时，`PWL(x)=x`。PWL 必须从 `(0,0)` 开始、输入输出严格递增，
最多 8 个点。禁止用 PWL 隐藏错误机型、错误模厚或错误结构常量。

在 `h=662 mm`、默认 Kmat 下：

```text
L_tooth_effective = 964.8 mm
G = 0.039364629449 1/mm
K0 = 0.000170212059 ns/N = 0.170212059 ns/kN
1 ns dett ~= 5875.024 N
```

## 5. 标准力标定模型

每个标定样本必须包含同一时刻附近的四杆标准力、四路 `dett`、当前 PLC 模厚、
活动型号身份和配置身份。时间配对最大差 500 ms；每档稳定至少 2 s，首尾各裁 250 ms，
每档至少 8 组有效配对。

对样本 `j`、通道 `i`：

```text
y_ji = dett_ji - coupling_bias_ns[i]
x_ji = 2 * G(h_j) * 1e9 * F_standard_ji_N
y_ji = Kmat * x_ji + b_shared + residual_ji
```

四根杆共享一个 `Kmat` 和一个诊断用共同截距 `b_shared`。使用稳健回归拟合，
四路独立斜率只作诊断，不得分别下发为四个材料系数。

正式下发规则：

- 下发共享 `Kmat`。
- `b_shared` 保存到标定证据和残差图，不直接写入 ARM 力值公式。
- 标准力标定默认保持四路 `coupling_bias_ns` 不变。
- 至少两个正载荷档且共享零锚定单调曲线能降低代表点力值 RMSE 时，才生成 PWL；
  否则保持恒等。
- 单一模厚会标定该模厚附近的材料斜率；多个模厚的独立会话用于验证结构模型在模厚
  范围内是否成立，不拟合或改变模厚的 1:1 系数、直径、面积、288 mm 或 0.35。

## 6. 截距和每通道偏置

建立零载模板已经执行一次零点相减，因此不能把拟合 `b_shared` 再减一次。若某通道在
多次卸载、重建模板、重启和重复试验后仍存在稳定残余，才可在独立维护试验中更新该通道
`coupling_bias_ns[i]`。一次标准力拟合得到的共同截距不得自动复制到四路偏置。

## 7. Windows 人工修改权限

工程师模式允许人工修改：

| 参数 | 权限 | 约束 |
|---|---|---|
| `kmat_unified` | 可写 | 有限、正数、ARM范围校验 |
| `coupling_bias_ns[4]` | 可写 | 四个有限值，逐通道显示 |
| `force_correction` | 可写 | 0点或2..8点；零锚定、严格单调、未用槽清零 |

以下参数只读：全部结构尺寸、面积、模厚 1:1 规则、`H_lock=288 mm`、
`eta_lock=0.35`、算法状态机和 PLC 语义。

人工修改和自动标定必须使用相同事务：

```text
本地候选
-> Validate（ARM只校验，不改变运行值）
-> Apply（应用到当前运行）
-> 读取 active 文档并逐字段核对
-> SaveStartup（保存当前 active 到启动槽）
-> 读取 startup 文档并逐字段核对
```

Windows 不得仅根据按钮成功或本地缓存显示“已生效”。必须显示 ARM 事务号、结果、
active/startup 读回值、配置 identity 和 generation。

USB revision 9 的 GW1850R DeviceModel 使用 schema 3。文档必须包含
`geometry_model="GW_DRAWING_FE_ENGINEERING_V1"`。该标识连同产品 ID 37、5230/280/250 mm、
两类面积及 662 mm 参考模厚，绑定本文件的 `H_lock=288 mm` 与 `eta_lock=0.35` 公式；
Windows 不得把它伪装成 `BODY_REFERENCE_V1`。schema 3 的可写标定字段固定为：

```text
kmat_unified
coupling_bias_ns[4]
force_correction_knot_count
force_correction_input_n[8]
force_correction_output_n[8]
```

## 8. 机型身份与运行边界

- PLC/本机机型必须为 GW1850R，产品 ID `37/0x0025`。
- USB geometry identity 必须明确表示 `GW_DRAWING_FE_ENGINEERING_V1`。
- 结构模型、Kmat、四路偏置和 PWL 都必须进入配置身份或明确绑定到该身份。
- 切换机型、应用新标定或改变偏置/PWL 后必须使旧模板和旧正式结果失效并重新建立。
- ARM 是正式力唯一计算者；Windows、Orange 和 STM32 只显示或转发 ARM 正式结果。

## 9. 验收门

实现至少通过：

1. ARM 与 Windows 对同一 `h/dett/Kmat/bias/PWL` 黄金向量逐位或容差一致。
2. `h=662 mm` 时 1 ns 得到约 `5875.024 N`；模厚每增加 1 mm，公式长度只增加 1 mm。
3. `b_shared` 不进入 ARM 下发值；零载模板不发生二次扣零。
4. Validate 不改变 active；Apply 后 active 回读一致；SaveStartup 后 startup 回读一致。
5. 非法 PWL、NaN/Inf、错误机型、过期/越界模厚和结构字段修改全部拒绝。
6. 冷启动后 Kmat、偏置、PWL、型号和配置身份与保存值一致。
7. 标准力现场报告必须列出适用模厚、载荷范围、残差、重复性和未验证范围。

## 10. 证据边界

`eta=0.35` 来自图纸 H=288 mm 和 ANSYS 一维齿载荷传递模型；有限元原始输入、日志、
刚度扫频和独立矩阵复核位于：

```text
D:/UCM2_RC13_BUILD_EVIDENCE/ucm_r2s/gw1850r_fea_20260916
```

它支持工程等效长度，不授予三维局部接触、齿根强度或最终计量精度信用。最终绝对精度仍
必须由 GW1850R 实体在标准力和实际模厚范围内验证。
