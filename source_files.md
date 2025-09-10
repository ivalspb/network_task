# Список C++ файлов и их содержимое

## Файл: server/Server/include/serverthread.h

```cpp
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

```

---


## Файл: server/Server/include/serverworker.h

```cpp
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


```

---


## Файл: server/Server/src/serverworker.cpp

```cpp
#include "../../Server/include/serverworker.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
#include <QHostAddress>
#include <QDebug>
#include <QThread>

ServerWorker::ServerWorker(QObject *parent)
    : QTcpServer(parent),
    m_nextClientId(1),
    m_running(false),
    m_heartbeatInterval(10000) // Проверка каждые 10 секунд
{
    // qDebug()<< QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
    //     <<"constructor serverWorker";
    // Таймер для проверки heartbeat клиентов
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ServerWorker::checkClientConnections);
    m_heartbeatTimer->start(m_heartbeatInterval);
}

ServerWorker::~ServerWorker()
{
    // qDebug()<< QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
    //     <<"destructor serverWorker";
    m_heartbeatTimer->stop();
    stopServer();
}

void ServerWorker::checkClientConnections()
{
    // qDebug() << "=== checkClientConnections START ===";
    // qDebug() << "Attempting to lock mutex...";
    QMutexLocker locker(&m_mutex);
    // qDebug() << "Mutex locked successfully";
    // qDebug() << "Active clients:" << m_clients.size();
    // qDebug() << "Socket map size:" << m_socketMap.size();
    QDateTime currentTime = QDateTime::currentDateTime();
    QList<QTcpSocket*> disconnectedSockets;
    QList<int> disconnectedClientIds;
    // СОБИРАЕМ данные для отключения под защитой мьютекса
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        QTcpSocket* socket = it.key();
        int clientId = it.value();
        // qDebug() << "Checking client" << clientId << "socket:" << socket;
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
            // qDebug() << "Client" << clientId << "socket not connected, marking for disconnect";
            disconnectedSockets.append(socket);
            disconnectedClientIds.append(clientId);
            continue;
        }
        // Проверяем время последней активности
        if (m_clientLastActivity.contains(clientId)) {
            qint64 inactiveSeconds = m_clientLastActivity[clientId].secsTo(currentTime);
            // qDebug() << "Client" << clientId << "inactive for" << inactiveSeconds << "seconds";
            if (inactiveSeconds > 30) {
                // qDebug() << "Client" << clientId << "inactive for" << inactiveSeconds << "seconds, disconnecting";
                disconnectedSockets.append(socket);
                disconnectedClientIds.append(clientId);
            }
        } else {
            // qDebug() << "Client" << clientId << "no activity record, creating one";
            m_clientLastActivity[clientId] = currentTime;
        }
    }
    // qDebug() << "Found" << disconnectedSockets.size() << "sockets to disconnect";
    // РАЗБЛОКИРУЕМ мьютекс перед операциями ввода-вывода
    locker.unlock();
    // qDebug() << "Mutex unlocked for disconnect operations";
    // Отключаем сокеты БЕЗ блокировки мьютекса
    for (int i = 0; i < disconnectedSockets.size(); ++i) {
        QTcpSocket* socket = disconnectedSockets[i];
        int clientId = disconnectedClientIds[i];
        if (socket) {
            // qDebug() << "Disconnecting socket for client:" << clientId;
            socket->disconnectFromHost();
            if (socket->state() != QAbstractSocket::UnconnectedState) {
                // qDebug() << "Waiting for socket disconnect...";
                socket->waitForDisconnected(1000);
            }
            // Вызываем обработчик отключения с повторной блокировкой
            onClientDisconnectedForced(socket);
        }
    }
    // qDebug() << "=== checkClientConnections END ===";
}
void ServerWorker::onClientDisconnectedForced(QTcpSocket *socket)
{
    if (!socket) return;
    QMutexLocker locker(&m_mutex);
    int clientId = m_clients.value(socket, -1);
    if (clientId != -1) {
        QString processId = m_clientToProcessId.value(clientId);
        // Удаляем из всех структур
        m_clients.remove(socket);
        if (m_socketMap.value(clientId) == socket) {
            m_socketMap.remove(clientId);
        }
        m_buffers.remove(socket);
        m_clientLastActivity.remove(clientId);
        locker.unlock();
        emit clientDisconnected(clientId);
        emit logMessage(QString("Client %1 force-disconnected, process: %2")
                            .arg(clientId).arg(processId));
    }
    socket->deleteLater();
}


void ServerWorker::startServer(int port)
{
    // qDebug()//<< QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString()
    //          << "startServer - Attempting to start server on port:" << port;
    // Проверяем валидность порта
    if ((port < 1024) || (port > 65535)) {
        emit serverStatus("Error: Invalid port number. Use ports 1024-65535");
        emit logMessage("Invalid port number: " + QString::number(port));
        return;
    }
    QMutexLocker locker(&m_mutex);
    if (m_running) 
    {
        emit serverStatus("Server already running");
        return;
    }
    // Пытаемся запустить сервер
    if (!listen(QHostAddress::Any, port)) {
        emit serverStatus("Error: " + errorString());
        emit logMessage("Failed to start server: " + errorString());
        return;
    }
    m_running = true;
    emit serverStatus(QString("Listening on port %1").arg(port));
    emit logMessage("Server started successfully");
    // qDebug()<<"Server started successfully";
}

void ServerWorker::stopServer()
{
    // qDebug() << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
    //          << "stopServerWorker - Thread:" << QThread::currentThreadId();
    QMutexLocker locker(&m_mutex);
    // qDebug() << "Mutex locked in stopServer";
    if (!m_running) {
        // qDebug() << "Server already stopped";
        return;
    }
    // qDebug() << "Current clients count:" << m_clients.size();
    // qDebug() << "Current processToClientId count:" << m_processToClientId.size();
    // qDebug() << "Current clientToProcessId count:" << m_clientToProcessId.size();
    m_running = false;
    auto sockets = m_clients.keys();    // Создаем копию списка клиентов для безопасного доступа
    locker.unlock();// РАЗБЛОКИРУЕМ мьютекс перед закрытием клиентов
    for (QTcpSocket *socket : sockets) {
        socket->disconnect();// Отключаем сигналы чтобы избежать рекурсивных вызовов
        if (socket->state() == QAbstractSocket::ConnectedState) {
            // qDebug()<<"disconnectFromHost";
            socket->disconnectFromHost();
            if (socket->state() != QAbstractSocket::UnconnectedState) {
                socket->waitForDisconnected(1000);
                // qDebug()<<"waitForDisconnected";
            }
        }
    }
    locker.relock();    // Снова блокируем мьютекс для очистки структур данных
    if (isListening()) {
        // qDebug() << "Closing server socket...";
        close();
    }
    // Очищаем только сокет-специфичные данные, но сохраняем идентификацию клиентов
    m_clients.clear();
    m_socketMap.clear();
    m_buffers.clear();
    // qDebug() << "After stop - processToClientId count:" << m_processToClientId.size();
    // qDebug() << "After stop - clientToProcessId count:" << m_clientToProcessId.size();
    emit serverStatus("Stopped");
    emit logMessage("Server stopped");
    // qDebug() << "Server stopped successfully";
}

void ServerWorker::updateThresholds(const QJsonObject &thresholds)
{
    QMutexLocker locker(&m_mutex);
    m_thresholds = thresholds;
    // qDebug() << "Thresholds updated:" << m_thresholds;
    emit logMessage("Thresholds updated");
    // Для отладки выведем все пороги
    // for (auto it = m_thresholds.begin(); it != m_thresholds.end(); ++it) {
    //     qDebug() << "Threshold" << it.key() << "=" << it.value().toDouble();
    // }
}

/**
 * @brief Обработчик входящих соединений
 * @param socketDescriptor Дескриптор сокета
 */
void ServerWorker::incomingConnection(qintptr socketDescriptor)
{
    // qDebug()<< QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
    //          <<"incomingConnection  with descriptor: "<< socketDescriptor;
    if (!m_running) {
        return; // Игнорируем входящие соединения если сервер остановлен
    }
    // Создаем новый сокет для клиента
    QTcpSocket *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {//устанавливаем соединение
        delete socket;
        emit logMessage("Failed to set socket descriptor");
        return;
    }
    // Проверяем, установлено ли соединение
    if (socket->state() != QAbstractSocket::ConnectedState) {
        emit logMessage("Socket not in connected state after setSocketDescriptor");
        delete socket;
        return;
    }
    // Ждем данные от клиента чтобы получить processId
    // Обработка будет в onClientReadyRead
    m_buffers.insert(socket, QByteArray());
    connect(socket, &QTcpSocket::readyRead, this, &ServerWorker::onClientReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &ServerWorker::onClientDisconnected);
    // qDebug()<<"Incoming connection setup completed";
}

/**
 * @brief Обработчик готовности данных от клиента
 */
void ServerWorker::onClientReadyRead()
{
    // qDebug() << "onClientReadyRead - START";
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket){
        // qDebug() << "onClientReadyRead - Invalid socket";
        return;
    }
    // Читаем все доступные данные
    QByteArray data = socket->readAll();
    QByteArray &buffer = m_buffers[socket];
    buffer.append(data);
    // qDebug() << "Received data from client:" << data;
    // qDebug() << "Buffer content:" << buffer;
    // Обрабатываем сообщения построчно
    int pos;
    while ((pos = buffer.indexOf('\n')) != -1) {
        QByteArray line = buffer.left(pos).trimmed();
        buffer.remove(0, pos + 1);
        if (!line.isEmpty()) {
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError) {
                emit logMessage("JSON parse error: " + error.errorString());
                continue;
            }
            if (!doc.isObject()) {
                emit logMessage("Invalid JSON format");
                continue;
            }
            QJsonObject obj = doc.object();
            QString type = obj.value("type").toString();
            //Обрабатываем начальное рукопожатие
            if (type == "ClientHandshake" && !m_clients.contains(socket)) {
                // qDebug()<<"Обрабатываем начальное рукопожатие";
                handleClientHandshake(socket, obj);
            }
            else if (m_clients.contains(socket)) {
                // Обрабатываем обычные данные от уже зарегистрированного клиента
                // qDebug()<<"Обрабатываем обычные данные от уже зарегистрированного клиента";
                processClientData(socket, obj);
            }
            else {
                // Клиент не зарегистрирован, игнорируем сообщение
                // emit logMessage("Received data from unregistered client");
            }
        }
    }
}

/**
 * @brief Обрабатывает начальное рукопожатие с клиентом
 * @param socket Сокет клиента
 * @param handshake Объект с данными рукопожатия
 */
void ServerWorker::handleClientHandshake(QTcpSocket *socket, const QJsonObject &handshake)
{
    // qDebug() << "Handling client handshake from:" << getClientInfo(socket);
    QString processId = handshake.value("process_id").toString();
    // qDebug() << "Process ID from handshake:" << processId;
    if (processId.isEmpty()) {
        emit logMessage("Client handshake missing process_id");
        socket->disconnectFromHost();
        return;
    }
    QMutexLocker locker(&m_mutex);//reduce mutex?
    // qDebug() << "handleClientHandshake - Mutex locked";
    // qDebug() << "Checking processToClientId for process:" << processId;
    // qDebug() << "Current processToClientId contents:" << m_processToClientId.keys();
    // Проверяем, есть ли уже запись с таким processId
    int clientId;
    bool isReconnection = false;
    if (m_processToClientId.contains(processId)) {
        // Клиент с таким process_id уже был подключен
        clientId = m_processToClientId.value(processId);
        // qDebug() << "Found existing client ID:" << clientId << "for process:" << processId;
        // Проверяем, активен ли старый сокет
        QTcpSocket* oldSocket = m_socketMap.value(clientId, nullptr);
        bool hasActiveSocket = oldSocket &&
                               oldSocket->state() == QAbstractSocket::ConnectedState;
        // qDebug() << "Has active socket:" << hasActiveSocket;
        if (hasActiveSocket && oldSocket != socket) {
            // Активное соединение уже существует - отклоняем новое
            // qDebug() << "Rejecting duplicate connection";
            emit logMessage(QString("Rejecting duplicate connection from process %1 (existing client ID: %2)")
                                .arg(processId).arg(clientId));
            QJsonObject errorResponse;
            errorResponse["type"] = "ConnectionError";
            errorResponse["message"] = "Duplicate process connection";
            QJsonDocument doc(errorResponse);
            socket->write(doc.toJson(QJsonDocument::Compact));
            socket->write("\n");
            socket->flush();
            socket->disconnectFromHost();
            socket->deleteLater();
            m_buffers.remove(socket);
            return;
        } else {
            // Старый сокет не активен - это переподключение
            // qDebug() << "Allowing reconnection with existing ID";
            isReconnection = true;
            // Очищаем старые данные сокета если они есть
            if (oldSocket && oldSocket != socket) {
                oldSocket->disconnect();
                oldSocket->deleteLater();
                // Удаляем старые записи из maps
                m_clients.remove(oldSocket);
                m_buffers.remove(oldSocket);
            }
            emit logMessage(QString("Client reconnected with existing ID: %1, process: %2")
                                .arg(clientId).arg(processId));
        }
    } else {
        // Новый клиент - создаем новый ID
        // qDebug() << "New client, generating new ID";
        clientId = m_nextClientId++;
        m_processToClientId.insert(processId, clientId);
        m_clientToProcessId.insert(clientId, processId);
        emit logMessage(QString("New client registered with ID: %1, process: %2")
                            .arg(clientId).arg(processId));
    }
    // Обновляем данные клиента
    QString clientInfo = getClientInfo(socket);
    m_clients.insert(socket, clientId);
    m_socketMap.insert(clientId, socket);
    m_buffers.insert(socket, QByteArray());
    m_clientLastActivity[clientId] = QDateTime::currentDateTime();
    // qDebug() << "Client registered with ID:" << clientId << "Process:" << processId;
    // Отправляем подтверждение подключения
    sendAck(socket, clientId);
    // Отправляем команду начать работу
    sendStart(socket, clientId);
    // Уведомляем GUI - ВСЕГДА отправляем сигнал, даже при переподключении
    locker.unlock(); // Разблокируем перед emit
    // qDebug() << "handleClientHandshake - Mutex unlocked, emitting signals";
    // ВАЖНО: Всегда отправляем clientConnected, даже при переподключении
    // GUI сам разберется, обновить существующую запись или создать новую
    emit clientConnected(clientId, clientInfo);
    emit logMessage(QString("Client %1 connected: %2, process: %3")
                        .arg(clientId).arg(clientInfo).arg(processId));
    // qDebug() << "handleClientHandshake - END";
}

void ServerWorker::onClientDisconnected()
{
    // qDebug()<< QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
    //          <<"onClientDisconnected - Socket:" << sender();
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket){
        // qDebug() << "onClientDisconnected - Invalid socket";
        return;
    }
    // Проверяем причину отключения
    QString errorString = socket->errorString();
    if (!errorString.isEmpty() && errorString != "Unknown error") {
        // emit logMessage("Client disconnected with error: " + errorString);
    }
    QMutexLocker locker(&m_mutex);
    int clientId = m_clients.value(socket, -1);
    if (clientId != -1) {
        QString processId = m_clientToProcessId.value(clientId);
        // qDebug() << "Client disconnected - ID:" << clientId << "Process:" << processId;
        // Удаляем только сокет-специфичные данные, но сохраняем идентификацию клиента
        m_clients.remove(socket);
        // Удаляем из socketMap только если это тот же сокет
        if (m_socketMap.value(clientId) == socket) {
            m_socketMap.remove(clientId);
        }
        m_buffers.remove(socket);
        m_clientLastActivity.remove(clientId); // ← Удаляем из активности
        // qDebug() << "After removal - clients:" << m_clients.size()
        //          << "socketMap:" << m_socketMap.size()
        //          << "buffers:" << m_buffers.size();
        emit clientDisconnected(clientId);
        emit logMessage(QString("Client %1 disconnected, process: %2")
                            .arg(clientId).arg(processId));
    }
    else {
        // qDebug() << "Client not found in m_clients";
    }
    socket->deleteLater();
}

void ServerWorker::processClientData(QTcpSocket *socket, const QJsonObject &json)
{
    // qDebug() << "processClientData - Processing data from client";
    int clientId;
    {
        // Блокируем мьютекс только для получения clientId
        QMutexLocker locker(&m_mutex);
        clientId = m_clients.value(socket, -1);
    }
    {
        // qDebug() << "processClientData - Updating activity for client:" << clientId;
        QMutexLocker locker(&m_mutex);
        m_clientLastActivity[clientId] = QDateTime::currentDateTime();
    }
    if (clientId == -1)
    {
        // qDebug() << "Received data from unregistered client";
        return;
    }
    QString type = json.value("type").toString("Unknown");
    QString content;
    QString timestamp = json.value("timestamp").toString(getTimestamp());
    // Обрабатываем разные типы данных
    if (type == "Heartbeat") {    // Обрабатываем heartbeat запросы
        QJsonObject response;
        response["type"] = "HeartbeatResponse";
        response["client_id"] = clientId;
        response["timestamp"] = getTimestamp();
        QJsonDocument doc(response);
        socket->write(doc.toJson(QJsonDocument::Compact));
        socket->write("\n");
        socket->flush();
        return;
    }
    else if (type == "EchoRequest") {    // Обрабатываем эхо-запросы для измерения RTT
        // qDebug() << "Processing EchoRequest from client" << clientId;
        QString echoData = json.value("data").toString();
        // Отправляем эхо-ответ
        QJsonObject echoResponse;
        echoResponse["type"] = "EchoResponse";
        echoResponse["data"] = echoData;
        echoResponse["timestamp"] = getTimestamp();
        QJsonDocument doc(echoResponse);
        socket->write(doc.toJson(QJsonDocument::Compact));
        socket->write("\n");
        socket->flush();
        return; // Завершаем обработку для эхо-запросов
    }
    else if (type == "ControlResponse") {
        processControlCommand(socket, json);
    }
    else if (type == "NetworkMetrics") {
        // qDebug() << "processClientData NetworkMetrics" << json;
        QJsonObject metrics = json.value("metrics").toObject();
        content = QString("Bandwidth: %1 Mbps, Latency: %2 ms, Packet Loss: %3%")
                      .arg(metrics.value("bandwidth").toDouble())
                      .arg(metrics.value("latency").toDouble())
                      .arg(metrics.value("packet_loss").toDouble());
        // Проверка порогов
        checkThresholds(clientId, type, json);
    }
    else if (type == "DeviceStatus") {
        // qDebug() << "processClientData DeviceStatus" << json;
        QJsonObject status = json.value("status").toObject();
        content = QString("Uptime: %1s, CPU: %2%, Memory: %3%")
                      .arg(status.value("uptime").toInt())
                      .arg(status.value("cpu_usage").toDouble(), 0, 'f', 1)
                      .arg(status.value("memory_usage").toDouble(), 0, 'f', 1);
        checkThresholds(clientId, type, json);
    }
    else if (type == "Log") {
        // qDebug() << "processClientData Log" << json;
        content = QString("[%1] %2: %3")
                      .arg(json.value("severity").toString("INFO"),
                           json.value("message").toString("No message"));
    }
    else {
        content = "Unknown data type: " + type;
        emit logMessage("Received unknown data type: " + type);
    }
    emit dataFromClient(clientId, type, content, timestamp);
    // qDebug() << "processClientData - END for client:" << clientId;
}

QTcpSocket* ServerWorker::getClientSocket(int clientId)
{
    QMutexLocker locker(&m_mutex);
    return m_socketMap.value(clientId, nullptr);
}

void ServerWorker::processControlCommand(QTcpSocket *socket, const QJsonObject &json)
{
    int clientId = m_clients.value(socket, -1);
    if (clientId == -1) return;

    QString status = json.value("status").toString();
    QString command = json.value("command").toString();

    bool enabled = (command == "StartDataExchange");
    emit clientControlStatusChanged(clientId, enabled); // ← Просто emit, без вызова слота

    emit logMessage(QString("Client %1 %2 data exchange: %3")
                        .arg(clientId)
                        .arg(command)
                        .arg(status));
}

void ServerWorker::checkThresholds(int clientId, const QString &type, const QJsonObject &data)
{
    QJsonObject thresholdsCopy;
    {
        // Блокируем мьютекс только для копирования порогов
        QMutexLocker locker(&m_mutex);
        // qDebug() << "=== CHECKING THRESHOLDS ===";
        // qDebug() << "Client:" << clientId << "Type:" << type;
        // qDebug() << "Thresholds empty?" << m_thresholds.isEmpty();
        // qDebug() << "Thresholds content:" << m_thresholds;
        if (m_thresholds.isEmpty()) {
            // qDebug() << "SKIPPING - thresholds are empty";
            return;
        }
        thresholdsCopy = m_thresholds; // Копируем для работы без блокировки
    }
    if (type == "NetworkMetrics") {
        QJsonObject metrics = data.value("metrics").toObject();
        double bandwidth = metrics.value("bandwidth").toDouble();
        double bandwidthThreshold = thresholdsCopy.value("bandwidth").toDouble();
        if ((bandwidth < bandwidthThreshold) &&( bandwidthThreshold > 0)) {
            QString advice = "Consider checking network connection or increasing bandwidth";
            emit thresholdWarning(clientId, "Bandwidth", bandwidth, bandwidthThreshold, advice);
        }
        double latency = metrics.value("latency").toDouble();
        double latencyThreshold = thresholdsCopy.value("latency").toDouble();
        if (latency > latencyThreshold && latencyThreshold > 0) {
            QString advice = "Check network congestion or server load";
            emit thresholdWarning(clientId, "Latency", latency, latencyThreshold, advice);
        }
        double packetLoss = metrics.value("packet_loss").toDouble();
        if (packetLoss > 5.0) { // Стандартный порог для потери пакетов
            QString advice = "Network instability detected, check connections";
            emit thresholdWarning(clientId, "Packet Loss", packetLoss, 5.0, advice);
        }
    }
    else if (type == "DeviceStatus") {
        QJsonObject status = data.value("status").toObject();
        double cpuUsage = status.value("cpu_usage").toDouble();
        double cpuThreshold = thresholdsCopy.value("cpu").toDouble();
        if (cpuUsage > cpuThreshold && cpuThreshold > 0) {
            QString advice = "Consider optimizing processes or upgrading hardware";
            emit thresholdWarning(clientId, "CPU Usage", cpuUsage, cpuThreshold, advice);
        }
        double memoryUsage = status.value("memory_usage").toDouble();
        double memoryThreshold = thresholdsCopy.value("memory").toDouble();
        if (memoryUsage > memoryThreshold && memoryThreshold > 0) {
            QString advice = "Close unused applications or add more RAM";
            emit thresholdWarning(clientId, "Memory Usage", memoryUsage, memoryThreshold, advice);
        }
    }
}

void ServerWorker::sendAck(QTcpSocket *socket, int clientId)
{
    // qDebug()<<QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")<<"sendAck";
    QJsonObject ack;
    ack["type"] = "ConnectionAck";
    ack["status"] = "success";
    ack["client_id"] = clientId;
    ack["message"] = "Connection established";
    QJsonDocument doc(ack);
    socket->write(doc.toJson(QJsonDocument::Compact));
    socket->write("\n");
}

void ServerWorker::sendStart(QTcpSocket *socket, int clientId)
{
    // qDebug()<< QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
    //          <<"sendStart  - Sending start command to client " << clientId;
    QJsonObject startCommand;
    startCommand["type"] = "StartCommand";
    startCommand["status"] = "success";
    startCommand["client_id"] = clientId;
    startCommand["message"] = "Start client commmunication";
    startCommand["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QJsonDocument doc(startCommand);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    data.append('\n');
    qint64 bytesWritten = socket->write(data);
    if (bytesWritten == -1) {
        emit logMessage("Failed to send start command: " + socket->errorString());
    }
    else if (bytesWritten != data.size()) {
        emit logMessage("Incomplete start command sent: " +
            QString::number(bytesWritten) + "/" +
            QString::number(data.size()) + " bytes");
    }
    else {
        emit logMessage("Start command sent to client " + QString::number(clientId));
        // qDebug() << "Start command sent successfully to client" << clientId;
    }
    // Принудительно сбрасываем буфер
    socket->flush();
}

QString ServerWorker::getTimestamp()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
}

QString ServerWorker::getClientInfo(QTcpSocket *socket) const
{
    return QString("%1:%2").arg(socket->peerAddress().toString()).arg(socket->peerPort());
}


```

