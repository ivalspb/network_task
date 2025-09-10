#include "../include/client.h"
#include <QJsonDocument>
#include <QHostAddress>
#include <QDateTime>
#include <QCoreApplication>
#include <QProcess>
#include <iostream>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QThread>

// #include <sys/socket.h>    // базовые определения сокетов
#include <netinet/tcp.h>   // TCP-специфичные опции
#include <netinet/in.h>    // сетевые структуры
// #include <arpa/inet.h>     // сетевые функции

const int RECONNECT_INTERVAL = 5000;
const int SEND_INTERVAL = 3000;
const int STATS_UPDATE_INTERVAL = 1000;
const bool TO_CONSOLE = true;

/**
 * @brief Генерирует уникальный идентификатор процесса
 * @return Строка с уникальным ID процесса
 */
QString NetworkClient::generateProcessId()
{
    // Используем комбинацию PID и времени запуска
    static QString processId;
    if (processId.isEmpty()) {
        qint64 pid = QCoreApplication::applicationPid();
        QString startupTime = QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
        processId = QString("%1_%2").arg(pid).arg(startupTime);
    }
    return processId;
}

/**
 * @brief Конструктор NetworkClient
 * Инициализирует все компоненты клиента и устанавливает соединения сигналов
 */
NetworkClient::NetworkClient(QObject *parent)
    : QObject(parent),
    m_socket(new QTcpSocket(this)),
    m_reconnectTimer(new QTimer(this)),
    m_dataTimer(new QTimer(this)),
    m_statsTimer(new QTimer(this)),
    m_heartbeatTimer(new QTimer(this)),
    m_networkMetrics(new NetworkMetrics(this)),
    m_deviceStatus(new DeviceStatus(this)),
    m_logGenerator(new LogGenerator(this)),
    m_serverPort(12345),
    m_clientId(-1),
    m_connectionAcknowledged(false),
    m_processId(generateProcessId()),
    m_heartbeatTimeout(0),
    m_dataExchangeEnabled(true),
    m_lastHeartbeatResponse(QDateTime::currentDateTime())
{
    // logMessage("NetworkClient constructor starting with processId: " + m_processId);
    // Настройка таймеров
    m_reconnectTimer->setInterval(RECONNECT_INTERVAL);
    m_reconnectTimer->setSingleShot(true);
    m_dataTimer->setInterval(SEND_INTERVAL);
    m_statsTimer->setInterval(STATS_UPDATE_INTERVAL);
    m_heartbeatTimer->setInterval(5000); // Проверка каждые 5 секунд
    // Подключение сигналов сокета
    connect(m_socket, &QTcpSocket::connected, this, &NetworkClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &NetworkClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &NetworkClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &NetworkClient::onErrorOccurred);
    // Подключение сигналов таймеров
    connect(m_reconnectTimer, &QTimer::timeout, this, &NetworkClient::tryReconnect);
    connect(m_dataTimer, &QTimer::timeout, this, &NetworkClient::sendData);
    connect(m_statsTimer, &QTimer::timeout, this, &NetworkClient::updateRealTimeData);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &NetworkClient::checkConnectionHealth);
}

NetworkClient::~NetworkClient()
{
    // logMessage("NetworkClient destructor");
    stop();
}

/**
 * @brief Запускает клиент с указанными параметрами сервера
 * @param address Адрес сервера
 * @param port Порт сервера
 */
void NetworkClient::start(const QString &address, quint16 port)
{
    // Разрешение адреса
    QHostAddress hostAddress;
    if (!hostAddress.setAddress(address)) {
        // Попробуем разрешить hostname
        QList<QHostAddress> addresses = QHostInfo::fromName(address).addresses();
        if (addresses.isEmpty()) {
            logMessage("Invalid server address: " + address, TO_CONSOLE);
            return;
        }
        hostAddress = addresses.first();
    }
    m_serverAddress = hostAddress;
    m_serverPort = port;
    logMessage(QString("Client starting with server %1:%2").arg(address).arg(port), TO_CONSOLE);
    // Запускаем обновление статистики
    m_statsTimer->start();
    // Начинаем попытки подключения
    tryReconnect();
}


/**
 * @brief Останавливает клиент и закрывает все соединения
 */
