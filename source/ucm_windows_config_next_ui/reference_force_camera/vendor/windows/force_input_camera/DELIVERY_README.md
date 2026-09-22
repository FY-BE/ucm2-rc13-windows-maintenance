# UCM2 交付包相机算法说明

本目录从用户指定的只读权威目录原样迁入：

```text
G:\Ultra ClampMonitor\Project refactoring code\windows_calibration\force_input_camera
```

识别数学、`config.json` 和 `rois.json` 未改。`SOURCE_PROVENANCE.json` 记录源路径、
历史冻结 commit 和逐文件 SHA-256；项目测试会拒绝未登记的算法漂移。

这套工具只用于人工/离线标定或复核：`S1/S2/S3/S4` 是四根拉杆的外部力读数，
单位 kN。它不得进入正式测试运行态的控制闭环；正式运行只加载已标定的
profile（包含固定 K0/b）。

**操作者请先读** [`WORKFLOW_CN.md`](./WORKFLOW_CN.md)（中文可复现步骤、安全落盘、禁止项）。
本机照片/队列/CSV/会话请写到 [`local/`](./local/)（默认不进 Git），不要覆盖包内冻结的 `rois.json`。

交付包命令路径为 `windows.force_input_camera` ，例如：

```powershell
python -m windows.force_input_camera.recognize_queue --queue-dir windows\force_input_camera\local\frame_queue --config windows\force_input_camera\config.json --rois windows\force_input_camera\rois.json --fields S1,S2,S3,S4 --min-confidence 0.9 --output windows\force_input_camera\local\values.csv
```

建议通过上层人工复核入口运行，它会同时保存图片 hash、原算法结果、置信度和人工确认：

```powershell
python -m windows.ucm2_host.camera_review --image windows\force_input_camera\local\panel.bmp --output windows\force_input_camera\local\sessions --operator operator-a --confirm-kN "10,20,30,40"
```

相机直采（需先安装 Hikrobot MVS SDK）：

```powershell
python -m windows.ucm2_host.camera_review --capture --output windows\force_input_camera\local\sessions --operator operator-a
```

无论原算法置信度高低，都不会自动晋级为人工确认值。若使用 `--non-interactive`
且不提供 `--confirm-kN`，会话会明确以 `degraded` 收口，不会伪报成功。

测试：

```powershell
python -m pytest windows\force_input_camera\tests tests\test_force_camera_source_provenance.py tests\test_windows_production_chain.py -q
```

当前已验证算法及适配单测；当前机器上 MVS wrapper 可加载，但枚举到的相机数量为 0，
因此真实相机 HIL 仍是待完成项。
