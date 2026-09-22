# ARM SYS-D021 CRS1 locked fixtures

状态：`FINAL_ARM_SCHEMA6_BYTE_IDENTICAL_REBIND / PRODUCT_PL_EXACT_CANDIDATES / DIAGNOSTIC_PL_DISPLAY_ONLY / LEGACY_SCHEMA5_REJECTION_AUDIT`

当前来源是 ARM commit
`e440037f415c1d2ae76f59227596ca9867bc45cb`，tree
`dd837f231e8b839da47feb3ed8a5b711d2c2fcbe`：

`src/linux/ucm_candidate_r0/tests/fixtures/ucm_d020_d021_r1`

Windows 测试只使用本目录锁定副本，不依赖另一 worktree 的运行时路径。
`contract.json` 是 e440037 的原样副本；四个 CRS1 `.bin` 自 f478 起字节未变。
`arm_schema6_rebind_audit.json` 记录 212a963→e440037 的逐对象核对：两个schema源文件、
descriptor、contract和全部15个`.bin`均为同一Git blob/字节；生成器只增加跨平台文本
CRLF规范化容忍，未改变生成结果。因此本轮是来源身份重绑，不是wire或descriptor变更。
`product_descriptor_schema_00010006.hex` 是当前 schema 6 的精确 256-byte descriptor；
`legacy_product_descriptor_schema_00010005.hex` 取自 parent c14f7cd，只用于双向拒绝与
回滚审计，绝不是当前可接受输入。`legacy_schema_00010005_audit.json` 保留被替代的D020
向量SHA，且显式声明当前Windows不得接受。`DESCRIPTOR_DECODED_SHA256SUMS`记录解码后字节身份。
`pl_runtime_identities.json` 锁定产品PL `984b863` 的50m14/65m14两个候选位流元组，以及
独立安全诊断PL `4ab5fdb` 的50m14无TX/无HV元组；后者只能进入诊断证据，永不取得产品ready。
提交前须在 ARM tree 运行独立 Python `generate_fixtures.py --check`，并逐一核对合同全部
15 个向量长度与 SHA-256。

`ORIGIN_SHA256SUMS` 记录 ARM 原始字节及两份跨层身份审计对象的 SHA-256；Windows提交中的
Git blob身份由该提交树锁定。fixture 中的 500 ms 是 `TEST_ONLY`，不能写成产品阈值或
产品 allowlist。复合对象最多授予 `RUNTIME_CHAIN_READY`，不授予 formal/force 信用。
Windows只消费外置verifier给出的已签名claims，不增加`open/ioctl/mmap`驱动路径，也不成为
硬件或算法真源。本目录不证明live USB publisher、产品切换、板级资格、installer或实机通过。