void NetworkClient::stop()
{
    // logMessage("Stopping client");
    // Останавливаем все таймеры
    m_reconnectTimer->stop();
    m_dataTimer->stop();
    m_statsTimer->stop();
    // Закрываем соединение
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->disconnectFromHost();
    }
}

/**
 * @brief Обработчик успешного подключения к серверу
 */
void NetworkClient::onConnected()
{
    logMessage(QString("Connected to server %1:%2")
        .arg(m_serverAddress.toString()).arg(m_serverPort), TO_CONSOLE);
    // Останавливаем таймер переподключения
    m_reconnectTimer->stop();
    m_connectionAcknowledged = false;
    // Включаем TCP keepalive
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
// На Linux можно настроить параметры keepalive
#ifdef Q_OS_LINUX
    int fd = m_socket->socketDescriptor();
    if (fd != -1) {
        int keepalive = 1;
        int keepidle = 5;    // Начать проверки через 5 секунд простоя
        int keepintvl = 2;   // Интервал проверок 2 секунды
        int keepcnt = 3;     // Количество проверок перед разрывом
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
    }
#endif
    //Отправляем начальное сообщение с process_id сразу после подключения
    sendInitialHandshake();
    // Запускаем мониторинг соединения
    startHeartbeatMonitoring();
}

/**
 * @brief Отправляет начальное рукопожатие с сервером
 */
void NetworkClient::sendInitialHandshake()
{
    // logMessage("Sending initial handshake");
    QJsonObject handshake;
    handshake["type"] = "ClientHandshake";
    handshake["process_id"] = m_processId;
    handshake["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_socket->write(createJsonMessage(handshake));
    // logMessage("Sent initial handshake");
}

void NetworkClient::onDisconnected()
{
    // logMessage("Disconnected from server");
    stopHeartbeatMonitoring();
    // Останавливаем отправку данных
    m_dataTimer->stop();
    m_connectionAcknowledged = false;
    // Планируем переподключение
    if (!m_reconnectTimer->isActive()) {
        logMessage("Attempting to reconnect...");
        m_reconnectTimer->start();
    }
}

/**
 * @brief Обработчик входящих данных от сервера
 */
void NetworkClient::onReadyRead()
{
    // logMessage("onReadyRead: Received data from server");
    QByteArray data = m_socket->readAll();
    QList<QByteArray> messages = data.split('\n');
    for (const QByteArray &message : messages) {
        if (message.trimmed().isEmpty()) {
            // logMessage("Empty message, skipping");
            continue;
        }
        // qDebug() << "Processing message:" << message;
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(message, &error);
        if (error.error != QJsonParseError::NoError) {
            // logMessage("Failed to parse server message: " + error.errorString());
            continue;
        }
        if (!doc.isObject()) {
            // logMessage("Invalid JSON message from server");
            continue;
        }
        QJsonObject obj = doc.object();
        parseServerMessage(obj);
    }
}

void NetworkClient::onErrorOccurred(QAbstractSocket::SocketError error)
{
    QString errorStr = m_socket->errorString();
    logMessage("Socket error: " + errorStr);
    // Особые случаи ошибок, которые указывают на обрыв сети
    if (error == QAbstractSocket::NetworkError ||
        error == QAbstractSocket::RemoteHostClosedError ||
        errorStr.contains("network", Qt::CaseInsensitive) ||
        errorStr.contains("host", Qt::CaseInsensitive)) {
        logMessage("Network issue detected, reconnecting...");
        m_socket->abort(); // Немедленно разрываем соединение
    }    // Планируем переподключение при ошибке
    if (m_socket->state() != QAbstractSocket::ConnectedState &&
        !m_reconnectTimer->isActive()) {
        m_reconnectTimer->start();
    }
}

/**
 * @brief Отправляет данные серверу (вызывается по таймеру)
 */
void NetworkClient::sendData()
{
    // logMessage("sendData");
    // Проверяем все условия перед отправкой
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState ||
        !m_connectionAcknowledged) {
        logMessage("sendData - Conditions not met for sending data");
        return;
    }
    if (!m_dataExchangeEnabled) {
        return;
    }
    // Генерируем случайную задержку от 10 до 100 миллисекунд (0.01 до 0.1 сек)
    int randomDelay = QRandomGenerator::global()->bounded(10, 101);
    // logMessage(QString("Adding random delay: %1 ms").arg(randomDelay));
    // Используем QTimer для асинхронной отправки с задержкой
    QTimer::singleShot(randomDelay, this, [this]() {
        // Проверяем соединение еще раз после задержки
        if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState ||
            !m_connectionAcknowledged) {
            // logMessage("Connection lost during delay, skipping send");
            return;
        }
        // Отправляем различные типы данных
        sendNetworkMetrics();
        sendDeviceStatus();
        sendLog();
        // logMessage("Data sent successfully with random delay");
    });
}