---


## Файл: server/Server/src/serverthread.cpp

```cpp
#include "../../Server/include/serverthread.h"
#include "../../Server/include/serverworker.h"

#include <QMetaObject>
#include <QEventLoop>

ServerThread::ServerThread(const QJsonObject &initialThresholds, QObject *parent)
    : QThread(parent),
    m_worker(nullptr),
    m_port(0),
    m_startRequested(false),
    m_stopRequested(false),
    m_thresholds(initialThresholds)
{
}

ServerThread::~ServerThread()
{
    stopServer();
    if (isRunning()) {
        quit();
        wait();
    }
}

void ServerThread::startServer(int port)
{
    m_port = port;
    m_startRequested = true;
    m_stopRequested = false;
    if (!isRunning()) {
        start();
    } else if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "startServer",
                                  Qt::QueuedConnection, Q_ARG(int, port));
    }
}

void ServerThread::stopServer()
{
    // qDebug() << "ServerThread::stopServer() - Thread:" << QThread::currentThreadId();
    m_stopRequested=true;
    m_startRequested = false;
    if (m_worker) {
        // qDebug() << "Invoking stopServer on worker...";
        QMetaObject::invokeMethod(m_worker, "stopServer", Qt::QueuedConnection);
        // qDebug() << "Worker stopServer invoked";
    }      
    // qDebug() << "ServerThread stopped (but thread remains alive)";
}

void ServerThread::updateThresholds(const QJsonObject &thresholds)
{
    // qDebug() << "ServerThread::updateThresholds called with:" << thresholds;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "updateThresholds",
                                  Qt::QueuedConnection, Q_ARG(QJsonObject, thresholds));
    } else {
        // qDebug() << "Worker is null, cannot update thresholds";
    }
}

QTcpSocket* ServerThread::getClientSocket(int clientId) const
{
    if (m_worker) {
        QTcpSocket* socket = nullptr;
        QMetaObject::invokeMethod(m_worker, [this, clientId, &socket]() {
            socket = m_worker->getClientSocket(clientId); // ← Используем публичный метод
        }, Qt::BlockingQueuedConnection);
        return socket;
    }
    return nullptr;
}

void ServerThread::run()
{
    // qDebug() << "ServerThread::run() started - Thread:" << QThread::currentThreadId();
    m_worker = new ServerWorker();
    // Проверяем, не запрошена ли остановка перед созданием worker
    if (m_stopRequested) {
        // qDebug() << "Stop requested before worker creation, exiting...";
        if (m_worker) {
            m_worker->deleteLater();
            m_worker = nullptr;
        }
        return;
    }
    // Перенаправляем сигналы от worker'а
    connect(m_worker, &ServerWorker::clientConnected, this, &ServerThread::clientConnected);
    connect(m_worker, &ServerWorker::clientDisconnected, this, &ServerThread::clientDisconnected);
    connect(m_worker, &ServerWorker::dataFromClient, this,
            [this](int id, const QString &type, const QString &content, const QString &timestamp) {
                // qDebug() << "ServerThread::dataFromClient received - ID:"
                //          << id << "Type:" << type << "Content:" << content;
                emit dataFromClient(id, type, content, timestamp);
            });
    connect(m_worker, &ServerWorker::logMessage, this, &ServerThread::logMessage);
    connect(m_worker, &ServerWorker::serverStatus, this, &ServerThread::serverStatus);
    connect(m_worker, &ServerWorker::thresholdWarning, this, &ServerThread::thresholdWarning);
    connect(m_worker, &ServerWorker::clientControlStatusChanged, this, &ServerThread::clientControlStatusChanged);
    // qDebug() << "All signals connected in ServerThread";
    // Сразу после создания worker'а устанавливаем пороги по умолчанию
    if (!m_thresholds.isEmpty()) {
        m_worker->updateThresholds(m_thresholds);
    }
    if (m_startRequested && !m_stopRequested) {
        QMetaObject::invokeMethod(m_worker, "startServer", Qt::QueuedConnection, Q_ARG(int, m_port));
    }
    // qDebug() << "Entering event loop...";
    // Модифицированный event loop с проверкой флага остановки
    QEventLoop loop;
    while (!m_stopRequested) {
        loop.exec(QEventLoop::WaitForMoreEvents | QEventLoop::EventLoopExec);
        if (m_stopRequested) {
            // qDebug() << "Stop requested, breaking event loop";
            break;
        }
        msleep(100);
    }
    // qDebug() << "Event loop exited, cleaning up...";
    // Очистка
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }
    // qDebug() << "ServerThread::run() finished";
}




```

