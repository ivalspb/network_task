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
