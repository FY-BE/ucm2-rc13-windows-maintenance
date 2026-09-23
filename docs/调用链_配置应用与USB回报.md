# Windows ConfigStudio：配置应用与 USB 回报调用链

> 基线：`a704b349a5c8eccad7e82ad0d5b0fffbbaa19799`。本文是源码静态调用链学习材料，不把静态证据写成 USB 现场已验证。

## 一图读懂

```text
main.cpp
  → DashboardBridge backend
  → QQmlContext::setContextProperty("backend")
  → Main.qml / ProductOperationsPanel.qml
  → Q_INVOKABLE applyPreparedConfiguration()
  → configurationApplyRequested
  → DashboardPollWorker::applyPreparedConfiguration()
  → ConfigurationSession::commitRam()
  → ConfigurationSession::confirmReadback()
  → configurationApplied
  → DashboardBridge::finishPreparedConfiguration()
  → QML 读取 configurationStatus / configurationIdentity
```

## 1. 应用入口与 QML 暴露

`main.cpp::main` 创建 `DashboardBridge backend`，随后以 context property 名 `backend` 暴露给 QML，再加载 `UcmConfigStudio.Next/Main`。因此 QML 中的 `backend.*` 不是独立服务，而是同一个 C++ bridge 实例。

确认到的源码边：

- `main.cpp::main` → `DashboardBridge` 构造函数
- `main.cpp::main` → `engine.rootContext()->setContextProperty("backend", &backend)`
- `Main.qml::onSelectedPageChanged` → `backend.setActivePage(...)`
- `ProductOperationsPanel.qml` → `backend.applyRuntimeConfiguration(...)` / `backend.applyRuntimeBurst(...)`

## 2. 配置应用路径

配置不是点击后直接写设备，而是分阶段进行：

1. QML 触发 `DashboardBridge::applyPreparedConfiguration()`。
2. Bridge 发出 `configurationApplyRequested`。
3. 构造函数中的 Qt queued connection 把信号送到 `DashboardPollWorker::applyPreparedConfiguration()`。
4. worker 在 `m_sessionMutex` 保护下调用 `m_session->commitRam()`。
5. commit 成功后调用 `m_session->confirmReadback()`。
6. 成功回读后根据 `m_session->active()` 生成 `activeIdentity`。
7. worker 发出 `configurationApplied`，Bridge 接收并更新 QML 可见状态。

这条链路说明“有回读确认逻辑”，但仍不能单凭源码证明当前设备会接受该写入；真实 USB 设备、固件版本和现场回读仍是更高层证据。

## 3. 运行参数 USB 路径

运行参数走另一条信号链：

- `DashboardBridge::applyRuntimeConfiguration(bool authorized)` 先检查授权、可写条件和 busy 状态；通过后发出 `runtimeParameterOperationRequested`。
- 构造函数把该信号 queued-connect 到 `DashboardPollWorker` 的运行参数处理槽。
- worker 完成后发出 `runtimeParameterOperationCompleted`。
- Bridge 的完成处理更新 `runtimeConfigurationStatus`、busy 状态和回读信息，QML 通过对应 `Q_PROPERTY` 读取。

因此“运行参数应用”和“配置草稿应用”不是同一个入口，修改其中一个不能默认覆盖另一个。

## 4. 当前可确认与不可确认

### 已由源码调用链确认

- `backend` 确实从 C++ 暴露到 QML。
- 配置应用信号存在 emit/connect/接收闭环。
- worker 确实调用 commit 后再做 readback。
- QML 可读取配置状态、身份、变化和错误列表。

### 尚不能由源码单独确认

- 当前连接的设备是否为目标 ARM 设备。
- 设备端驱动、固件和协议版本是否与本基线匹配。
- 真实 USB 写入是否成功、是否持久化到开机配置。
- 现场产品流程是否会走到这个页面和按钮。

## 5. 后续维护入口

修改配置应用行为时，按以下顺序核对：

1. QML 触发点和授权条件；
2. `DashboardBridge` 的状态门控与信号；
3. `DashboardPollWorker` 的锁、transport 调用和回读；
4. `ConfigurationSession` 的 commit/confirm 语义；
5. 对应离线测试、构建产物和实际设备证据。

不要只改 QML 文案或只改 worker 分支后就宣称整条配置链已修复。
