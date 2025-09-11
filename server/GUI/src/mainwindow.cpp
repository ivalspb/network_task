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

/**
 * @brief Конструктор MainWindow
 * @details Инициализирует главное окно сервера, устанавливает пороги по умолчанию,
 *          настраивает UI, создает серверный поток и подключает сигналы
 * @param parent Родительский виджет
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    m_serverThread(nullptr),
    m_running(false),
    m_listenPort(kPort),
    m_nextRow(0)
{
    // пороги по умолчанию
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

/**
 * @brief Деструктор MainWindow
 * @details Останавливает сервер, дожидается завершения потока
 *          и освобождает ресурсы
 */
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

/**
 * @brief Обработчик события закрытия окна
 * @details Останавливает сервер и дожидается завершения потока
 *          перед закрытием приложения
 * @param event Событие закрытия
 */
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

/**
 * @brief Настраивает пользовательский интерфейс
 * @details Создает и размещает все элементы UI: кнопки, таблицы,
 *          текстовые поля и настраивает их свойства
 */
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

/**
 * @brief Обработчик нажатия кнопки Start/Stop
 * @details Запускает или останавливает сервер в зависимости от текущего состояния,
 *          обновляет статусы клиентов при остановке
 */
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

/**
 * @brief Обработчик нажатия кнопки управления клиентом
 * @details Отправляет команду Start/Stop data exchange выбранному клиенту
 * @param clientId ID клиента
 * @param start Флаг начала (true) или остановки (false) обмена данными
 */
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

/**
 * @brief Обработчик подключения нового клиента
 * @details Добавляет клиента в таблицу или обновляет существующую запись,
 *          сбрасывает флаги превышения порогов при переподключении
 * @param id ID клиента
 * @param ip IP адрес клиента
 */
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

/**
 * @brief Сбрасывает цвета строки данных клиента
 * @details Восстанавливает стандартные цвета фона для всех ячеек
 *          в строке данных указанного клиента
 * @param clientData Данные клиента для сброса цветов
 */
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

/**
 * @brief Обработчик отключения клиента
 * @details Обновляет статус клиента в таблице на "Disconnected"
 *          и изменяет цвет фона на красный
 * @param id ID отключившегося клиента
 */
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
            log(QString("Client %1 disconnected - ID: %2").arg(id));
            break;
        }
    }
}

/**
 * @brief Обработчик получения данных от клиента
 * @details Обновляет таблицу данных на основе полученной информации,
 *          проверяет пороги и подсвечивает превышения
 * @param id ID клиента
 * @param type Тип данных (NetworkMetrics/DeviceStatus/Log)
 * @param content Содержание данных
 * @param timestamp Временная метка данных
 */
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

/**
 * @brief Обработчик запроса настроек
 * @details Открывает диалог настроек порогов и применяет новые значения
 */
void MainWindow::onSettingsRequested()
{
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        m_currentThresholds = dialog.thresholdsJson(); // Сохраняем пороги
        m_serverThread->updateThresholds(m_currentThresholds);
        log("Settings updated");
    }
}

/**
 * @brief Обработчик предупреждений о превышении порогов
 * @details Добавляет предупреждение в лог порогов и подсвечивает
 *          соответствующие ячейки в таблице данных
 * @param clientId ID клиента
 * @param metric Метрика, вызвавшая предупреждение
 * @param value Текущее значение метрики
 * @param threshold Значение порога
 * @param advice Рекомендация по устранению проблемы
 */
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

/**
 * @brief Обработчик лог-сообщений от сервера
 * @details Добавляет сообщение в лог сервера
 * @param msg Текст сообщения
 */
void MainWindow::onLogMessage(const QString &msg)
{
    log(msg);
}

/**
 * @brief Обработчик изменения статуса сервера
 * @details Добавляет сообщение о статусе сервера в лог
 * @param status Текст статуса сервера
 */
void MainWindow::onServerStatusChanged(const QString &status)
{
    log("Server status: " + status);
}

/**
 * @brief Добавляет сообщение в лог сервера
 * @details Форматирует сообщение с временной меткой и добавляет его
 *          в текстовое поле лога
 * @param message Текст сообщения для логирования
 */
void MainWindow::log(const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    m_logEdit->append("[" + timestamp + "] " + message);
}