---


## Файл: server/main.cpp

```cpp
#include "GUI/include/mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}

```

---


## Файл: server/GUI/include/mainwindow.h

```cpp
#pragma once

#include <QMainWindow>
#include <QTableWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QJsonObject>
#include <QHBoxLayout>

// Предварительное объявление ServerThread
class ServerThread;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
private slots:
    void onStartStopClicked();
    void onClientConnected(int id, const QString &ip/*, const QString &processId*/);
    void onClientDisconnected(int id);
    void onDataFromClient(int id, const QString &type, const QString &content, const QString &timestamp);
    void onLogMessage(const QString &msg);
    void onServerStatusChanged(const QString &status);
    void onSettingsRequested();
    void onThresholdWarning(int clientId, const QString &metric,
                            double value, double threshold, const QString &advice);
    void onClientControlClicked(int clientId, bool start);
protected:
    // Объявляем метод для обработки события закрытия
    void closeEvent(QCloseEvent *event) override;
private:
    // UI элементы
    QTableWidget *m_clientsTable;
    QTableWidget *m_dataTable;
    QPushButton *m_startStopBtn;
    QPushButton *m_settingsBtn;
    QTextEdit *m_logEdit;
    QTextEdit *m_thresholdsLogEdit;
    QJsonObject m_currentThresholds;
    // Структура для хранения данных клиента
    struct ClientData {
        int clientId;
        QTableWidgetItem* bandwidthItem;
        QTableWidgetItem* latencyItem;
        QTableWidgetItem* packetLossItem;
        QTableWidgetItem* uptimeItem;
        QTableWidgetItem* cpuItem;
        QTableWidgetItem* memoryItem;
        QTableWidgetItem* logItem;
        QTableWidgetItem* timestampItem;
        int row; // Номер строки в таблице
        //флаги превышения порогов
        bool bandwidthThresholdExceeded=false;
        bool latencyThresholdExceeded=false;
        bool cpuThresholdExceeded=false;
        bool memoryThresholdExceeded=false;
        bool dataExchangeEnabled;
    };
    QMap<int, QPushButton*> m_clientControlButtons;
    QMap<int, ClientData> m_clientDataMap; // clientId -> ClientData
    int m_nextRow; // Следующая свободная строка
    ServerThread *m_serverThread;    // Серверный поток
    bool m_running;
    int m_listenPort;

    void setupUi();
    void log(const QString &message);
    void resetClientDataRowColors(ClientData &clientData);
};

```

