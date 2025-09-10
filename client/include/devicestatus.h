#pragma once

#include <QObject>
#include <QJsonObject>

class DeviceStatus : public QObject
{
    Q_OBJECT
public:
    explicit DeviceStatus(QObject *parent = nullptr);
    void updateStatus();
    QJsonObject toJson() const;
private:
    qint64 m_uptime;
    double m_cpuUsage;
    double m_memoryUsage;
    qint64 m_lastCpuTotal;
    qint64 m_lastCpuIdle;
    QDateTime m_lastCpuUpdate;

    qint64 getUptime();
    double getCpuUsage();
    double getMemoryUsage();
    void getLinuxCpuUsage(qint64 &total, qint64 &idle);
};
