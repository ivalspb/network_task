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
 * @details Создает ID на основе PID приложения и времени запуска,
 *          гарантируя уникальность даже при перезапусках клиента
 * @return Строка с уникальным ID процесса в формате "PID_время_запуска"
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
 * @details Инициализирует все компоненты клиента: сокет, таймеры, метрики,
 *          статус устройства и генератор логов. Устанавливает соединения сигналов.
 * @param parent Родительский QObject
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

/**
 * @brief Деструктор NetworkClient
 * @details Останавливает все операции и освобождает ресурсы
 */
NetworkClient::~NetworkClient()
{
    // logMessage("NetworkClient destructor");
    stop();
}

/**
 * @brief Запускает клиент с указанными параметрами сервера
 * @details Разрешает адрес сервера, запускает таймеры статистики
 *          и инициирует подключение
 * @param address Адрес сервера (IP или hostname)
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
 * @details Останавливает все таймеры, разрывает соединение
 *          и освобождает ресурсы
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
 * @details Настраивает TCP keepalive, отправляет начальное рукопожатие
 *          и запускает мониторинг соединения
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
        int keepintvl = 2;   // Интервал проверки 2 секунды
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
 * @details Создает и отправляет JSON сообщение с типом "ClientHandshake",
 *          содержащее уникальный идентификатор процесса и временную метку
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

/**
 * @brief Обработчик отключения от сервера
 * @details Останавливает мониторинг heartbeat, таймер отправки данных
 *          и планирует переподключение
 */
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
 * @details Читает и парсит входящие JSON сообщения, разделенные символом новой строки,
 *          и передает их на обработку parseServerMessage
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

/**
 * @brief Обработчик ошибок сокета
 * @details Логирует ошибки и обрабатывает сетевые проблемы,
 *          инициируя переподключение при необходимости
 * @param error Тип ошибки сокета
 */
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
 * @details Проверяет условия подключения, добавляет случайную задержку
 *          и отправляет метрики сети, статус устройства и логи
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
 * @details Закрывает существующее соединение и пытается установить новое,
 *          с обработкой таймаутов и повторными попытками
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
 * @details Вызывается по таймеру для обновления метрик сети
 *          и статуса устройства перед отправкой на сервер
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

/**
 * @brief Отправляет метрики сети на сервер
 * @details Создает JSON сообщение с типом "NetworkMetrics",
 *          содержащее данные о bandwidth, latency и packet loss
 */
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

/**
 * @brief Отправляет статус устройства на сервер
 * @details Создает JSON сообщение с типом "DeviceStatus",
 *          содержащее данные о uptime, CPU и memory usage
 */
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

/**
 * @brief Отправляет лог-сообщение на сервер
 * @details Создает JSON сообщение с типом "Log",
 *          содержащее сгенерированное лог-сообщение
 */
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
 * @details Конвертирует JSON объект в компактный формат
 *          и добавляет символ новой строки для разделения сообщений
 * @param obj JSON объект для отправки
 * @return Массив байт готовый для отправки по сети
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
 * @details Анализирует тип сообщения и вызывает соответствующие обработчики
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

/**
 * @brief Обрабатывает команды управления от сервера
 * @details Обрабатывает команды StartDataExchange/StopDataExchange,
 *          изменяет состояние обмена данными и отправляет подтверждение
 * @param message JSON объект с командой управления
 */
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
 * @details Форматирует сообщение с временной меткой и выводит его
 *          в консоль или внутренний лог в зависимости от параметра
 * @param message Текст сообщения для логирования
 * @param toConsole Флаг указывающий на вывод в консоль (true) или внутренний лог (false)
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

/**
 * @brief Запускает мониторинг heartbeat соединения
 * @details Инициализирует счетчики таймаутов и запускает таймер
 *          для периодической проверки активности соединения
 */
void NetworkClient::startHeartbeatMonitoring()
{
    m_heartbeatTimeout = 0;
    m_lastHeartbeatResponse = QDateTime::currentDateTime();
    m_heartbeatTimer->start();
    // logMessage("Heartbeat monitoring started", TO_CONSOLE);
}

/**
 * @brief Останавливает мониторинг heartbeat соединения
 * @details Прекращает проверку активности соединения
 */
void NetworkClient::stopHeartbeatMonitoring()
{
    m_heartbeatTimer->stop();
    // logMessage("Heartbeat monitoring stopped", TO_CONSOLE);
}

/**
 * @brief Проверяет состояние соединения
 * @details Вызывается периодически для проверки активности сервера,
 *          отслеживает время с последнего ответа и инициирует
 *          переподключение при длительном отсутствии ответа
 */
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

/**
 * @brief Отправляет heartbeat запрос на сервер
 * @details Создает и отправляет сообщение типа "Heartbeat" для
 *          проверки активности соединения и получения ответа
 */
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


