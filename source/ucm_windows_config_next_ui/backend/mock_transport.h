#pragma once

#include "config_transport.h"

namespace ucm {

class MockTransport final : public ConfigurationTransport {
public:
    explicit MockTransport(Configuration active = {});

    const Configuration &activeConfiguration() const override { return m_active; }
    bool hasStagedConfiguration() const override { return m_hasStaged; }
    TransportInfo info() const override;

    void failNextRamCommit(const QString &reason = QStringLiteral("已注入 Mock RAM 提交失败。"));
    void failNextReadback(const QString &reason = QStringLiteral("已注入 Mock 回读失败。"));

    TransportResult stageRam(const Configuration &candidate) override;
    ReadbackResult readBackStaged() override;
    TransportResult confirmReadback() override;
    void rollbackStagedRam() override;
    bool injectNextReadbackFailure() override;

private:
    Configuration m_active;
    Configuration m_staged;
    bool m_hasStaged = false;
    QString m_nextCommitFailure;
    QString m_nextReadbackFailure;
};

} // namespace ucm