---


## Файл: server/GUI/include/settingsdialog.h

```cpp
#pragma once

#include <QDialog>
#include <QJsonObject>

class QSpinBox;
class QDoubleSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();
    QJsonObject thresholdsJson() const;
private:
    QDoubleSpinBox *m_cpuThreshold;
    QDoubleSpinBox *m_memoryThreshold;
    QDoubleSpinBox *m_bandwidthThreshold;
    QDoubleSpinBox *m_latencyThreshold;
};

```

---


## Файл: server/GUI/src/settingsdialog.cpp

```cpp
#include "../../GUI/include/settingsdialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Client Threshold Settings");
    setModal(true);
    QFormLayout *layout = new QFormLayout(this);
    // CPU Threshold
    m_cpuThreshold = new QDoubleSpinBox(this);
    m_cpuThreshold->setRange(0, 100);
    m_cpuThreshold->setValue(80);
    m_cpuThreshold->setSuffix("%");
    layout->addRow("CPU Usage Threshold:", m_cpuThreshold);
    // Memory Threshold
    m_memoryThreshold = new QDoubleSpinBox(this);
    m_memoryThreshold->setRange(0, 100);
    m_memoryThreshold->setValue(85);
    m_memoryThreshold->setSuffix("%");
    layout->addRow("Memory Usage Threshold:", m_memoryThreshold);
    // Bandwidth Threshold
    m_bandwidthThreshold = new QDoubleSpinBox(this);
    m_bandwidthThreshold->setRange(0, 1000);
    m_bandwidthThreshold->setValue(800);
    m_bandwidthThreshold->setSuffix(" Mbps");
    layout->addRow("Bandwidth Threshold:", m_bandwidthThreshold);
    // Latency Threshold
    m_latencyThreshold = new QDoubleSpinBox(this);
    m_latencyThreshold->setRange(0, 1000);
    m_latencyThreshold->setValue(100);
    m_latencyThreshold->setSuffix(" ms");
    layout->addRow("Latency Threshold:", m_latencyThreshold);
    // Кнопки
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addRow(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

SettingsDialog::~SettingsDialog() {}

QJsonObject SettingsDialog::thresholdsJson() const
{
    QJsonObject thresholds;
    thresholds["cpu"] = m_cpuThreshold->value();
    thresholds["memory"] = m_memoryThreshold->value();
    thresholds["bandwidth"] = m_bandwidthThreshold->value();
    thresholds["latency"] = m_latencyThreshold->value();
    return thresholds;
}

```

---


## Файл: server/GUI/src/mainwindow.cpp

