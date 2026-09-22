#!/usr/bin/env python3
"""Generate canonical revision-9 parameter-catalog and config vectors."""
from __future__ import annotations
import hashlib, json, struct, zlib
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "arm_product_v9_parameter_config"
CFG_BYTES, RECEIPT_BYTES = 384, 768
READ, WRITE, ASSET, RESTART, FROZEN = 1, 2, 8, 16, 64
RANGE, STEP, ENUM, CONTEXT = 1, 2, 4, 8
U32, I32, ENUM_KIND, BOOL = 1, 2, 4, 5
GLOBAL, PER_ROD, ASSET_SCOPE = 1, 2, 3

def p16(b,o,v): struct.pack_into("<H",b,o,v)
def p32(b,o,v): struct.pack_into("<I",b,o,v & 0xffffffff)
def p64(b,o,v): struct.pack_into("<Q",b,o,v)

def descriptor(field, group, kind=U32, scope=GLOBAL, access=READ|WRITE|RESTART,
               minimum=0.0, maximum=1_000_000.0, step=0.0, enum_mask=0,
               constraints=RANGE):
    if step: constraints |= STEP
    if enum_mask: constraints |= ENUM
    return struct.pack("<IHBBIIdddQ", field, group, kind, scope, access,
                       constraints, minimum, maximum, step, enum_mask)

def catalog() -> bytes:
    rows=[]
    def asset(f, group=6, **kw):
        rows.append(descriptor(f,group,scope=ASSET_SCOPE,
                               access=READ|WRITE|ASSET,**kw))
    def runtime(f, group=8, **kw): rows.append(descriptor(f,group,**kw))
    def readonly(f, group=8, **kw):
        rows.append(descriptor(f,group,access=READ,**kw))
    def frozen(f, group=8, **kw):
        rows.append(descriptor(f,group,kind=BOOL,access=READ|FROZEN,
                               minimum=1,maximum=1,**kw))
    # Canonical wire order is frozen independently of numeric field-ID order.
    asset(4,kind=ENUM_KIND,minimum=1,maximum=2,enum_mask=0x6)
    asset(1,minimum=1,maximum=5839); asset(2,minimum=0,maximum=5839)
    asset(3,group=1,minimum=1,maximum=10000)
    for f in (48,49,50): asset(f,minimum=0,maximum=10000)
    for f in (51,52,53): asset(f,minimum=0,maximum=100_000_000)
    for f in (54,55): asset(f,minimum=0,maximum=1)
    asset(56,kind=I32,minimum=-1_000_000,maximum=1_000_000)
    asset(57,minimum=1,maximum=1_000_000)
    runtime(16,group=4,scope=PER_ROD,minimum=12,maximum=24,
            enum_mask=(1<<12)|(1<<18)|(1<<24))
    readonly(17,group=4,scope=PER_ROD,minimum=0,maximum=0)
    runtime(18,group=3,kind=ENUM_KIND,minimum=24,maximum=30,
            enum_mask=(1<<24)|(1<<30))
    runtime(19,group=3,minimum=2621,maximum=39322,step=1)
    readonly(20,group=3,minimum=0,maximum=0)
    runtime(64,group=7,minimum=0,maximum=1_000_000)
    runtime(65,group=7,minimum=1_000_000,maximum=10_000_000)
    runtime(66,group=7,kind=I32,minimum=-100_000,maximum=100_000)
    runtime(80,group=7,minimum=1,maximum=1000,step=1)
    runtime(81,group=7,minimum=0,maximum=2_147_483_647,step=1)
    runtime(82,group=7,minimum=0,maximum=1_000_000)
    runtime(83,group=7,minimum=1,maximum=1000,step=1)
    runtime(84,group=7,minimum=0,maximum=2_147_483_647,step=1)
    runtime(96,group=9,minimum=50,maximum=50)
    runtime(97,minimum=50,maximum=250,step=5,constraints=RANGE|CONTEXT)
    runtime(98,minimum=1,maximum=8,step=1,constraints=RANGE|CONTEXT)
    runtime(112,kind=BOOL,minimum=0,maximum=1,step=1)
    runtime(116,minimum=1,maximum=998); runtime(117,minimum=2,maximum=999)
    runtime(118,minimum=3,maximum=1000)
    runtime(122,minimum=0,maximum=1_000_000_000)
    runtime(123,minimum=0,maximum=1_000_000); runtime(124,minimum=0,maximum=1_000_000)
    for f in (125,126,127): runtime(f,minimum=1,maximum=10000,step=1)
    runtime(144,minimum=2621,maximum=39322); runtime(145,minimum=2621,maximum=39322)
    for f in (146,147,148): runtime(f,minimum=1,maximum=65535,step=1)
    runtime(149,minimum=1,maximum=3); runtime(150,minimum=1,maximum=7)
    runtime(151,minimum=50,maximum=250,step=5); runtime(152,minimum=50,maximum=250,step=5)
    runtime(153,minimum=5,maximum=5); runtime(154,minimum=1,maximum=8,step=1)
    runtime(155,minimum=1,maximum=8,step=1)
    frozen(113); frozen(114); frozen(115)
    readonly(128,scope=PER_ROD,minimum=12,maximum=24)
    readonly(129,minimum=24,maximum=30); readonly(130,minimum=2621,maximum=39322)
    readonly(131,minimum=0,maximum=42); readonly(132,minimum=50,maximum=250)
    readonly(133,minimum=1,maximum=8); readonly(134,minimum=0,maximum=16)
    readonly(135,kind=BOOL,minimum=0,maximum=1); readonly(136,group=9,minimum=50,maximum=50)
    runtime(99,group=9,minimum=0,maximum=91808,step=1,
            constraints=RANGE|CONTEXT)
    assert len(rows)==65 and all(len(row)==48 for row in rows)
    return b"".join(rows)

