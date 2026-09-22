# ARM revision 9 offline fixtures

Original revision 9 baseline: ARM `c83051cf733126f4765f1df7ad50e3649fabcc4b`. BODY_REFERENCE migration verified 2026-09-09 against ARM checkout based on `50c0795f4c913ed55e7bfaaf1d276bcf904966ea`; exact compiled C source hashes are recorded by generate.ps1.
`UCM_WINDOWS_USB_GOLDEN_V1.json` is an unchanged copy of the ARM handoff request vectors.
`device_model.json` and `input_policy.json` use the original ARM unit-test golden JSON and canonical identities.

The `.bin` response payloads are synthetic states produced locally by the original ARM C encoders and checked by ARM validators. They are **not board captures or evidence of physical calibration**. Regenerate with `generate.ps1 -ArmSource <ARM checkout> -OutputDirectory <local build directory>`. The generator uses GCC LTO to exclude unused Linux service functions; it never starts a VM, opens a port, or accesses hardware.

- `capabilities.bin`: original ARM capabilities initializer, revision 9.
- `device_model_{state,active,startup}.bin`, `input_policy_{state,active,startup}.bin`: boot 32, authority 40, session 48, response request 56; active/startup generation 7, startup commit 8. Caller simulations must reseal the inner CRC when rebinding request IDs. Payload SHA and canonical identity are verified by ARM C.
- `publication_valid.bin`, `publication_invalid.bin`, `input_status.bin`: publisher 16, time 1000, session 32, sequence 40, frame 48, request 56. Both result fixtures pair with the sidecar by all six fields. The valid fixture exercises numeric projection only, with synthetic forces.
- `publication_maintenance.bin`: ARM `publication_invalid` output, reason 10 and frame counter zero. It intentionally has no matching input sidecar; it must not borrow prior input or values.
- `log_catalog.bin`, `log_chunk.bin`: one immutable 680-byte synthetic segment. The chunk data are zeros; this checks transfer framing/identity only, not the URS2 record parser. The identity CRC is not a whole-file cryptographic digest. Chunk header CRC covers 64 bytes; outer USB payload CRC must protect the full response.

The Qt decoder separately tests malformed CRC, repaired-CRC reserved corruption, each of the six pairing identities, canonical/payload digest mismatch, duplicate and escaped-duplicate JSON keys, invalid UTF-8, and out-of-range JSON generations. Generations above `INT64_MAX` are explicitly rejected at the Qt JSON boundary instead of rounded.

These fixtures are in an otherwise ignored directory; stage these exact fixture paths explicitly when committing the tests.

`SHA256SUMS.txt` records each response binary, golden JSON, and generator source/script SHA-256. The fixture generator was compiled and run locally with MSYS2 UCRT GCC 16.1.0:

```powershell
./generate.ps1 -ArmSource D:/UCM2_RC13_WORKTREES/ucm-arm-product-runtime-r1 -OutputDirectory D:/UCM2_RC13_BUILD_EVIDENCE/windows_usb_w1_codec
```

The schema 2 Qt 6 codec executable passed 196 checks. Build and test logs are retained in that output directory. Golden payload equality and semantic mutation tests are the validation evidence; the generated executable hash is not a board deployment identity.

## BODY_REFERENCE migration

DeviceModel JSON now has schema_version 2, geometry_model BODY_REFERENCE_V1, M_ref=500 and C_ref=1775 in the original ARM synthetic test fixture (these are NOT product defaults). DCI2 identity is 512 bytes; expected SHA-256 is 42f9022dc3e0fd80358ce9148896d7aef64850e928495128d0cb6ff1d5b4d5b3. Model state/document policy schema is 2; InputPolicy remains schema 1. The seven original outer request vectors are unchanged.

Pass -WindowsOperationTests <ucm_next_usb_product_operations_v9_tests.exe> to generate.ps1 to emit three Windows model requests and validate them using the real ARM request decoder, byte-identical re-encoder and DCI2 identity function. Cases: original ARM model, unqualified candidate references (1075/2000, phi .46), and generation 9007199254740993. Crosscheck evidence goes under OutputDirectory/windows-crosscheck. All requests remain local files and are never transmitted.