```cpp
#include "../../GUI/include/mainwindow.h"
#include "../../GUI/include/settingsdialog.h"
#include "../../Server/include/serverthread.h"
#include "../../Server/include/serverworker.h"

#include <QHeaderView>
#include <QDateTime>
#include <QMessageBox>
#include <QLabel>
#include <QFileDialog>
#include <QEventLoop>
#include <QTimer>
#include <QCloseEvent>
#include <QScreen>
#include <QApplication>
#include <QTcpSocket>
#include <QMutexLocker>
#include <QJsonDocument>

namespace {
const int kPort = 12345;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    m_serverThread(nullptr),
    m_running(false),
    m_listenPort(kPort),
    m_nextRow(0)
{
    //  пороги по умолчанию
    m_currentThresholds["cpu"] = 80.0;
    m_currentThresholds["memory"] = 85.0;
    m_currentThresholds["bandwidth"] = 800.0;
    m_currentThresholds["latency"] = 100.0;
    setupUi();
    showMaximized();
    // Создаем серверный поток
    m_serverThread = new ServerThread(m_currentThresholds);
    // Подключаем сигналы
    connect(m_serverThread, &ServerThread::clientConnected, this, &MainWindow::onClientConnected);
    connect(m_serverThread, &ServerThread::clientDisconnected, this, &MainWindow::onClientDisconnected);
    connect(m_serverThread, &ServerThread::dataFromClient, this,
            [this](int id, const QString &type, const QString &content, const QString &timestamp) {
                // qDebug() << "MainWindow::dataFromClient received - ID:" << id << "Type:" << type;
                onDataFromClient(id, type, content, timestamp);
    });
    connect(m_serverThread, &ServerThread::logMessage, this, &MainWindow::onLogMessage);
    connect(m_serverThread, &ServerThread::serverStatus, this, &MainWindow::onServerStatusChanged);
    connect(m_serverThread, &ServerThread::thresholdWarning, this, &MainWindow::onThresholdWarning);    
    log("Application started");
    log("Default thresholds applied");
    // qDebug() << "MainWindow signals connected";
}

MainWindow::~MainWindow()
{
    // Останавливаем сервер но не уничтожаем поток
    if (m_serverThread && m_serverThread->isRunning()) {
        m_serverThread->stopServer();
        m_serverThread->quit();
        m_serverThread->wait();
    }
    // Теперь безопасно удаляем
    delete m_serverThread;
    m_serverThread = nullptr;
}

// Переопределяем closeEvent
void MainWindow::closeEvent(QCloseEvent *event)
{
    // qDebug() << "Main window closing, stopping server...";
    // Останавливаем сервер
    if (m_serverThread && m_serverThread->isRunning()) {
        m_serverThread->stopServer();
        // Ждем завершения потока
        if (!m_serverThread->wait(2000)) {
            qWarning() << "Server thread did not exit properly";
        }
    }
    event->accept();
}

void MainWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    // Панель кнопок
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_startStopBtn = new QPushButton("Start Server", this);
    m_settingsBtn = new QPushButton("Settings", this);
    buttonLayout->addWidget(m_startStopBtn);
    buttonLayout->addWidget(m_settingsBtn);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
    // Таблица клиентов
    m_clientsTable = new QTableWidget(this);
    m_clientsTable->setColumnCount(4); // Увеличиваем количество столбцов
    m_clientsTable->setHorizontalHeaderLabels(QStringList() << "ID" << "IP Address" << "Status" << "Control");
    m_clientsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout->addWidget(m_clientsTable);
    // Таблица данных
    m_dataTable = new QTableWidget(this);
    m_dataTable->setColumnCount(9);
    // Устанавливаем заголовки
    QStringList headers;
    headers << "Client ID"
            << "NetworkMetrics\nBandwidth"
            << "NetworkMetrics\nLatency"
            << "NetworkMetrics\nPacket Loss"
            << "DeviceStatus\nUptime"
            << "DeviceStatus\nCPU"
            << "DeviceStatus\nMemory"
            << "Log"
            << "Timestamp";
    m_dataTable->setHorizontalHeaderLabels(headers);
    // Настраиваем resize mode
    m_dataTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_dataTable->horizontalHeader()->setDefaultSectionSize(120); // Default width
    m_dataTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch); // Log column stretches
    // Дополнительная настройка таблицы данных
    m_dataTable->setAlternatingRowColors(true);
    m_dataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dataTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_dataTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dataTable->verticalHeader()->setVisible(false);
    // Устанавливаем tooltips для заголовков
    m_dataTable->horizontalHeaderItem(1)->setToolTip("Network bandwidth in bytes/sec");
    m_dataTable->horizontalHeaderItem(2)->setToolTip("Network latency in milliseconds");
    m_dataTable->horizontalHeaderItem(3)->setToolTip("Packet loss percentage");
    m_dataTable->horizontalHeaderItem(4)->setToolTip("System uptime in seconds");
    m_dataTable->horizontalHeaderItem(5)->setToolTip("CPU usage percentage");
    m_dataTable->horizontalHeaderItem(6)->setToolTip("Memory usage percentage");
    m_dataTable->horizontalHeaderItem(7)->setToolTip("Log messages");
    m_dataTable->horizontalHeaderItem(8)->setToolTip("Timestamp of last update");
    mainLayout->addWidget(m_dataTable, 3);
    // Область для пороговых логов
    QHBoxLayout *logsLayout = new QHBoxLayout();
    QVBoxLayout *thresholdsLayout = new QVBoxLayout();
    QLabel *thresholdsLabel = new QLabel("Threshold Warnings:", this);
    thresholdsLabel->setMaximumHeight(20);
    m_thresholdsLogEdit = new QTextEdit(this);
    m_thresholdsLogEdit->setReadOnly(true);
    m_thresholdsLogEdit->setMaximumHeight(150);
    thresholdsLayout->addWidget(thresholdsLabel);
    thresholdsLayout->addWidget(m_thresholdsLogEdit);
    QVBoxLayout *serverLogLayout = new QVBoxLayout();
    QLabel *logLabel = new QLabel("Server Log:", this);
    logLabel->setMaximumHeight(20);
    m_logEdit = new QTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumHeight(150);
    serverLogLayout->addWidget(logLabel);
    serverLogLayout->addWidget(m_logEdit);
    logsLayout->addLayout(thresholdsLayout, 1);
    logsLayout->addLayout(serverLogLayout, 1);
    mainLayout->addLayout(logsLayout, 1);
    setCentralWidget(centralWidget);
    // resize(1000, 800);
    setWindowTitle("Network Monitoring Server");
    // Подключаем кнопки
    connect(m_startStopBtn, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::onSettingsRequested);
}

void MainWindow::onStartStopClicked()
{
    if (!m_running) {
        // Запуск сервера
        log("Starting server on port " + QString::number(m_listenPort));
        if (!m_serverThread) {
            // Пересоздаем поток если он был уничтожен
            m_serverThread = new ServerThread();
            // Переподключаем сигналы
            connect(m_serverThread, &ServerThread::clientConnected, this, &MainWindow::onClientConnected);
            connect(m_serverThread, &ServerThread::clientDisconnected, this, &MainWindow::onClientDisconnected);
            connect(m_serverThread, &ServerThread::dataFromClient, this, &MainWindow::onDataFromClient);
            connect(m_serverThread, &ServerThread::logMessage, this, &MainWindow::onLogMessage);
            connect(m_serverThread, &ServerThread::serverStatus, this, &MainWindow::onServerStatusChanged);
            connect(m_serverThread, &ServerThread::thresholdWarning, this, &MainWindow::onThresholdWarning);
        }
        try {
            m_serverThread->startServer(m_listenPort);
            m_running = true;
            m_startStopBtn->setText("Stop Server");
            log("Server start command sent");
            // Проверяем состояние через секунду
            QTimer::singleShot(1000, [this]() {
                if (m_serverThread->isRunning()) {
                    log("Server thread is running");
                } else {
                    log("Server thread failed to start");
                }
            });
        }
        catch (const std::exception& e) {
            log("Error starting server: " + QString(e.what()));
        }
    } else {
 // Остановка сервера
        log("Stopping server...");
        if (m_serverThread) {
            // Перед остановкой сервера обновляем статусы всех клиентов на "Disconnected"
            for (int row = 0; row < m_clientsTable->rowCount(); ++row) {
                if (m_clientsTable->item(row, 2)->text() == "Connected") {
                    m_clientsTable->item(row, 2)->setText("Disconnected");
                    int clientId = m_clientsTable->item(row, 0)->text().toInt();
                    log(QString("Client %1 disconnected due to server stop").arg(clientId));
                }
            }
            m_serverThread->stopServer();
            // НЕ УНИЧТОЖАЕМ поток здесь, только останавливаем
            m_running = false;
            m_startStopBtn->setText("Start Server");
            log("Server stopped successfully");
        }
    }
}


void MainWindow::onClientControlClicked(int clientId, bool start)
{
    // Используем новый публичный метод
    QTcpSocket* socket = m_serverThread->getClientSocket(clientId);

    if (socket && socket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject controlMessage;
        controlMessage["type"] = start ? "StartDataExchange" : "StopDataExchange";
        controlMessage["client_id"] = clientId;
        controlMessage["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        QJsonDocument doc(controlMessage);
        socket->write(doc.toJson(QJsonDocument::Compact));
        socket->write("\n");
        socket->flush();
        // Обновляем кнопку
        QPushButton* btn = m_clientControlButtons.value(clientId);
        if (btn) {
            btn->setText(start ? "Stop" : "Start");
            btn->setProperty("isRunning", start);
        }
        log(QString("Data exchange %1 for client %2").arg(start ? "started" : "stopped").arg(clientId));
    }
}

void MainWindow::onClientConnected(int id, const QString &ip)
{
    // qDebug() << "MainWindow::onClientConnected - ID:" << id << "IP:" << ip;
    // Проверяем, есть ли уже клиент с таким ID
    bool clientExists = false;
    for (int row = 0; row < m_clientsTable->rowCount(); ++row) {
        QTableWidgetItem* idItem = m_clientsTable->item(row, 0);
        if (idItem && idItem->text().toInt() == id) {
            // Обновляем существующего клиента
            m_clientsTable->item(row, 1)->setText(ip);
            // СБРАСЫВАЕМ СТАТУС И ЦВЕТ при переподключении
            QTableWidgetItem* statusItem = m_clientsTable->item(row, 2);
            statusItem->setText("Connected");
            statusItem->setBackground(Qt::green); // ← Явно устанавливаем зеленый цвет
            clientExists = true;
            // qDebug() << "Updated existing client row:" << row;
            // Также сбрасываем флаги превышения порогов в данных клиента
            if (m_clientDataMap.contains(id)) {
                ClientData &clientData = m_clientDataMap[id];
                clientData.bandwidthThresholdExceeded = false;
                clientData.latencyThresholdExceeded = false;
                clientData.cpuThresholdExceeded = false;
                clientData.memoryThresholdExceeded = false;
                clientData.dataExchangeEnabled = true;
                // Сбрасываем цвет ячеек в таблице данных
                resetClientDataRowColors(clientData);
            }
            break;
        }
    }
    if (!clientExists) {
        // Добавляем нового клиента
        int row = m_clientsTable->rowCount();
        m_clientsTable->insertRow(row);
        // ID
        QTableWidgetItem* idItem = new QTableWidgetItem(QString::number(id));
        idItem->setTextAlignment(Qt::AlignCenter);
        m_clientsTable->setItem(row, 0, idItem);
        // IP
        QTableWidgetItem* ipItem = new QTableWidgetItem(ip);
        ipItem->setTextAlignment(Qt::AlignCenter);
        m_clientsTable->setItem(row, 1, ipItem);
        // Status
        QTableWidgetItem* statusItem = new QTableWidgetItem("Connected");
        statusItem->setTextAlignment(Qt::AlignCenter);
        statusItem->setBackground(Qt::green);
        m_clientsTable->setItem(row, 2, statusItem);
        // Control - кнопка управления
        QPushButton* controlBtn = new QPushButton("Stop", this);
        controlBtn->setProperty("clientId", id);
        controlBtn->setProperty("isRunning", true);
        connect(controlBtn, &QPushButton::clicked, this, [this, id, controlBtn]() {
            bool isRunning = controlBtn->property("isRunning").toBool();
            onClientControlClicked(id, !isRunning);
        });
        m_clientsTable->setCellWidget(row, 3, controlBtn);
        m_clientControlButtons[id] = controlBtn;
        // qDebug() << "Added new client at row:" << row;
    }
    log(QString("Client connected - ID: %1, IP: %2").arg(id).arg(ip));
    // Обновляем отображение
    m_clientsTable->viewport()->update();
}

void MainWindow::resetClientDataRowColors(ClientData &clientData)
{
    // Получаем стандартный цвет фона из палитры таблицы
    QBrush defaultBackground = m_dataTable->palette().base();
    // Сбрасываем цвет всех ячеек
    if (clientData.bandwidthItem) {
        clientData.bandwidthItem->setBackground(defaultBackground);
    }
    if (clientData.latencyItem) {
        clientData.latencyItem->setBackground(defaultBackground);
    }
    if (clientData.packetLossItem) {
        clientData.packetLossItem->setBackground(defaultBackground);
    }
    if (clientData.cpuItem) {
        clientData.cpuItem->setBackground(defaultBackground);
    }
    if (clientData.memoryItem) {
        clientData.memoryItem->setBackground(defaultBackground);
    }
    // qDebug() << "Reset colors for client" << clientData.clientId;
}

void MainWindow::onClientDisconnected(int id)
{
    // qDebug() << "MainWindow::onClientDisconnected - ID:" << id;
    for (int row = 0; row < m_clientsTable->rowCount(); ++row) {
        QTableWidgetItem* idItem = m_clientsTable->item(row, 0);
        if (idItem && idItem->text().toInt() == id) {
            QTableWidgetItem* statusItem = m_clientsTable->item(row, 2);
            if (statusItem) {
                statusItem->setText("Disconnected");
                statusItem->setBackground(Qt::red);
                qDebug() << "Updated client" << id << "to Disconnected status at row:" << row;
            }
            log(QString("Client disconnected - ID: %1").arg(id));
            break;
        }
    }
}

void MainWindow::onDataFromClient(int id, const QString &type,
                                  const QString &content, const QString &timestamp)
{
    // qDebug() << "MainWindow::onDataFromClient - ID:"
    //          << id << "Type:" << type << "Content:" << content;
    // Проверяем, есть ли уже данные для этого клиента
    if (!m_clientDataMap.contains(id)) {
        // qDebug() << "Creating new client data entry for ID:" << id;
        // Создаем новую запись для клиента
        ClientData newData;
        newData.clientId = id;
        newData.row = m_nextRow++;
        // Инициализируем флаги
        newData.bandwidthThresholdExceeded = false;
        newData.latencyThresholdExceeded = false;
        newData.cpuThresholdExceeded = false;
        newData.memoryThresholdExceeded = false;
        // Вставляем новую строку
        m_dataTable->insertRow(newData.row);
        // qDebug() << "Inserted new row:" << newData.row;
        // Создаем элементы таблицы
        newData.bandwidthItem = new QTableWidgetItem();
        newData.latencyItem = new QTableWidgetItem();
        newData.packetLossItem = new QTableWidgetItem();
        newData.uptimeItem = new QTableWidgetItem();
        newData.cpuItem = new QTableWidgetItem();
        newData.memoryItem = new QTableWidgetItem();
        newData.logItem = new QTableWidgetItem();
        newData.timestampItem = new QTableWidgetItem();
        // Устанавливаем Client ID
        QTableWidgetItem* idItem = new QTableWidgetItem(QString::number(id));
        idItem->setTextAlignment(Qt::AlignCenter);
        m_dataTable->setItem(newData.row, 0, idItem);
        // Устанавливаем элементы в таблицу
        m_dataTable->setItem(newData.row, 1, newData.bandwidthItem);
        m_dataTable->setItem(newData.row, 2, newData.latencyItem);
        m_dataTable->setItem(newData.row, 3, newData.packetLossItem);
        m_dataTable->setItem(newData.row, 4, newData.uptimeItem);
        m_dataTable->setItem(newData.row, 5, newData.cpuItem);
        m_dataTable->setItem(newData.row, 6, newData.memoryItem);
        m_dataTable->setItem(newData.row, 7, newData.logItem);
        m_dataTable->setItem(newData.row, 8, newData.timestampItem);
        // Сохраняем данные
        m_clientDataMap[id] = newData;
    }
    // Получаем ссылку на данные клиента
    ClientData &clientData = m_clientDataMap[id];
    // Обновляем данные в зависимости от типа
    if (type == "NetworkMetrics") {
        // Парсим метрики сети
        QStringList parts = content.split(", ");
        for (const QString &part : parts) {
            if (part.startsWith("Bandwidth:")) {
                QString valueStr = part.mid(11).split(" ")[0];
                double bandwidth = valueStr.toDouble();
                clientData.bandwidthItem->setText(part.mid(11));
                // Проверка порога bandwidth
                double bandwidthThreshold = m_currentThresholds.value("bandwidth").toDouble();
                if ((bandwidth < bandwidthThreshold) && (bandwidthThreshold > 0)) {
                    clientData.bandwidthItem->setBackground(Qt::red);
                    clientData.bandwidthThresholdExceeded = true;
                } else if (clientData.bandwidthThresholdExceeded) {
                    // СБРАСЫВАЕМ цвет когда значение пришло в норму
                    clientData.bandwidthItem->setBackground(m_dataTable->palette().base());
                    clientData.bandwidthThresholdExceeded = false;
                }
            }
            else if (part.startsWith("Latency:")) {
                QString valueStr = part.mid(9).split(" ")[0];
                double latency = valueStr.toDouble();
                clientData.latencyItem->setText(part.mid(9));
                // Проверка порога latency
                double latencyThreshold = m_currentThresholds.value("latency").toDouble();
                if (latency > latencyThreshold && latencyThreshold > 0) {
                    clientData.latencyItem->setBackground(Qt::red);
                    clientData.latencyThresholdExceeded = true;
                } else if (clientData.latencyThresholdExceeded) {
                    // СБРАСЫВАЕМ цвет
                    clientData.latencyItem->setBackground(m_dataTable->palette().base());
                    clientData.latencyThresholdExceeded = false;
                }
            }
            else if (part.startsWith("Packet Loss:")) {
                QString valueStr = part.mid(13).split("%")[0];
                double packetLoss = valueStr.toDouble();
                clientData.packetLossItem->setText(part.mid(13));
                // Проверка порога packet loss
                if (packetLoss > 5.0) {
                    clientData.packetLossItem->setBackground(Qt::red);
                } else {
                    // СБРАСЫВАЕМ цвет
                    clientData.packetLossItem->setBackground(m_dataTable->palette().base());
                }
            }
        }
    }
    else if (type == "DeviceStatus") {
        // Парсим статус устройства
        QStringList parts = content.split(", ");
        for (const QString &part : parts) {
            if (part.startsWith("Uptime:")) {
                clientData.uptimeItem->setText(part.mid(8));
            }
            else if (part.startsWith("CPU:")) {
                QString valueStr = part.mid(5).split("%")[0];
                double cpu = valueStr.toDouble();
                clientData.cpuItem->setText(part.mid(5));
                // Проверка порога CPU
                double cpuThreshold = m_currentThresholds.value("cpu").toDouble();
                if (cpu > cpuThreshold && cpuThreshold > 0) {
                    clientData.cpuItem->setBackground(Qt::red);
                    clientData.cpuThresholdExceeded = true;
                } else if (clientData.cpuThresholdExceeded) {
                    // СБРАСЫВАЕМ цвет
                    clientData.cpuItem->setBackground(m_dataTable->palette().base());
                    clientData.cpuThresholdExceeded = false;
                }
            }
            else if (part.startsWith("Memory:")) {
                QString valueStr = part.mid(8).split("%")[0];
                double memory = valueStr.toDouble();
                clientData.memoryItem->setText(part.mid(8));
                // Проверка порога Memory
                double memoryThreshold = m_currentThresholds.value("memory").toDouble();
                if (memory > memoryThreshold && memoryThreshold > 0) {
                    clientData.memoryItem->setBackground(Qt::red);
                    clientData.memoryThresholdExceeded = true;
                } else if (clientData.memoryThresholdExceeded) {
                    // СБРАСЫВАЕМ цвет
                    clientData.memoryItem->setBackground(m_dataTable->palette().base());
                    clientData.memoryThresholdExceeded = false;
                }
            }
        }
    }
    else if (type == "Log") {
        // Устанавливаем лог
        clientData.logItem->setText(content);
    }
    // Обновляем timestamp
    clientData.timestampItem->setText(timestamp);
    // Автопрокрутка к последней обновленной строке
    m_dataTable->scrollToItem(clientData.timestampItem);
    // Обновляем отображение
    m_dataTable->viewport()->update();
    // qDebug() << "Data processed for client ID:" << id;
}

void MainWindow::onSettingsRequested()
{
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        m_currentThresholds = dialog.thresholdsJson(); // Сохраняем пороги
        m_serverThread->updateThresholds(m_currentThresholds);
        log("Settings updated");
    }
}

void MainWindow::onThresholdWarning(int clientId, const QString &metric,
                                    double value, double threshold, const QString &advice)
{
    // qDebug() << "Threshold warning received:" << metric << value << threshold;
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString warning = QString("[%1] Client %2: %3 %4 (threshold: %5) - %6")
                          .arg(timestamp)
                          .arg(clientId)
                          .arg(metric)
                          .arg(value)
                          .arg(threshold)
                          .arg(advice);
    m_thresholdsLogEdit->append(warning);
    // Подсветка соответствующих ячеек в таблице
    if (m_clientDataMap.contains(clientId)) {
        ClientData &clientData = m_clientDataMap[clientId];
        if (metric == "Bandwidth") {
            clientData.bandwidthThresholdExceeded = true;
            clientData.bandwidthItem->setBackground(Qt::red);
        }
        else if (metric == "Latency") {
            clientData.latencyThresholdExceeded = true;
            clientData.latencyItem->setBackground(Qt::red);
        }
        else if (metric == "Packet Loss") {
            clientData.packetLossItem->setBackground(Qt::red);
        }
        else if (metric == "CPU Usage") {
            clientData.cpuThresholdExceeded = true;
            clientData.cpuItem->setBackground(Qt::red);
        }
        else if (metric == "Memory Usage") {
            clientData.memoryThresholdExceeded = true;
            clientData.memoryItem->setBackground(Qt::red);
        }
    }
}

void MainWindow::onLogMessage(const QString &msg)
{
    log(msg);
}

void MainWindow::onServerStatusChanged(const QString &status)
{
    log("Server status: " + status);
}

void MainWindow::log(const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    m_logEdit->append("[" + timestamp + "] " + message);
}

```

