#include "../../GUI/include/settingsdialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>

/**
 * @brief Конструктор SettingsDialog
 * @details Создает диалоговое окно настроек порогов с элементами управления
 *          для установки значений CPU, памяти, bandwidth и latency
 * @param parent Родительский виджет
 */
SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Client Threshold Settings");
    setModal(true);

    // Создаем layout для размещения элементов
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

    // Кнопки OK и Cancel
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addRow(buttonBox);

    // Подключаем сигналы кнопок
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

/**
 * @brief Деструктор SettingsDialog
 * @details Освобождает ресурсы, связанные с диалоговым окном
 */
SettingsDialog::~SettingsDialog() {}

/**
 * @brief Получает текущие значения порогов в формате JSON
 * @details Собирает значения из всех spinbox'ов и формирует
 *          JSON объект с соответствующими ключами
 * @return QJsonObject с текущими значениями порогов
 */
QJsonObject SettingsDialog::thresholdsJson() const
{
    QJsonObject thresholds;
    thresholds["cpu"] = m_cpuThreshold->value();
    thresholds["memory"] = m_memoryThreshold->value();
    thresholds["bandwidth"] = m_bandwidthThreshold->value();
    thresholds["latency"] = m_latencyThreshold->value();
    return thresholds;
}
