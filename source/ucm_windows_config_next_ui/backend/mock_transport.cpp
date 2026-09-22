#include "mock_transport.h"

namespace ucm {

MockTransport::MockTransport(Configuration active)
    : m_active(std::move(active))
{
}

TransportInfo MockTransport::info() const
{
    return {
        QStringLiteral("MockTransport"),
        true,
        false,
        false,
        false,
        false,
        true
    };
}

void MockTransport::failNextRamCommit(const QString &reason)
{
    m_nextCommitFailure = reason;
}

void MockTransport::failNextReadback(const QString &reason)
{
    m_nextReadbackFailure = reason;
}

TransportResult MockTransport::stageRam(const Configuration &candidate)
{
    if (!m_nextCommitFailure.isEmpty()) {
        const QString reason = m_nextCommitFailure;
        m_nextCommitFailure.clear();
        return {false, reason};
    }
    const QString expectedVersion = nextConfigVersion(m_active.cfgVersion);
    if (expectedVersion.isEmpty() || candidate.cfgVersion != expectedVersion) {
        return {false, QStringLiteral("MockTransport 拒绝非连续 cfg_version；期望 %1，收到 %2。")
                    .arg(expectedVersion, candidate.cfgVersion)};
    }
    m_staged = candidate;
    m_hasStaged = true;
    return {true, QStringLiteral("MockTransport 已将完整配置对象暂存至模拟易失 RAM。")};
}

ReadbackResult MockTransport::readBackStaged()
{
    if (!m_nextReadbackFailure.isEmpty()) {
        const QString reason = m_nextReadbackFailure;
        m_nextReadbackFailure.clear();
        return {false, {}, reason};
    }
    if (!m_hasStaged) {
        return {false, {}, QStringLiteral("Mock RAM 中没有可回读的配置对象。")};
    }
    return {true, m_staged, QStringLiteral("MockTransport 返回了模拟 RAM 回读对象。")};
}

TransportResult MockTransport::confirmReadback()
{
    if (!m_hasStaged) {
        return {false, QStringLiteral("无暂存对象，不能确认回读。")};
    }
    m_active = m_staged;
    m_hasStaged = false;
    return {true, QStringLiteral("回读身份匹配；模拟活动配置已原子切换。")};
}

void MockTransport::rollbackStagedRam()
{
    m_staged = m_active;
    m_hasStaged = false;
}

bool MockTransport::injectNextReadbackFailure()
{
    failNextReadback();
    return true;
}

} // namespace ucm
