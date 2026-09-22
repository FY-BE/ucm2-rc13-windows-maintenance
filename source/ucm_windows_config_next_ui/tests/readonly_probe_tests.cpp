#include "readonly_probe.h"
#include "usb_wire_v1.h"
#include <QCoreApplication>
#include <QTextStream>
class Sink final : public ucm::LibusbBackend {
public:
    int writes=0; bool online=true;
    ucm::UsbOpenResult open() override { online=true; return {true,{}, {}}; }
    void close() override {online=false;}
    bool isOpen() const override {return online;}
    ucm::UsbTransferResult writeAll(const QByteArray &b,unsigned) override {++writes; return {ucm::UsbTransferStatus::Success,b.size(),{}, {}};}
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); int failed=0;
    auto expect=[&](bool ok,const char *message){if(!ok){++failed;QTextStream(stderr)<<message<<'\n';}};
    const QList<int> reads {1,7,16,17,20,24,25,26,30,31,37,38,39,40,41,45,48};
    for(int type=0;type<256;++type) {
        auto sink=std::make_unique<Sink>(); auto *view=sink.get();
        ucm::probe::ReadOnlyBackend guarded(std::move(sink)); QString error;
        const auto request=ucm::encodeUsbFrameV1(static_cast<ucm::UsbMessageTypeV1>(type),0,1,0,{},&error);
        const bool ok=guarded.writeAll(request,100).success();
        expect(ok==reads.contains(type),"only enumerated read message IDs may pass");
        expect(view->writes==(reads.contains(type)?1:0),"denied request must not reach USB backend");
    }
    auto sink=std::make_unique<Sink>(); auto *view=sink.get();
    ucm::probe::ReadOnlyBackend guarded(std::move(sink)); QString error;
    const auto query=ucm::encodeUsbFrameV1(static_cast<ucm::UsbMessageTypeV1>(24),0,1,0,QByteArray(8,0),&error);
    expect(guarded.writeAll(query.left(40),100).success() && guarded.writeAll(query.mid(40),100).success() && view->writes==2,"preserve header/body transport boundaries");
    auto bad=ucm::encodeUsbFrameV1(static_cast<ucm::UsbMessageTypeV1>(30),0,2,0,{},&error); bad[36]^=1;
    expect(!guarded.writeAll(bad,100).success() && view->writes==2 && !guarded.isOpen(),"invalid header closes without forwarding");
    ucm::probe::Statistics stats; stats.connection(true); stats.connection(true); stats.connection(false); stats.connection(false); stats.connection(true);
    ucm::TelemetrySnapshot s; s.success=true; s.formalForceValid=true;
    s.lowLoadBiasInvalid=true; s.biasValidMinTotalForceN=20000;
    stats.sample(s,0,12); s.formalForceValid=false; s.lowLoadBiasInvalid=false; s.primaryReasonCode=9; stats.sample(s,100,25); s.success=false; s.primaryReasonCode=0; stats.sample(s,400,350);
    const auto result=stats.report(true,500);
    expect(result["simulation"].toBool() && result["formal_valid_samples"].toString()=="1" && result["paired_samples"].toString()=="2","invalid paired result distinct from unavailable pair");
    expect(result["connects"].toString()=="2" && result["disconnects"].toString()=="1","count transitions not each offline sample");
    expect(result["request_max_ms"].toInt()==350 && result["observation_max_gap_ms"].toInt()==300,"host request time and inter-observation gaps remain distinct");
    expect(result["low_load_bias_invalid_samples"].toString()=="1" && result["imbalance_available_samples"].toString()=="0","low-load and imbalance availability counted separately");
    expect(result["bias_valid_min_total_force_n_counts"].toObject()["20000"].toString()=="2","bias gate value preserved for every paired diagnostic sample");
    expect(result["arm_reason_counts"].toObject()["9"].toString()=="1" && !result["acceptance"].toString().contains("passed"),"preserve reason without automatic acceptance");
    return failed?1:0;
}
