#include "product_log_download.h"
#include <QCoreApplication>
#include <QTextStream>
using namespace ucm;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *name) { if (!ok) { ++failures; QTextStream(stderr) << name << '\n'; } };
    DeviceLogSource source;
    source.sourceId=1; source.available=true; source.totalBytes=68000;
    source.snapshotId=UINT64_C(0xfedcba9876543210); source.fileIdentityCrc32=42;
    source.name="result-20260909-001.urs2";
    ProductLogDownload download;
    check(download.begin(source) && download.nextBytes()==65536, "begin bounded download");
    DeviceLogChunkResult chunk;
    chunk.success=true; chunk.sourceId=1; chunk.snapshotId=source.snapshotId;
    chunk.fileIdentityCrc32=42; chunk.totalBytes=68000; chunk.more=true;
    chunk.data=QByteArray(65536,'x');
    check(download.accept(chunk) && download.offset()==65536 && download.data().isEmpty(), "partial not exported");
    chunk.offset=65536; chunk.data=QByteArray(2464,'y'); chunk.more=false;
    check(download.accept(chunk) && download.complete() && download.data().size()==68000, "complete bytes");
    check(download.localSha256()==QCryptographicHash::hash(QByteArray(65536,'x')+QByteArray(2464,'y'),QCryptographicHash::Sha256), "local digest");
    check(download.begin(source), "new download resets");
    chunk.offset=0; chunk.more=true; chunk.data=QByteArray(65536,'x');
    check(download.accept(chunk), "first chunk");
    chunk.offset=65536; chunk.snapshotId++;
    check(!download.accept(chunk) && download.data().isEmpty() && !download.active() && download.offset()==0, "conflict destroys partial bytes");
    source.name="../result-20260909-001.urs2";
    check(!download.begin(source), "reject path injection");
    source.name="result-20260909-001.urs2"; source.totalBytes=16777217;
    check(!download.begin(source), "reject oversized file");
    source.totalBytes=0; check(download.begin(source), "empty file needs terminal read");
    check(!download.complete() && download.localSha256().isEmpty(), "empty file not prematurely complete");
    chunk={};chunk.success=true;chunk.sourceId=1;chunk.snapshotId=source.snapshotId;chunk.fileIdentityCrc32=42;
    check(download.accept(chunk) && download.complete(), "empty terminal identity confirmed");
    source.kind=2; source.name="result-20260909-1205.url3"; source.totalBytes=512;
    check(download.begin(source) && download.nextBytes()==512, "native R2S log accepted");
    source.name="result-20260909-001.urs2";
    check(!download.begin(source), "native log cannot masquerade as URS2");
    source.name="result-20260909-1205.url3"; source.totalBytes=513;
    check(!download.begin(source), "native log must contain complete records");
    return failures ? 1 : 0;
}
