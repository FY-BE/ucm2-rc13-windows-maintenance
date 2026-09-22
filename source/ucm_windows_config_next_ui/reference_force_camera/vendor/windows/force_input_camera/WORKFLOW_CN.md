# 网口相机标定 + 离线识别：操作流程（中文）

面向操作者。本流程只做**拍照、认数字、人工确认证据**，不写正式 runtime 硬件，也不会自动批准标定。

正确模块名（交付包）：

```text
windows.force_input_camera
windows.ucm2_host.camera_review
```

> 若你在旧文档里看到 `windows_calibration.force_input_camera`，那是迁入前的旧路径，在本仓请一律改用上面的名字。

---

## 0. 先分清三样东西

| 名称 | 是什么 | 能不能随便改 |
|---|---|---|
| 包内 `config.json` / `rois.json` | 冻结的认字规则和数字框位置 | **不要直接覆盖**；改了会破坏指纹校验 |
| `local/` | 本机工作区：照片、队列、CSV、自己画的框 | 可以随便写；默认不进 Git |
| `camera_review` | 人工复核入口 | 程序只给候选；**必须人手确认**四杆力 |

`S1/S2/S3/S4` = 四根拉杆外力，单位 **kN**。

---

## 1. 环境准备

在仓库根目录（含 `windows` 文件夹的那一层）打开终端：

```powershell
pip install -r windows\force_input_camera\requirements.txt
```

还需要安装海康机器人 **MVS**。默认会在常见安装路径找 SDK；若装在别处：

```powershell
set HIKROBOT_MVS_PYTHON=C:\Program Files (x86)\MVS\Development\Samples\Python\MvImport
```

跑 Python 采集前，请先**关掉 MVS 客户端**，否则相机常报访问拒绝（如 `0x80000203`）。

没有相机时：仍可对已有图片做识别 / `camera_review`；本机枚举相机数为 0 时，采集步骤会失败，属预期。

---

## 2. 推荐主流程（先拍后认）

工作文件建议都放在包内 `local\`，避免和冻结文件搅在一起。

### 步骤 A：连拍入队列

```powershell
python -m windows.force_input_camera.capture_queue --hz 35 --frames 80 --queue-dir windows\force_input_camera\local\frame_queue
```

- `--hz 35`：常用推荐；再高往往顶到约 39 Hz，收益有限。
- 每张图文件名里带帧号和毫秒时间戳（电脑写盘时间，不是硬件曝光时间）。

### 步骤 B：离线认数字

务必显式指向**包内冻结配置**（或你自己的工作副本），不要依赖“当前目录碰巧有同名文件”：

```powershell
python -m windows.force_input_camera.recognize_queue ^
  --queue-dir windows\force_input_camera\local\frame_queue ^
  --config windows\force_input_camera\config.json ^
  --rois windows\force_input_camera\rois.json ^
  --fields S1,S2,S3,S4 ^
  --min-confidence 0.9 ^
  --output windows\force_input_camera\local\values.csv
```

行为摘要：

- 置信度够高的行写入 CSV；默认处理后删图。
- 坏图进 `frame_queue\failed\`。
- 调参时需要留下低置信度图：加 `--keep-rejected`。
- 边拍边延迟识别：再加 `--watch`（仍建议输出写到 `local\`）。

### 步骤 C：人工复核（证据会话）

把候选值和人工确认一起存档；**不会**写入正式控制闭环：

```powershell
python -m windows.ucm2_host.camera_review ^
  --image windows\force_input_camera\local\panel.bmp ^
  --output windows\force_input_camera\local\sessions ^
  --operator operator-a ^
  --confirm-kN "10,20,30,40"
```

或现场拍一张再复核（需相机）：

```powershell
python -m windows.ucm2_host.camera_review ^
  --capture ^
  --output windows\force_input_camera\local\sessions ^
  --operator operator-a
```

说明：

- 不提供 `--confirm-kN` 且用 `--non-interactive` 时，会话以 `degraded` 结束，**不会假装成功**。
- 无论算法置信度多高，都**不会**自动变成“已确认标定值”。

---

## 3. 单张图调试（可选）

拍一张：

```powershell
python -m windows.force_input_camera.capture_once --output windows\force_input_camera\local\panel.bmp --timeout-ms 3000
```

对已有图识别（可加 `--debug` 看中间图）：

```powershell
python -m windows.force_input_camera.seven_segment_recognize windows\force_input_camera\local\panel.bmp ^
  --config windows\force_input_camera\config.json ^
  --rois windows\force_input_camera\rois.json ^
  --output windows\force_input_camera\local\output_live ^
  --debug
```

Debug 排查顺序：红光 mask → 数字切分 → 七段框 → 小数点框。

---

## 4. 需要重画数字框时（高风险，单独约定）

只在相机位置/画面变化、旧 `rois.json` 对不上时才做。

**禁止**默认覆盖包内 `windows\force_input_camera\rois.json`。

正确做法：输出到工作副本：

```powershell
python -m windows.force_input_camera.calibrate_rois windows\force_input_camera\local\panel.bmp ^
  --config windows\force_input_camera\config.json ^
  --out windows\force_input_camera\local\rois.work.json
```

按顺序框：`S1` → `S2` → `S3` → `S4` → `Total` → `Other`。只框数字窗口，不要框标签和外壳。

之后识别时把 `--rois` 改成这份工作副本。若将来要替换仓库冻结 ROI，必须另开明确变更：更新文件 + 更新 `SOURCE_PROVENANCE.json` 指纹 + 通过 provenance 测试；本流程默认不做这一步。

---

## 5. 明确禁止

- 把 OCR / CSV / `camera_review` 候选值自动写进正式 runtime、寄存器、或自动批准 CalibrationBundle。
- 在未授权情况下覆盖包内冻结的 `config.json` / `rois.json` / 识别算法源文件。
- 把本工具接到 `production_cli` / formal runtime 控制闭环。

---

## 6. 自检命令

在仓库根目录：

```powershell
python -m pytest windows\force_input_camera\tests tests\test_force_camera_source_provenance.py tests\test_windows_production_chain.py -q
```

`test_force_camera_source_provenance` 会检查冻结算法文件是否被改动。

---

## 7. 和旧英文 README / FREEZE 的关系

- `README.md`、`FREEZE_v0.1.md`：迁入时的历史说明（含旧模块名），指纹锁定，日常操作**以本文 + `DELIVERY_README.md` 为准**。
- `DELIVERY_README.md`：交付边界与人工复核入口摘要。
- 本文件：操作者可复现步骤与安全落盘约定。