---


## Файл: client/include/networkmetrics.h

```cpp
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

```

---


## Файл: client/include/loggenerator.h

```cpp
#pragma once

#include <QObject>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStringList>

class LogGenerator : public QObject
{
    Q_OBJECT
public:
    explicit LogGenerator(QObject *parent = nullptr);
    QJsonObject generateLog();
private:
    QRandomGenerator m_random;
    const QStringList m_infoMessages;
    const QStringList m_warningMessages;
    const QStringList m_errorMessages;
    QString generateInfoMessage();
    QString generateWarningMessage();
    QString generateErrorMessage();
    QString generateLongInfoMessage();
    QString generateLongWarningMessage();
    QString generateLongErrorMessage();
};

```

---


## Файл: client/include/client.h

```cpp
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

```

---


## Файл: client/include/devicestatus.h

```cpp
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

```

---


## Файл: client/src/client.cpp

```cpp
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



```

---


## Файл: client/src/loggenerator.cpp

```cpp
#include "../include/loggenerator.h"
#include <QDateTime>

LogGenerator::LogGenerator(QObject *parent)
    : QObject(parent),
    m_random(QRandomGenerator::securelySeeded()),
    m_infoMessages({
        // Короткие сообщения (до 50 символов)
        "System operational",
        "Network connected",
        "Service started",
        "User logged in",
        "Backup completed",
        "Cache cleared",
        "DNS resolved",
        "Security scan OK",
        "Temperature normal",
        "Disk I/O normal",
        // Средние сообщения (50-200 символов)
        "System operating within normal parameters, all services responding appropriately to current load conditions",
        "Network connectivity established successfully with stable bandwidth and minimal packet loss observed",
        "Scheduled maintenance task completed without errors, all components verified functional",
        "User authentication processed successfully with appropriate permissions granted for requested resources",
        "Incremental backup procedure finished, data integrity verified across all storage volumes",
        "Memory cache optimization completed, resulting in improved application response times",
        "Domain name resolution functioning normally with fast response times from configured DNS servers",
        "Security vulnerability scan completed, no critical issues detected in current configuration",
        "System temperature monitoring indicates all components within specified operating ranges",
        "Disk input/output operations performing at expected rates with no latency issues detected"
    }),
    m_warningMessages({
        // Короткие предупреждения
        "CPU usage high",
        "Memory low",
        "Network latency",
        "Disk space low",
        "Temp rising",
        "Cache miss rate up",
        "SSL cert expiring",
        "Load average high",
        "Swap usage increasing",
        "Database slow queries",
        // Средние предупреждения
        "CPU utilization has exceeded warning threshold, consider investigating resource-intensive processes",
        "Available memory is below recommended levels, application performance may be affected soon",
        "Network latency measurements indicate increased response times affecting service quality",
        "Disk storage capacity approaching critical levels, please review and archive unused data",
        "System temperature readings show gradual increase, monitor cooling system effectiveness",
        "Cache hit ratio has dropped below optimal levels, suggesting need for configuration review",
        "SSL certificate expiration approaching within 30 days, schedule renewal to avoid service disruption",
        "System load average consistently above normal levels, indicating potential resource contention",
        "Swap space usage has increased significantly, suggesting physical memory pressure issues",
        "Database query performance degradation detected, recommend index optimization and query review"
    }),
    m_errorMessages({
        // Короткие ошибки
        "Service crashed",
        "Connection failed",
        "Disk write error",
        "Auth failure",
        "Database down",
        "Network timeout",
        "Memory allocation failed",
        "Config corrupted",
        "Permission denied",
        "Hardware fault detected",
        // Средние ошибки
        "Critical service termination detected due to unhandled exception, automatic restart initiated",
        "Network connection attempt failed after multiple retries, check firewall and routing configuration",
        "Disk write operation aborted with I/O error, filesystem integrity should be verified immediately",
        "Authentication service unavailable, user login requests failing across multiple access points",
        "Database connection pool exhausted, unable to establish new connections to backend storage",
        "Network request timeout exceeded maximum threshold, remote service may be unavailable",
        "Memory allocation request failed due to system resource exhaustion, process terminated",
        "Configuration file corruption detected during parsing, using fallback defaults with limited functionality",
        "Permission validation failed for critical system operation, security policy enforcement triggered",
        "Hardware diagnostic routine detected potential failure in storage controller, recommend immediate replacement"
    })
{
}

QJsonObject LogGenerator::generateLog()
{
    int severityChoice = m_random.bounded(100);
    QString severity;
    QString message;
    // Генерируем случайное сообщение с разной длиной
    if (severityChoice < 70) {
        severity = "INFO";
        message = generateInfoMessage();
    } else if (severityChoice < 90) {
        severity = "WARNING";
        message = generateWarningMessage();
    } else {
        severity = "ERROR";
        message = generateErrorMessage();
    }
    QJsonObject logObject;
    logObject["type"] = "Log";
    logObject["message"] = message;
    logObject["severity"] = severity;
    logObject["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return logObject;
}

QString LogGenerator::generateInfoMessage()
{
    int lengthChoice = m_random.bounded(100);
    if (lengthChoice < 40) {
        // Короткие сообщения (30%)
        return m_infoMessages[m_random.bounded(10)];
    } else if (lengthChoice < 80) {
        // Средние сообщения (40%)
        return m_infoMessages[10 + m_random.bounded(10)];
    } else {
        // Длинные сообщения (30%)
        return generateLongInfoMessage();
    }
}

QString LogGenerator::generateWarningMessage()
{
    int lengthChoice = m_random.bounded(100);
    if (lengthChoice < 40) {
        return m_warningMessages[m_random.bounded(10)];
    } else if (lengthChoice < 80) {
        return m_warningMessages[10 + m_random.bounded(10)];
    } else {
        return generateLongWarningMessage();
    }
}

QString LogGenerator::generateErrorMessage()
{
    int lengthChoice = m_random.bounded(100);
    if (lengthChoice < 40) {
        return m_errorMessages[m_random.bounded(10)];
    } else if (lengthChoice < 80) {
        return m_errorMessages[10 + m_random.bounded(10)];
    } else {
        return generateLongErrorMessage();
    }
}

QString LogGenerator::generateLongInfoMessage()
{
    static const QStringList longInfoTemplates = {
        "System performance metrics indicate optimal operation across all monitored parameters. "
        "Network throughput remains consistent at %1 Mbps with latency measurements below %2 ms. "
        "CPU utilization averaging %3% across all cores, memory allocation efficiently managed with %4% "
        "cache hit rate. Storage subsystems responding within expected timeframes, no I/O bottlenecks detected. "
        "All scheduled maintenance tasks completed successfully, next routine check scheduled for %5.",

        "Comprehensive system health check completed successfully. All services operating within specified "
        "performance envelopes, security protocols actively enforced, and resource allocation optimized "
        "for current workload patterns. Network connectivity stable with %1 active connections, "
        "bandwidth utilization at %2% capacity. Storage integrity verified across %3 volumes, "
        "totaling %4 TB of available space. Environmental controls maintaining optimal operating "
        "conditions with temperature at %5°C and humidity at %6%.",

        "Extended diagnostic analysis reveals system operating at peak efficiency. Application response "
        "times averaging %1 ms, database queries executing in %2 ms, and network packet delivery "
        "achieving %3% success rate. Resource consumption patterns indicate balanced load distribution "
        "across %4 server instances, with failover mechanisms tested and verified operational. "
        "Security audit completed, all vulnerabilities patched and intrusion detection systems active."
    };

    QString templateMsg = longInfoTemplates[m_random.bounded(longInfoTemplates.size())];
    return templateMsg.arg(m_random.bounded(500) + 100)
        .arg(m_random.bounded(50) + 10)
        .arg(m_random.bounded(30) + 10)
        .arg(m_random.bounded(20) + 75)
        .arg(QDateTime::currentDateTime().addDays(m_random.bounded(7) + 1).toString("yyyy-MM-dd"))
        .arg(m_random.bounded(20) + 40);
}

QString LogGenerator::generateLongWarningMessage()
{
    static const QStringList longWarningTemplates = {
        "Sustained elevated resource utilization detected across multiple system components. "
        "CPU usage consistently above %1% threshold for past %2 minutes, memory consumption "
        "showing gradual increase to %3% of available capacity. Network latency measurements "
        "indicate periodic spikes reaching %4 ms, potentially affecting application responsiveness. "
        "Recommend reviewing active processes and considering load distribution optimization.",

        "Performance degradation trends observed in storage subsystem. Disk I/O wait times "
        "increased by %1% compared to baseline, read/write operations experiencing occasional "
        "timeouts. Filesystem fragmentation at %2%, potentially contributing to reduced efficiency. "
        "Storage capacity forecast indicates critical levels may be reached within %3 days "
        "at current growth rate. Immediate capacity planning and archival procedures recommended.",

        "Network connectivity analysis reveals intermittent stability issues. Packet loss "
        "measurements show periodic spikes up to %1%, primarily affecting UDP-based services. "
        "Routing path analysis indicates %2 hop count increase with corresponding latency "
        "impact. DNS resolution times elevated by %3 ms average. Recommend network infrastructure "
        "review and potential carrier circuit evaluation."
    };
    QString templateMsg = longWarningTemplates[m_random.bounded(longWarningTemplates.size())];
    return templateMsg.arg(m_random.bounded(20) + 75)
        .arg(m_random.bounded(30) + 5)
        .arg(m_random.bounded(15) + 80)
        .arg(m_random.bounded(100) + 50)
        .arg(m_random.bounded(10) + 3);
}

QString LogGenerator::generateLongErrorMessage()
{
    static const QStringList longErrorTemplates = {
        "CRITICAL SYSTEM FAILURE: Multiple redundancy failures detected in primary storage array. "
        "RAID controller reporting %1 disk failures across %2 drive group, data integrity "
        "compromised on %3 logical volumes. Automatic failover to secondary storage unsuccessful "
        "due to %4. Immediate manual intervention required to prevent data loss. Emergency "
        "procedures initiated, estimated recovery time: %5 minutes.",

        "NETWORK INFRASTRUCTURE FAILURE: Core routing equipment experiencing complete service outage. "
        "Primary and secondary uplinks disconnected, BGP sessions dropped, and internal routing "
        "protocols non-responsive. Impact analysis shows %1% of services affected with estimated "
        "%2 customers unable to connect. Redundant systems failed to activate due to %3. "
        "Emergency response team activated, ETA for partial restoration: %4 minutes.",

        "SECURITY BREACH DETECTED: Intrusion prevention system identified sophisticated multi-vector "
        "attack pattern targeting %1 vulnerability. %2 unauthorized access attempts recorded "
        "from %3 distinct IP addresses over past %4 minutes. Data exfiltration detected affecting "
        "%5 database tables. Automatic containment procedures initiated, incident response team "
        "notified, and regulatory compliance reporting procedures activated."
    };
    QString templateMsg = longErrorTemplates[m_random.bounded(longErrorTemplates.size())];
    return templateMsg.arg(m_random.bounded(3) + 1)
        .arg(m_random.bounded(4) + 2)
        .arg(m_random.bounded(5) + 1)
        .arg(QStringList{"configuration mismatch", "hardware failure", "software bug", "network partition"}[m_random.bounded(4)])
        .arg(m_random.bounded(120) + 30)
        .arg(m_random.bounded(20) + 5);
}

```

