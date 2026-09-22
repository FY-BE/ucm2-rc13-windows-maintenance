# Revision 9 波形切片合同

ARM runtime 已为每通道保留同一帧的 8192 个样本。Windows 用现有 `WAVEFORM`（消息 7）从这 8192 点中读取一个 2048 点切片；该只读切片操作不改变 PL 采集窗口。

32 字节请求保持向后兼容：

| 偏移 | 字段 | 类型 | 规则 |
|---:|---|---:|---|
| 0 | `expected_generation` | u64 LE | 0 表示读取最新发布帧 |
| 8 | `max_payload_bytes` | u32 LE | 固定 16384 |
| 12 | `slice_offset` | u32 LE | 0–6144；相对 runtime 8192 点帧起点 |
| 16 | `reserved` | 16 bytes | 固定全 0 |

当 `slice_offset=0` 时，请求与旧客户端完全一致。非零切片仍使用消息 7，ARM 验证偏移范围，并要求 16–31 字节保持为 0。

响应仍使用现有 64 字节头和四路 `int16[2048]`。`header.window_start` 必须等于 PL 采集窗口起点加 `slice_offset`，其余 generation、session、sequence、frame、样本率、CRC 和布局不变。读取是只读操作，不要求 HOST_MANAGED，不停止 50 Hz，也不重建模板。

四路 LNA（CFG2 字段 16）、PGA（字段 18）、VCNTL（字段 19）继续走现有配置事务；只有 HOST_MANAGED、租约有效、硬件安全停止和 UHW2 确认卸载后才能应用，并核对硬件实际回读。恢复测量产生新帧后，Windows 使用消息 7 的波形切片查看调参后的不同回波区段。

若工程师需要改变下一批采集的 PL 起始点，使用 CFG2 字段 99
`capture_window_start`（0..91808，步进 1，offset 304）。该操作与增益一样只通过 ARM
安全写门执行，并强制重新确认 AGC 与模板。Windows 先读取活动 CFG2 值作为候选；A001
无历史配置时才使用 9781 的产品默认值，不能用本地默认值覆盖设备读回值。
