# Windows 发布签名与 USB 驱动说明

## 当前 USB 驱动结论

当前工程设备通过 Microsoft OS compatible-ID 描述符报告 `WINUSB`，Windows 10/11
会自动匹配系统自带、Microsoft 已签名的 `winusb.inf / winusb.sys`。应用安装包已经携带
用户态访问所需的 `libusb-1.0.dll`，不需要、也不应再塞入一个自制内核 `.sys` 或覆盖
系统 WinUSB。

安装器会创建“USB 驱动和连接检查”开始菜单入口。它只读核对：唯一工程
`VID:PID 1D6B:0105`、PnP service=`WinUSB`、configuration/interface 和 bulk endpoint
合同；不会写板卡、安装 INF 或修改设备驱动。

工程 `1D6B:0105` 不是量产 VID/PID。量产身份冻结后必须同步更新设备固件、Windows
身份合同与 HIL，不能用当前工程身份冒充量产发布。

## 用户需要配合的签名材料

当前产品架构只需要给自有应用 EXE、安装器和卸载器做 Authenticode 签名：

1. 以公司法定主体申请公开 CA 签发的 Windows Code Signing 证书（OV 或 EV）；
2. 让证书和私钥可由发布电脑访问，优先放在硬件 Token、云签名服务或 Windows
   证书存储区，而不是把 PFX 和密码放进仓库；
3. 提供证书指纹（thumbprint）和该 CA 的 RFC 3161 时间戳 URL；
4. 硬件 Token 的 PIN、PFX 密码或云签名授权只在发布电脑本地输入，不在聊天、脚本、
   Git 或构建日志中传递。

如果未来取消设备 OS 描述符、需要自定义 INF/内核驱动，才进入另一条更重的流程：
公司需用 EV 证书注册 Microsoft Hardware Dev Center，再走 Microsoft attestation 或
HLK/WHQL 签名。当前 WinUSB 自动绑定方案不需要这一步。

## 发布顺序

先生成 Portable 闭包，再对闭包内自有 EXE 签名：

```powershell
.\scripts\sign_windows_binary.ps1 `
  -File .\dist\UcmConfigStudioNext\UcmConfigStudioNext.exe `
  -CertificateThumbprint '<公司证书指纹>' `
  -TimestampUrl '<CA提供的RFC3161地址>'
```

签名后重新生成 Portable 的 `SHA256SUMS.txt`，再构建安装器。安装器构建脚本会让
Inno Setup 使用同一证书签名 Setup 和 Uninstaller，并在完成后验证签名：

```powershell
.\scripts\build_installer.ps1 `
  -PortableDirectory .\dist\UcmConfigStudioNext `
  -OutputDirectory .\dist `
  -Version 0.1.0-preview.4 `
  -NumericVersion 0.1.4.0 `
  -OutputBaseName QIZHEN_UCM_CONFIG_STUDIO_NEXT_PREVIEW_r4_Setup `
  -CertificateThumbprint '<公司证书指纹>' `
  -TimestampUrl '<CA提供的RFC3161地址>'
```

没有证书参数时仍可生成内部测试安装器，但其 Authenticode 状态会明确显示
`NotSigned`，不得冒充已签名外发版。
