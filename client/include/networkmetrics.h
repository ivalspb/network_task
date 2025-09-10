#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QHostAddress>

class NetworkMetrics : public QObject
{
    Q_OBJECT
public:
    explicit NetworkMetrics(QObject *parent = nullptr);
    void updateMetrics(const QHostAddress &targetAddress);
    QJsonObject toJson() const;
private:
    double m_bandwidth;
    double m_latency;
    double m_packetLoss;
    QString m_interfaceName;
    qint64 m_lastTotalBytes;
    QDateTime m_lastUpdateTime;

    double getNetworkBandwidth();
    double getNetworkLatency(const QHostAddress &targetAddress);
    double getPacketLoss(const QHostAddress &targetAddress);
    QString getNetworkInterfaceName(const QHostAddress &targetAddress);
    qint64 getTotalBytes();
    double measureTcpRttWithEcho(const QHostAddress &address, quint16 port = 12345);
    double measurePingLatency(const QHostAddress &address);
};
