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