---


## Файл: client/src/networkmetrics.cpp

```cpp
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

```

---


## Файл: client/src/devicestatus.cpp

```cpp
#include "../include/devicestatus.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>

#ifdef Q_OS_LINUX
#include <sys/sysinfo.h>
#endif

DeviceStatus::DeviceStatus(QObject *parent)
    : QObject(parent),
    m_uptime(0),
    m_cpuUsage(0),
    m_memoryUsage(0),
    m_lastCpuTotal(0),
    m_lastCpuIdle(0)
{
    m_lastCpuUpdate = QDateTime::currentDateTime();
}

void DeviceStatus::updateStatus()
{
    m_uptime = getUptime();
    m_cpuUsage = getCpuUsage();
    m_memoryUsage = getMemoryUsage();
}

QJsonObject DeviceStatus::toJson() const
{
    QJsonObject status;
    status["uptime"] = m_uptime;
    status["cpu_usage"] = m_cpuUsage;
    status["memory_usage"] = m_memoryUsage;
    return status;
}

qint64 DeviceStatus::getUptime()
{
#ifdef Q_OS_LINUX
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.uptime;
    }
#endif
    return 0;
}

double DeviceStatus::getCpuUsage()
{
    qint64 total = 0;
    qint64 idle = 0;
#ifdef Q_OS_LINUX
    getLinuxCpuUsage(total, idle);
#endif

    if (m_lastCpuTotal > 0 && total > m_lastCpuTotal) {
        qint64 totalDiff = total - m_lastCpuTotal;
        qint64 idleDiff = idle - m_lastCpuIdle;

        if (totalDiff > 0) {
            m_lastCpuTotal = total;
            m_lastCpuIdle = idle;
            m_lastCpuUpdate = QDateTime::currentDateTime();
            return 100.0 * (1.0 - static_cast<double>(idleDiff) / totalDiff);
        }
    }

    m_lastCpuTotal = total;
    m_lastCpuIdle = idle;
    m_lastCpuUpdate = QDateTime::currentDateTime();
    return 0;
}

double DeviceStatus::getMemoryUsage()
{
#ifdef Q_OS_LINUX
    QFile meminfo("/proc/meminfo");
    if (!meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open /proc/meminfo";
        return 0.0;
    }
    // qDebug() << "File opened successfully. Size:" << meminfo.size();
    QByteArray data = meminfo.readAll();
    QString content = QString::fromLocal8Bit(data);
    // qDebug() << "File content:" << content.left(200) << "..."; // Первые 200 символов
    QStringList lines = content.split('\n', Qt::SkipEmptyParts);
    qint64 totalMem = 0;
    qint64 freeMem = 0;
    for (const QString &line : lines) {
        if (line.startsWith("MemTotal:")) {
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                totalMem = parts[1].toLongLong();
                // qDebug() << "MemTotal:" << totalMem;
            }
        } else if (line.startsWith("MemAvailable:")) {
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                freeMem = parts[1].toLongLong();
                // qDebug() << "MemAvailable:" << freeMem;
                break; // Нашли оба значения, можно выходить
            }
        }
    }

    meminfo.close();

    if (totalMem > 0 && freeMem >= 0) {
        double usage = 100.0 * (1.0 - static_cast<double>(freeMem) / totalMem);
        // qDebug() << "Memory usage:" << usage << "%";
        return usage;
    }

    qDebug() << "Failed to parse memory info";
#endif
    return 0.0;
}

void DeviceStatus::getLinuxCpuUsage(qint64 &total, qint64 &idle)
{
    QFile statFile("/proc/stat");
    if (statFile.open(QIODevice::ReadOnly)) {
        QTextStream in(&statFile);
        QString line = in.readLine();
        if (line.startsWith("cpu ")) {
            QStringList values = line.split(" ", Qt::SkipEmptyParts);
            if (values.size() >= 5) {
                total = 0;
                for (int i = 1; i < values.size(); i++) {
                    total += values[i].toLongLong();
                }
                idle = values[4].toLongLong();
            }
        }
    }
}

```

---


## Файл: client/main.cpp

```cpp
#include "include/client.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    // Настройка парсера командной строки
    QCommandLineParser parser;
    parser.setApplicationDescription("Network Monitoring Client");
    parser.addHelpOption();
    // Добавляем опции для адреса и порта
    QCommandLineOption addressOption(
        QStringList() << "a" << "address",
        QCoreApplication::translate("main", "Server address <address>."),
        QCoreApplication::translate("main", "address"),
        "localhost"
        );
    QCommandLineOption portOption(
        QStringList() << "p" << "port",
        QCoreApplication::translate("main", "Server port <port>."),
        QCoreApplication::translate("main", "port"),
        "12345"
        );
    parser.addOption(addressOption);
    parser.addOption(portOption);
    parser.process(app);
    // Получаем параметры из командной строки
    QString serverAddress = parser.value(addressOption);
    quint16 serverPort = parser.value(portOption).toUShort();
    NetworkClient client;
    QObject::connect(&client, &NetworkClient::finished, &app, &QCoreApplication::quit);
    // Запускаем клиент с указанными параметрами
    QTimer::singleShot(0, [&client, serverAddress, serverPort]() {
        client.start(serverAddress, serverPort);
    });
    return app.exec();
}

```

---