/**
 * @brief Пытается переподключиться к серверу
 */
void NetworkClient::tryReconnect()
{
    // Проверяем текущее состояние сокета
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        // logMessage("Already connected to server");
        return;
    }
    // Полностью закрываем старое соединение
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort(); // ← Используем abort вместо disconnectFromHost
    }
    // logMessage(QString("Attempting to connect to %1:%2...")
    //                .arg(m_serverAddress.toString()).arg(m_serverPort));
    m_socket->connectToHost(m_serverAddress, m_serverPort);
    // Уменьшаем таймаут ожидания подключения
    if (!m_socket->waitForConnected(2000)) {
        // logMessage(QString("Connection failed: %1").arg(m_socket->errorString()));
        if (!m_reconnectTimer->isActive()) {
            m_reconnectTimer->start(3000); // Ждем 3 секунды перед повторной попыткой
        }
    }
}

/**
 * @brief Обновляет данные в реальном времени
 */
void NetworkClient::updateRealTimeData()
{
    // logMessage("updateRealTimeData");
    if (m_socket&& m_socket->state() == QAbstractSocket::ConnectedState&& m_connectionAcknowledged) {
        // Обновляем метрики
        m_networkMetrics->updateMetrics(m_serverAddress);
        m_deviceStatus->updateStatus();
    }
}

