#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonObject>
#include <QHostAddress>
#include <QHostInfo>
#include <QUuid>
#include <QDateTime>
#include "networkmetrics.h"
#include "devicestatus.h"
#include "loggenerator.h"

class NetworkClient : public QObject
{
    Q_OBJECT
public:
    explicit NetworkClient(QObject *parent = nullptr);
    ~NetworkClient();
public slots:
    void start(const QString &address = "localhost", quint16 port = 12345);
    void stop();
signals:
    void finished();
private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void processControlCommand(const QJsonObject &message);
    void onErrorOccurred(QAbstractSocket::SocketError error);
    void sendData();
    void tryReconnect();
    void updateRealTimeData();
    void checkConnectionHealth();
    void sendHeartbeat();
private:
    QTcpSocket *m_socket;
    QTimer *m_reconnectTimer;
    QTimer *m_dataTimer;
    QTimer *m_statsTimer;
    QTimer *m_heartbeatTimer;
    NetworkMetrics *m_networkMetrics;
    DeviceStatus *m_deviceStatus;
    LogGenerator *m_logGenerator;
    QHostAddress m_serverAddress;
    quint16 m_serverPort;
    int m_clientId;
    int m_heartbeatTimeout;
    bool m_connectionAcknowledged;
    bool m_dataExchangeEnabled;
    QString m_processId;
    QDateTime m_lastHeartbeatResponse;

    void sendNetworkMetrics();
    void sendDeviceStatus();
    void sendLog();
    QByteArray createJsonMessage(const QJsonObject &obj);
    void parseServerMessage(const QJsonObject &message);
    void logMessage(const QString &message, bool toConsole = false);
    QString generateProcessId();
    void sendInitialHandshake();
    void startHeartbeatMonitoring();
    void stopHeartbeatMonitoring();
};
