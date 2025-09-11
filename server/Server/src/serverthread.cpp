#include "../../Server/include/serverthread.h"
#include "../../Server/include/serverworker.h"

#include <QMetaObject>
#include <QEventLoop>

/**
 * @brief Конструктор ServerThread
 * @details Инициализирует серверный поток с начальными порогами
 * @param initialThresholds Начальные значения порогов в формате JSON
 * @param parent Родительский QObject
 */
ServerThread::ServerThread(const QJsonObject &initialThresholds, QObject *parent)
    : QThread(parent),
    m_worker(nullptr),
    m_port(0),
    m_startRequested(false),
    m_stopRequested(false),
    m_thresholds(initialThresholds)
{
}

/**
 * @brief Деструктор ServerThread
 * @details Останавливает сервер и дожидается завершения потока
 */
ServerThread::~ServerThread()
{
    stopServer();
    if (isRunning()) {
        quit();
        wait();
    }
}

/**
 * @brief Запускает сервер на указанном порту
 * @details Устанавливает параметры и запускает поток, если он не запущен,
 *          или отправляет команду запуска работающему воркеру
 * @param port Порт для прослушивания подключений
 */
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

/**
 * @brief Останавливает сервер
 * @details Устанавливает флаг остановки и отправляет команду
 *          остановки работающему воркеру
 */
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

/**
 * @brief Обновляет значения порогов
 * @details Отправляет новые значения порогов работающему воркеру
 * @param thresholds Новые значения порогов в формате JSON
 */
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

/**
 * @brief Получает сокет клиента по его ID
 * @details Запрашивает сокет клиента у воркера с использованием
 *          блокирующего соединения для thread-safe доступа
 * @param clientId ID клиента
 * @return Указатель на QTcpSocket клиента или nullptr если не найден
 */
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

/**
 * @brief Основной метод выполнения потока
 * @details Создает ServerWorker, настраивает соединения сигналов,
 *          устанавливает начальные пороги и запускает event loop
 */
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
