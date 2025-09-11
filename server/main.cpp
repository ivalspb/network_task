#include "GUI/include/mainwindow.h"
#include <QApplication>

/**
 * @brief Точка входа серверного приложения
 * @details Инициализирует Qt приложение, создает и отображает главное окно,
 *          запускает главный цикл обработки событий
 * @param argc Количество аргументов командной строки
 * @param argv Массив аргументов командной строки
 * @return Код возврата приложения (0 - успешное завершение)
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
