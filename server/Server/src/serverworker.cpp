#include "../../Server/include/serverworker.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
#include <QHostAddress>
#include <QDebug>
#include <QThread>

/**
 * @brief Конструктор ServerWorker
 * @details Инициализирует серверный воркер, устанавливает начальные значения
 *          и запускает таймер для проверки активности клиентов
 * @param parent Родительский QObject
 */
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

/**
 * @brief Деструктор ServerWorker
 * @details Останавливает таймер и сервер, освобождает ресурсы
 */
ServerWorker::~ServerWorker()
{
    // qDebug()<< QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
    //     <<"destructor serverWorker";
    m_heartbeatTimer->stop();
    stopServer();
}

/**
 * @brief Проверяет соединения с клиентами
 * @details Вызывается периодически для проверки активности клиентов,
 *          отключает неактивных клиентов и очищает связанные ресурсы
 */
void ServerWorker::checkClientConnections()
{
    // qDebug() << "=== checkClientConnections START ===";
    // qDebug() << "Attempting to lock mutex...";
    QList<QTcpSocket*> disconnectedSockets;
    QList<int> disconnectedClientIds;
    QMutexLocker locker(&m_mutex);
    // qDebug() << "Mutex locked successfully";
    // qDebug() << "Active clients:" << m_clients.size();
    // qDebug() << "Socket map size:" << m_socketMap.size();
    QDateTime currentTime = QDateTime::currentDateTime();
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

/**
 * @brief Принудительно обрабатывает отключение клиента
 * @details Вызывается при обнаружении неактивного клиента,
 *          очищает все связанные с клиентом ресурсы
 * @param socket Сокет отключившегося клиента
 */
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

/**
 * @brief Запускает сервер на указанном порту
 * @details Проверяет валидность порта, устанавливает прослушивание
 *          и обновляет статус сервера
 * @param port Порт для прослушивания подключений
 */
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

/**
 * @brief Останавливает сервер
 * @details Закрывает все соединения с клиентами, останавливает прослушивание
 *          и очищает внутренние структуры данных
 */
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

/**
 * @brief Обновляет значения порогов
 * @details Устанавливает новые значения порогов для проверки метрик клиентов
 * @param thresholds Новые значения порогов в формате JSON
 */
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
 * @details Создает сокет для нового клиента и настраивает обработчики
 *          для чтения данных и отключения
 * @param socketDescriptor Дескриптор сокета нового подключения
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
 * @details Читает и парсит данные от клиента, обрабатывает handshake
 *          и обычные сообщения в зависимости от состояния клиента
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
 * @details Регистрирует нового клиента или обрабатывает переподключение,
 *          отправляет подтверждение и команду начала работы
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

/**
 * @brief Обработчик отключения клиента
 * @details Вызывается при нормальном отключении клиента,
 *          очищает связанные ресурсы и уведомляет об отключении
 */
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

/**
 * @brief Обрабатывает данные от клиента
 * @details Разбирает полученные данные, обновляет время активности
 *          и обрабатывает различные типы сообщений
 * @param socket Сокет клиента
 * @param json JSON объект с данными от клиента
 */
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

/**
 * @brief Получает сокет клиента по его ID
 * @details Возвращает сокет клиента для заданного ID с защитой мьютексом
 * @param clientId ID клиента
 * @return Указатель на QTcpSocket клиента или nullptr если не найден
 */
QTcpSocket* ServerWorker::getClientSocket(int clientId)
{
    QMutexLocker locker(&m_mutex);
    return m_socketMap.value(clientId, nullptr);
}

/**
 * @brief Обрабатывает команду управления от клиента
 * @details Обрабатывает ответы на команды управления обменом данными
 * @param socket Сокет клиента
 * @param json JSON объект с командой управления
 */
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

/**
 * @brief Проверяет метрики на превышение порогов
 * @details Сравнивает значения метрик с установленными порогами
 *          и генерирует предупреждения при превышении
 * @param clientId ID клиента
 * @param type Тип данных (NetworkMetrics/DeviceStatus)
 * @param data JSON объект с данными метрик
 */
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

/**
 * @brief Отправляет подтверждение подключения клиенту
 * @details Создает и отправляет ACK сообщение с ID клиента
 * @param socket Сокет клиента
 * @param clientId ID клиента
 */
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

/**
 * @brief Отправляет команду начала работы клиенту
 * @details Создает и отправляет команду начала обмена данными
 * @param socket Сокет клиента
 * @param clientId ID клиента
 */
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

/**
 * @brief Генерирует временную метку
 * @details Создает строку с текущим временем в стандартном формате
 * @return Строка с временной меткой
 */
QString ServerWorker::getTimestamp()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
}

/**
 * @brief Получает информацию о клиенте
 * @details Форматирует строку с IP и портом клиента
 * @param socket Сокет клиента
 * @return Строка с информацией о клиенте
 */
QString ServerWorker::getClientInfo(QTcpSocket *socket) const
{
    return QString("%1:%2").arg(socket->peerAddress().toString()).arg(socket->peerPort());
}