CATALOG = catalog()
CATALOG_CRC = zlib.crc32(CATALOG)

def config(operation: int) -> bytes:
    b=bytearray(CFG_BYTES)
    p32(b,0,0x32474643);p16(b,4,2);p16(b,6,CFG_BYTES);p32(b,16,operation);p32(b,20,4)
    p64(b,24,0x1020304050607080);p64(b,32,7);p64(b,40,7 if operation==2 else 8)
    p64(b,48,0x7ff);p64(b,56,0 if operation==2 else 0x1e9);p32(b,64,CATALOG_CRC);p32(b,68,0x87654321)
    fields={112:1,116:50,120:1000,124:280,128:5900,132:50,136:1,140:24,144:21845,
      148:0,152:900000,156:1100000,160:12000,164:20,168:1000,172:200000,176:20000,180:5,
      184:15,188:600,192:780,196:900,200:4000,204:950000,208:1000,212:5,216:4,220:48,
      224:2621,228:39322,232:375,236:749,240:1498,244:50,248:250,252:5,256:1,260:8,
      264:18,268:18,272:18,276:18,280:0,284:0,288:0,292:0,296:3,300:7,
      304:9781}
    for o,v in fields.items(): p32(b,o,v)
    h=bytearray(b);h[8:12]=b"\0"*4;h[72:104]=b"\0"*32;b[72:104]=hashlib.sha256(h).digest()
    c=bytearray(b);c[8:12]=b"\0"*4;p32(b,8,zlib.crc32(c));return bytes(b)

def query(kind: int, transaction: int=0) -> bytes:
    b=bytearray(32);p32(b,0,0x32514355);p16(b,4,2);p16(b,6,32);p32(b,12,kind);p64(b,16,transaction)
    p32(b,8,zlib.crc32(b));return bytes(b)

def receipt(active: bytes) -> bytes:
    b=bytearray(RECEIPT_BYTES);p32(b,0,0x32524355);p16(b,4,2);p16(b,6,RECEIPT_BYTES)
    p32(b,12,2);p32(b,24,1);p32(b,28,48);p64(b,32,0x1020304050607080)
    p64(b,40,7);p64(b,48,8);p64(b,56,8);p64(b,64,0x1e9);b[80:464]=active
    p32(b,656,0x32574855);p16(b,660,2);p16(b,662,112);p32(b,664,1)
    p32(b,668,2);p32(b,672,50);p32(b,676,50);p32(b,680,1);p32(b,684,24)
    p32(b,688,21845);p32(b,692,0)
    for o in (696,700,704,708):p32(b,o,18)
    p32(b,728,0xf);p64(b,736,123456789);p64(b,744,42);p64(b,752,8)
    p32(b,8,zlib.crc32(b));return bytes(b)

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    vectors={"parameter_catalog_descriptors.bin":CATALOG,"config_apply.bin":config(1),
      "config_save_startup.bin":config(2),"config_validate.bin":config(3),
      "query_active.bin":query(2),"query_transaction.bin":query(4,0x1020304050607080)}
    vectors["receipt_applied.bin"]=receipt(vectors["config_apply.bin"])
    for name,data in vectors.items():(OUT/name).write_bytes(data)
    manifest={"schema":"UCM_USB_REVISION9_PARAMETER_CONFIG_V1","wire_revision":9,"byte_order":"little",
      "messages":{"PARAMETER_CATALOG":17,"CONFIG_STATE":18,"CONFIG_APPLY":19},
      "objects":{"CATALOG_DESCRIPTORS":{"entries":65,"descriptor_bytes":48,"crc32":f"0x{CATALOG_CRC:08x}"},
                 "CFG2":{"schema":2,"revision":4,"bytes":384},"UCQ2":{"schema":2,"bytes":32},
                 "UCR2":{"schema":2,"bytes":768,"field_results":48},
                 "UHW2":{"schema":2,"bytes":112,"offset_in_ucr2":656}},
      "operations":{"APPLY_ACTIVE":1,"SAVE_STARTUP":2,"VALIDATE_ONLY":3},
      "query_kinds":{"LATEST":1,"ACTIVE":2,"STARTUP":3,"TRANSACTION":4},
      "sha256":{n:hashlib.sha256(d).hexdigest() for n,d in vectors.items()}}
    (OUT/"manifest.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    (OUT/"SHA256SUMS.txt").write_text("".join(f"{manifest['sha256'][n]}  {n}\n" for n in sorted(vectors)),encoding="ascii")
if __name__=="__main__":main()