void NetworkClient::sendNetworkMetrics()
{
    // logMessage("sendNetworkMetrics");
    QJsonObject metrics = m_networkMetrics->toJson();
    metrics["process_id"] = m_processId;
    QJsonObject message;
    message["type"] = "NetworkMetrics";
    message["metrics"] = metrics;
    message["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_socket->write(createJsonMessage(message));
}

void NetworkClient::sendDeviceStatus()
{
    // logMessage("sendDeviceStatus");
    QJsonObject status = m_deviceStatus->toJson();
    status["process_id"] = m_processId;
    QJsonObject message;
    message["type"] = "DeviceStatus";
    message["status"] = status;
    message["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_socket->write(createJsonMessage(message));
    // logMessage("Sent DeviceStatus data");
}

void NetworkClient::sendLog()
{
    // logMessage("sendLog");
    QJsonObject logM = m_logGenerator->generateLog();
    logM["process_id"] = m_processId;
    logM["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_socket->write(createJsonMessage(logM));
    // logMessage("Sent Log: " + logM["message"].toString());
}

/**
 * @brief Создает JSON сообщение для отправки
 * @param obj JSON объект для отправки
 * @return Массив байт готовый для отправки
 */
QByteArray NetworkClient::createJsonMessage(const QJsonObject &obj)
{
    // logMessage("createJsonMessage");
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    data.append('\n');
    // qDebug() << "Sending message:" << data;
    return data;
}

/**
 * @brief Обрабатывает сообщения от сервера
 * @param message JSON объект с сообщением от сервера
 */
void NetworkClient::parseServerMessage(const QJsonObject &message)
{
    // logMessage("parseServerMessage");
    QString type = message.value("type").toString();
    if (type == "HeartbeatResponse") {
        // Обновляем время последнего ответа
        m_lastHeartbeatResponse = QDateTime::currentDateTime();
        m_heartbeatTimeout = 0;
        return;
    }
    else if (type == "StartDataExchange" || type == "StopDataExchange") {
        processControlCommand(message);
    }
    else if (type == "ConnectionAck") {
        // Подтверждение подключения получено
        m_connectionAcknowledged = true;
        m_clientId = message.value("client_id").toInt(-1);
        QString status = message.value("status").toString();
        logMessage(QString("Connection acknowledged. Client ID: %1, Status: ").arg(m_clientId)
                       + status, TO_CONSOLE);
    }
    else if (type == "StartCommand"){
        // Команда начать отправку данных
        m_dataTimer->start();
        logMessage( "[" + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
                  + "] Starting data exchange with server", TO_CONSOLE);
    }
    else if (type == "ThresholdWarning") {
        QString metric = message.value("metric").toString();
        double value = message.value("value").toDouble();
        double threshold = message.value("threshold").toDouble();
        QString advice = message.value("advice").toString();
        logMessage("THRESHOLD WARNING: " + metric + " = " + QString::number(value)
                       + " (threshold: " + QString::number(threshold) + ")"
                  + (advice.isEmpty() ? "" : " - " + advice), TO_CONSOLE);
    }
    else if (type == "Log") {
        QString logMsg = message.value("message").toString();
        QString severity = message.value("severity").toString("INFO");

        logMessage("SERVER LOG [" + severity + "]: "
                  + logMsg, TO_CONSOLE);
    }
    else {
        logMessage("Received unknown message type: " + type, TO_CONSOLE);
    }
    // Для любых сообщений от сервера обновляем время ответа
    m_lastHeartbeatResponse = QDateTime::currentDateTime();
}

void NetworkClient::processControlCommand(const QJsonObject &message)
{
    QString type = message.value("type").toString();
    bool enable = (type == "StartDataExchange");
    m_dataExchangeEnabled = enable;
    // Отправляем подтверждение
    QJsonObject response;
    response["type"] = "ControlResponse";
    response["status"] = enable ? "started" : "stopped";
    response["command"] = type;
    response["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_socket->write(createJsonMessage(response));
    // logMessage(QString("Data exchange %1 by server command").arg(enable ? "started" : "stopped"));
}

/**
 * @brief Логирует сообщение
 * @param message Текст сообщения
 * @param toConsole Выводить ли в консоль
 */
void NetworkClient::logMessage(const QString &message, bool toConsole)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString logLine = "[" + timestamp + "] " + message;
    if (toConsole) {
        std::cout << logLine.toStdString() << std::endl;
    } else {
        qDebug() << logLine;
    }
}

void NetworkClient::startHeartbeatMonitoring()
{
    m_heartbeatTimeout = 0;
    m_lastHeartbeatResponse = QDateTime::currentDateTime();
    m_heartbeatTimer->start();
    // logMessage("Heartbeat monitoring started", TO_CONSOLE);
}

void NetworkClient::stopHeartbeatMonitoring()
{
    m_heartbeatTimer->stop();
    // logMessage("Heartbeat monitoring stopped", TO_CONSOLE);
}

void NetworkClient::checkConnectionHealth()
{
    if (m_socket->state() != QAbstractSocket::ConnectedState || !m_connectionAcknowledged) {
        m_heartbeatTimeout = 0;
        return;
    }
    // Проверяем, когда был последний ответ от сервера
    qint64 secondsSinceLastResponse = m_lastHeartbeatResponse.secsTo(QDateTime::currentDateTime());
    if (secondsSinceLastResponse > 15) { // 15 секунд без ответа
        m_heartbeatTimeout++;
        logMessage(QString("No server response for %1 seconds (timeout %2/3)")
                       .arg(secondsSinceLastResponse).arg(m_heartbeatTimeout), TO_CONSOLE);
        if (m_heartbeatTimeout >= 3) {
            logMessage("Connection considered dead, reconnecting...", TO_CONSOLE);
            m_socket->abort(); // Принудительно разрываем соединение
            onDisconnected();
        }
    }
    // Отправляем heartbeat запрос
    sendHeartbeat();
}

void NetworkClient::sendHeartbeat()
{
    if (m_socket->state() != QAbstractSocket::ConnectedState || !m_connectionAcknowledged) {
        return;
    }
    QJsonObject heartbeat;
    heartbeat["type"] = "Heartbeat";
    heartbeat["client_id"] = m_clientId;
    heartbeat["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_socket->write(createJsonMessage(heartbeat));
    m_socket->flush();
}


