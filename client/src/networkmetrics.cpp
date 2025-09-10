#include "../include/networkmetrics.h"
#include <QNetworkInterface>
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDateTime>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QDebug>
#include <QTcpServer>
#include <QUdpSocket>
#include <QJsonObject>
#include <QJsonDocument>

NetworkMetrics::NetworkMetrics(QObject *parent)
    : QObject(parent),
    m_bandwidth(0),
    m_latency(0),
    m_packetLoss(0),
    m_lastTotalBytes(0)
{
    m_lastUpdateTime = QDateTime::currentDateTime();
    m_lastTotalBytes = getTotalBytes();
}

void NetworkMetrics::updateMetrics(const QHostAddress &targetAddress)
{
    m_bandwidth = getNetworkBandwidth();
    static int counter = 0;
    if (counter % 5 == 0) {
        m_latency = getNetworkLatency(targetAddress);
        m_packetLoss = getPacketLoss(targetAddress);
        m_interfaceName = getNetworkInterfaceName(targetAddress);
    }
    counter++;
}

QJsonObject NetworkMetrics::toJson() const
{
    QJsonObject metrics;
    metrics["bandwidth"] = m_bandwidth;
    metrics["latency"] = m_latency;
    metrics["packet_loss"] = m_packetLoss;
    metrics["interface"] = m_interfaceName;
    return metrics;
}

double NetworkMetrics::getNetworkBandwidth()
{
    qint64 currentTotalBytes = getTotalBytes();
    QDateTime currentTime = QDateTime::currentDateTime();
    qint64 timeDiff = m_lastUpdateTime.msecsTo(currentTime);
    if (timeDiff > 0) {
        qint64 bytesDiff = currentTotalBytes - m_lastTotalBytes;
        m_bandwidth = (bytesDiff * 1000) / timeDiff;
        m_lastTotalBytes = currentTotalBytes;
        m_lastUpdateTime = currentTime;
    }
    return m_bandwidth;
}

double NetworkMetrics::getNetworkLatency(const QHostAddress &targetAddress)
{
    // Пробуем оба метода
    double tcpLatency = measureTcpRttWithEcho(targetAddress);
    if (tcpLatency > 0) {
        return tcpLatency;
    }
    double pingLatency = measurePingLatency(targetAddress);
    if (pingLatency > 0) {
        return pingLatency;
    }
    return -1;
}

double NetworkMetrics::getPacketLoss(const QHostAddress &targetAddress)
{
    Q_UNUSED(targetAddress);
    QFile snmpFile("/proc/net/snmp");
    if (!snmpFile.open(QIODevice::ReadOnly)) {
        return 0;
    }
    QTextStream in(&snmpFile);
    QString line;
    qint64 totalSegments = 0;
    qint64 retransmittedSegments = 0;
    while (!in.atEnd()) {
        line = in.readLine();
        if (line.startsWith("Tcp:")) {
            QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() >= 13) {
                bool ok;
                retransmittedSegments = parts[10].toLongLong(&ok);
                if (!ok) retransmittedSegments = 0;
                totalSegments = parts[12].toLongLong(&ok);
                if (!ok) totalSegments = 1;
            }
            break;
        }
    }
    if (totalSegments > 0) {
        return (static_cast<double>(retransmittedSegments) / totalSegments) * 100.0;
    }
    return 0;
}

QString NetworkMetrics::getNetworkInterfaceName(const QHostAddress &targetAddress)
{
    QProcess process;
    process.start("ip", QStringList() << "route" << "get" << targetAddress.toString());
    if (process.waitForFinished(1000)) {
        QString output = process.readAllStandardOutput();
        QRegularExpression regex("dev\\s+(\\S+)");
        QRegularExpressionMatch match = regex.match(output);
        if (match.hasMatch()) {
            return match.captured(1);
        }
    }
    QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &interface : interfaces) {
        if (interface.flags().testFlag(QNetworkInterface::IsUp) &&
            !interface.flags().testFlag(QNetworkInterface::IsLoopBack) &&
            interface.flags().testFlag(QNetworkInterface::IsRunning)) {
            return interface.humanReadableName();
        }
    }
    return "unknown";
}

