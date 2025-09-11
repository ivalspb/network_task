#include "../include/devicestatus.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>

#ifdef Q_OS_LINUX
#include <sys/sysinfo.h>
#endif

/**
 * @brief Конструктор DeviceStatus
 * @details Инициализирует метрики устройства нулевыми значениями
 *          и устанавливает начальное время обновления CPU
 * @param parent Родительский QObject
 */
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

/**
 * @brief Обновляет все метрики статуса устройства
 * @details Вызывает методы получения uptime, CPU usage и memory usage
 *          для актуализации текущих значений
 */
void DeviceStatus::updateStatus()
{
    m_uptime = getUptime();
    m_cpuUsage = getCpuUsage();
    m_memoryUsage = getMemoryUsage();
}

/**
 * @brief Преобразует метрики статуса в JSON объект
 * @details Создает JSON объект с текущими значениями uptime,
 *          CPU usage и memory usage
 * @return QJsonObject с метриками статуса устройства
 */
QJsonObject DeviceStatus::toJson() const
{
    QJsonObject status;
    status["uptime"] = m_uptime;
    status["cpu_usage"] = m_cpuUsage;
    status["memory_usage"] = m_memoryUsage;
    return status;
}

/**
 * @brief Получает время работы системы (uptime)
 * @details Использует системные вызовы для получения времени
 *          с последней перезагрузки системы
 * @return Время работы системы в секундах, или 0 при ошибке
 */
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

/**
 * @brief Получает текущую загрузку CPU
 * @details Читает статистику использования CPU из /proc/stat,
 *          вычисляет процент использования на основе разницы
 *          между текущими и предыдущими значениями
 * @return Процент использования CPU (0-100), или 0 при ошибке
 */
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

/**
 * @brief Получает текущее использование памяти
 * @details Читает информацию о памяти из /proc/meminfo,
 *          вычисляет процент использования на основе
 *          доступной и общей памяти
 * @return Процент использования памяти (0-100), или 0 при ошибке
 */
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

/**
 * @brief Получает статистику использования CPU для Linux
 * @details Читает первую строку из /proc/stat для получения
 *          общей статистики использования CPU системой
 * @param total Ссылка для возврата общего времени работы CPU
 * @param idle Ссылка для возврата времени простоя CPU
 */
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
