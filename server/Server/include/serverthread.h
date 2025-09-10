#pragma once

#include <QThread>
#include <QJsonObject>
#include <QTcpSocket>

class ServerWorker;

class ServerThread : public QThread
{
    Q_OBJECT
public:
    explicit ServerThread(const QJsonObject &initialThresholds = QJsonObject(), QObject *parent = nullptr);
    ~ServerThread();
    void startServer(int port);
    void stopServer();
    void updateThresholds(const QJsonObject &thresholds);
    QTcpSocket* getClientSocket(int clientId) const;
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
    void run() override;
private:
    ServerWorker *m_worker;
    int m_port;
    bool m_startRequested;
    bool m_stopRequested;
    QJsonObject m_thresholds;
};
