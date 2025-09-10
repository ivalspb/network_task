#pragma once

#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QJsonObject>
#include <QMutex>
#include <QTimer>

class ServerWorker : public QTcpServer
{
    Q_OBJECT
public:
    explicit ServerWorker(QObject *parent = nullptr);
    ~ServerWorker();
    QTcpSocket* getClientSocket(int clientId);
public slots:
    void startServer(int port);
    void stopServer();
    void updateThresholds(const QJsonObject &thresholds);
signals:
    void clientConnected(int id, const QString &ip);
    void clientDisconnected(int id);
    void dataFromClient(int id, const QString &type, const QString &content, const QString &timestamp);
    void logMessage(const QString &msg);
    void serverStatus(const QString &status);
    void clientControlStatusChanged(int clientId, bool enabled);
    void thresholdWarning(int clientId, const QString &metric,
                          double value, double threshold, const QString &advice);
protected:
    void incomingConnection(qintptr socketDescriptor) override;
private slots:
    void onClientReadyRead();
    void onClientDisconnected();
    void processControlCommand(QTcpSocket *socket, const QJsonObject &json);
private:
    QMap<QTcpSocket*, int> m_clients;
    QMap<int, QTcpSocket*> m_socketMap;
    QMap<QTcpSocket*, QByteArray> m_buffers;
    QMap<QString, int> m_processToClientId;
    QMap<int, QString> m_clientToProcessId;
    int m_nextClientId;
    QJsonObject m_thresholds;
    QMutex m_mutex;
    bool m_running;
    QMap<int, QDateTime> m_clientLastActivity; // clientId -> время последней активности
    QTimer* m_heartbeatTimer;
    int m_heartbeatInterval;

    void checkThresholds(int clientId, const QString &type, const QJsonObject &data);
    void processClientData(QTcpSocket *socket, const QJsonObject &json);
    void sendAck(QTcpSocket *socket, int clientId);
    void sendStart(QTcpSocket *socket, int clientId);
    void handleClientHandshake(QTcpSocket *socket, const QJsonObject &handshake);
    QString getTimestamp();
    QString getClientInfo(QTcpSocket *socket) const;
    void checkClientConnections();
    void onClientDisconnectedForced(QTcpSocket *socket);
};