qint64 NetworkMetrics::getTotalBytes()
{
    qint64 totalBytes = 0;
    QFile file("/proc/net/dev");
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    QTextStream in(&file);
    QString line;
    in.readLine();
    in.readLine();
    while (!in.atEnd()) {
        line = in.readLine().trimmed();
        int colonPos = line.indexOf(':');
        if (colonPos == -1) continue;
        QString interfaceName = line.left(colonPos).trimmed();
        QString stats = line.mid(colonPos + 1).trimmed();
        if (interfaceName == "lo") continue;
        QStringList parts = stats.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 16) continue;
        bool ok1, ok2;
        qint64 receiveBytes = parts[0].toLongLong(&ok1);
        qint64 transmitBytes = parts[8].toLongLong(&ok2);
        if (ok1 && ok2) {
            totalBytes += receiveBytes + transmitBytes;
        }
    }
    return totalBytes;
}

double NetworkMetrics::measureTcpRttWithEcho(const QHostAddress &address, quint16 port)
{
    QTcpSocket socket;
    QElapsedTimer timer;
    // Устанавливаем короткие таймауты
    socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    timer.start();
    // Подключаемся к серверу
    socket.connectToHost(address, port);
    if (!socket.waitForConnected(1000)) {
        qDebug() << "TCP connect failed:" << socket.errorString();
        return -1;
    }
    // Отправляем эхо-запрос специального формата
    QJsonObject echoRequest;
    echoRequest["type"] = "EchoRequest";
    echoRequest["data"] = QString("PING_%1").arg(QDateTime::currentMSecsSinceEpoch());
    echoRequest["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QJsonDocument doc(echoRequest);
    QByteArray requestData = doc.toJson(QJsonDocument::Compact) + "\n";
    // Засекаем время отправки
    timer.restart();
    qint64 bytesWritten = socket.write(requestData);
    if (bytesWritten != requestData.size() || !socket.waitForBytesWritten(1000)) {
        qDebug() << "Failed to send echo request";
        socket.disconnectFromHost();
        return -1;
    }
    // Ждем ответ
    QByteArray response;
    while (timer.elapsed() < 2000) {
        if (socket.waitForReadyRead(50)) {
            response += socket.readAll();
            // Проверяем, получили ли полный JSON ответ
            if (response.contains('\n')) {
                QJsonParseError error;
                QJsonDocument responseDoc = QJsonDocument::fromJson(response, &error);
                if (error.error == QJsonParseError::NoError &&
                    responseDoc.isObject()) {
                    QJsonObject responseObj = responseDoc.object();
                    if (responseObj["type"] == "EchoResponse") {
                        qint64 rttTime = timer.elapsed();
                        socket.disconnectFromHost();
                        return static_cast<double>(rttTime);
                    }
                }
            }
        }
    }
    socket.disconnectFromHost();
    // qDebug() << "Echo response timeout or invalid response";
    return -1;
}

double NetworkMetrics::measurePingLatency(const QHostAddress &address)
{
    QProcess pingProcess;
    QStringList args;
#ifdef Q_OS_WINDOWS
    args << "-n" << "1" << "-w" << "1000";
#else
    args << "-c" << "1" << "-W" << "1";
#endif
    args << address.toString();
    pingProcess.start("ping", args);
    if (!pingProcess.waitForFinished(2000)) {
        return -1;
    }
    QString output = pingProcess.readAllStandardOutput();
    QRegularExpression regex("time=([0-9.]+)");
    QRegularExpressionMatch match = regex.match(output);
    if (match.hasMatch()) {
        return match.captured(1).toDouble();
    }
    return -1;
}
