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
