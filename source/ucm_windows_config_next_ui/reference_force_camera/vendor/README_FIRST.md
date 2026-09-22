# 海康摄像头段码屏识别与标定工具交付说明

本包是独立的 Windows/Python 工具包，用于海康（Hikrobot MVS）工业相机采图、固定位置段码屏识别、四路 ROI 标定、离线队列处理、CSV 输出和人工复核留档。它与 UCM2 FPGA/ARM 最小交付包分开，不会改变 FPGA 或 ARM 产物。

## 已交付内容

- `windows/force_input_camera/`：海康相机适配、单帧/队列采集、七段码识别、ROI 标定、置信度判断、CSV 输出和单元测试。
- `windows/ucm2_host/`：`camera_review` 人工复核及会话证据入口。
- `tests/`：来源一致性及人工复核链测试。
- `SOURCE_FROM_GIT_c995d5c.zip`：从冻结 Git 提交直接导出的原始源码归档。
- `PACKAGE_MANIFEST.json`、`SHA256SUMS.txt`：生成后的逐文件身份清单。

## 源码身份

- Git commit：`c995d5c07bd8aca77f1137718bcf917535388964`
- Git tree：`6ef6fa80722b757a90cecbfecc782ae7491cb9fb`
- 模块历史冻结来源：见 `windows/force_input_camera/SOURCE_PROVENANCE.json`

## 环境准备

```powershell
cd <本交付包目录>
python -m pip install -r windows\force_input_camera\requirements.txt
```

相机直采还需要安装 Hikrobot MVS SDK，并把 SDK 的 `MvImport` 目录提供给程序：

```powershell
set HIKROBOT_MVS_PYTHON=C:\Program Files (x86)\MVS\Development\Samples\Python\MvImport
```

## 推荐使用顺序

1. 单帧采图：

```powershell
python -m windows.force_input_camera.capture_once --output windows\force_input_camera\local\panel.bmp --timeout-ms 3000
```

2. 第一次安装或镜头位置变化后标定四个显示区域：

```powershell
python -m windows.force_input_camera.calibrate_rois windows\force_input_camera\local\panel.bmp --config windows\force_input_camera\config.json --out windows\force_input_camera\local\rois.work.json
```

3. 识别已有图片：

```powershell
python -m windows.force_input_camera.seven_segment_recognize windows\force_input_camera\local\panel.bmp --config windows\force_input_camera\config.json --rois windows\force_input_camera\local\rois.work.json --output windows\force_input_camera\local\output_live
```

4. 推荐使用人工复核入口保存图片哈希、识别候选值和人工确认值：

```powershell
python -m windows.ucm2_host.camera_review --image windows\force_input_camera\local\panel.bmp --output windows\force_input_camera\local\sessions --operator operator-a --confirm-kN "10,20,30,40"
```

更完整的采集队列、识别参数和安全落盘说明见 `windows/force_input_camera/WORKFLOW_CN.md`。

## 本包验证记录

- Python 3.14.2。
- 段码识别、图像 IO、离线队列、输出、置信度、海康 SDK 适配、源码来源和人工复核链共 29 个定向测试节点均取得 PASS。
- 所有交付 Python 文件均完成内存语法编译检查。
- 本次交付信用只覆盖上述源码与离线/接口行为；不把未在本次运行中执行的现场相机采集或现场识别精度写成已验证结论。

