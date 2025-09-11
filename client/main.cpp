#include "include/client.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTimer>

/**
 * @brief Точка входа клиентского приложения
 * @details Инициализирует приложение, парсит аргументы командной строки,
 *          создает и запускает сетевого клиента
 * @param argc Количество аргументов командной строки
 * @param argv Массив аргументов командной строки
 * @return Код возврата приложения (0 - успешное завершение)
 */
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
