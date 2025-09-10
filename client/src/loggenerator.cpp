#include "../include/loggenerator.h"
#include <QDateTime>

LogGenerator::LogGenerator(QObject *parent)
    : QObject(parent),
    m_random(QRandomGenerator::securelySeeded()),
    m_infoMessages({
        // Короткие сообщения (до 50 символов)
        "System operational",
        "Network connected",
        "Service started",
        "User logged in",
        "Backup completed",
        "Cache cleared",
        "DNS resolved",
        "Security scan OK",
        "Temperature normal",
        "Disk I/O normal",
        // Средние сообщения (50-200 символов)
        "System operating within normal parameters, all services responding appropriately to current load conditions",
        "Network connectivity established successfully with stable bandwidth and minimal packet loss observed",
        "Scheduled maintenance task completed without errors, all components verified functional",
        "User authentication processed successfully with appropriate permissions granted for requested resources",
        "Incremental backup procedure finished, data integrity verified across all storage volumes",
        "Memory cache optimization completed, resulting in improved application response times",
        "Domain name resolution functioning normally with fast response times from configured DNS servers",
        "Security vulnerability scan completed, no critical issues detected in current configuration",
        "System temperature monitoring indicates all components within specified operating ranges",
        "Disk input/output operations performing at expected rates with no latency issues detected"
    }),
    m_warningMessages({
        // Короткие предупреждения
        "CPU usage high",
        "Memory low",
        "Network latency",
        "Disk space low",
        "Temp rising",
        "Cache miss rate up",
        "SSL cert expiring",
        "Load average high",
        "Swap usage increasing",
        "Database slow queries",
        // Средние предупреждения
        "CPU utilization has exceeded warning threshold, consider investigating resource-intensive processes",
        "Available memory is below recommended levels, application performance may be affected soon",
        "Network latency measurements indicate increased response times affecting service quality",
        "Disk storage capacity approaching critical levels, please review and archive unused data",
        "System temperature readings show gradual increase, monitor cooling system effectiveness",
        "Cache hit ratio has dropped below optimal levels, suggesting need for configuration review",
        "SSL certificate expiration approaching within 30 days, schedule renewal to avoid service disruption",
        "System load average consistently above normal levels, indicating potential resource contention",
        "Swap space usage has increased significantly, suggesting physical memory pressure issues",
        "Database query performance degradation detected, recommend index optimization and query review"
    }),
    m_errorMessages({
        // Короткие ошибки
        "Service crashed",
        "Connection failed",
        "Disk write error",
        "Auth failure",
        "Database down",
        "Network timeout",
        "Memory allocation failed",
        "Config corrupted",
        "Permission denied",
        "Hardware fault detected",
        // Средние ошибки
        "Critical service termination detected due to unhandled exception, automatic restart initiated",
        "Network connection attempt failed after multiple retries, check firewall and routing configuration",
        "Disk write operation aborted with I/O error, filesystem integrity should be verified immediately",
        "Authentication service unavailable, user login requests failing across multiple access points",
        "Database connection pool exhausted, unable to establish new connections to backend storage",
        "Network request timeout exceeded maximum threshold, remote service may be unavailable",
        "Memory allocation request failed due to system resource exhaustion, process terminated",
        "Configuration file corruption detected during parsing, using fallback defaults with limited functionality",
        "Permission validation failed for critical system operation, security policy enforcement triggered",
        "Hardware diagnostic routine detected potential failure in storage controller, recommend immediate replacement"
    })
{
}

QJsonObject LogGenerator::generateLog()
{
    int severityChoice = m_random.bounded(100);
    QString severity;
    QString message;
    // Генерируем случайное сообщение с разной длиной
    if (severityChoice < 70) {
        severity = "INFO";
        message = generateInfoMessage();
    } else if (severityChoice < 90) {
        severity = "WARNING";
        message = generateWarningMessage();
    } else {
        severity = "ERROR";
        message = generateErrorMessage();
    }
    QJsonObject logObject;
    logObject["type"] = "Log";
    logObject["message"] = message;
    logObject["severity"] = severity;
    logObject["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return logObject;
}

QString LogGenerator::generateInfoMessage()
{
    int lengthChoice = m_random.bounded(100);
    if (lengthChoice < 40) {
        // Короткие сообщения (30%)
        return m_infoMessages[m_random.bounded(10)];
    } else if (lengthChoice < 80) {
        // Средние сообщения (40%)
        return m_infoMessages[10 + m_random.bounded(10)];
    } else {
        // Длинные сообщения (30%)
        return generateLongInfoMessage();
    }
}

QString LogGenerator::generateWarningMessage()
{
    int lengthChoice = m_random.bounded(100);
    if (lengthChoice < 40) {
        return m_warningMessages[m_random.bounded(10)];
    } else if (lengthChoice < 80) {
        return m_warningMessages[10 + m_random.bounded(10)];
    } else {
        return generateLongWarningMessage();
    }
}

QString LogGenerator::generateErrorMessage()
{
    int lengthChoice = m_random.bounded(100);
    if (lengthChoice < 40) {
        return m_errorMessages[m_random.bounded(10)];
    } else if (lengthChoice < 80) {
        return m_errorMessages[10 + m_random.bounded(10)];
    } else {
        return generateLongErrorMessage();
    }
}

QString LogGenerator::generateLongInfoMessage()
{
    static const QStringList longInfoTemplates = {
        "System performance metrics indicate optimal operation across all monitored parameters. "
        "Network throughput remains consistent at %1 Mbps with latency measurements below %2 ms. "
        "CPU utilization averaging %3% across all cores, memory allocation efficiently managed with %4% "
        "cache hit rate. Storage subsystems responding within expected timeframes, no I/O bottlenecks detected. "
        "All scheduled maintenance tasks completed successfully, next routine check scheduled for %5.",

        "Comprehensive system health check completed successfully. All services operating within specified "
        "performance envelopes, security protocols actively enforced, and resource allocation optimized "
        "for current workload patterns. Network connectivity stable with %1 active connections, "
        "bandwidth utilization at %2% capacity. Storage integrity verified across %3 volumes, "
        "totaling %4 TB of available space. Environmental controls maintaining optimal operating "
        "conditions with temperature at %5°C and humidity at %6%.",

        "Extended diagnostic analysis reveals system operating at peak efficiency. Application response "
        "times averaging %1 ms, database queries executing in %2 ms, and network packet delivery "
        "achieving %3% success rate. Resource consumption patterns indicate balanced load distribution "
        "across %4 server instances, with failover mechanisms tested and verified operational. "
        "Security audit completed, all vulnerabilities patched and intrusion detection systems active."
    };

    QString templateMsg = longInfoTemplates[m_random.bounded(longInfoTemplates.size())];
    return templateMsg.arg(m_random.bounded(500) + 100)
        .arg(m_random.bounded(50) + 10)
        .arg(m_random.bounded(30) + 10)
        .arg(m_random.bounded(20) + 75)
        .arg(QDateTime::currentDateTime().addDays(m_random.bounded(7) + 1).toString("yyyy-MM-dd"))
        .arg(m_random.bounded(20) + 40);
}

QString LogGenerator::generateLongWarningMessage()
{
    static const QStringList longWarningTemplates = {
        "Sustained elevated resource utilization detected across multiple system components. "
        "CPU usage consistently above %1% threshold for past %2 minutes, memory consumption "
        "showing gradual increase to %3% of available capacity. Network latency measurements "
        "indicate periodic spikes reaching %4 ms, potentially affecting application responsiveness. "
        "Recommend reviewing active processes and considering load distribution optimization.",

        "Performance degradation trends observed in storage subsystem. Disk I/O wait times "
        "increased by %1% compared to baseline, read/write operations experiencing occasional "
        "timeouts. Filesystem fragmentation at %2%, potentially contributing to reduced efficiency. "
        "Storage capacity forecast indicates critical levels may be reached within %3 days "
        "at current growth rate. Immediate capacity planning and archival procedures recommended.",

        "Network connectivity analysis reveals intermittent stability issues. Packet loss "
        "measurements show periodic spikes up to %1%, primarily affecting UDP-based services. "
        "Routing path analysis indicates %2 hop count increase with corresponding latency "
        "impact. DNS resolution times elevated by %3 ms average. Recommend network infrastructure "
        "review and potential carrier circuit evaluation."
    };
    QString templateMsg = longWarningTemplates[m_random.bounded(longWarningTemplates.size())];
    return templateMsg.arg(m_random.bounded(20) + 75)
        .arg(m_random.bounded(30) + 5)
        .arg(m_random.bounded(15) + 80)
        .arg(m_random.bounded(100) + 50)
        .arg(m_random.bounded(10) + 3);
}

QString LogGenerator::generateLongErrorMessage()
{
    static const QStringList longErrorTemplates = {
        "CRITICAL SYSTEM FAILURE: Multiple redundancy failures detected in primary storage array. "
        "RAID controller reporting %1 disk failures across %2 drive group, data integrity "
        "compromised on %3 logical volumes. Automatic failover to secondary storage unsuccessful "
        "due to %4. Immediate manual intervention required to prevent data loss. Emergency "
        "procedures initiated, estimated recovery time: %5 minutes.",

        "NETWORK INFRASTRUCTURE FAILURE: Core routing equipment experiencing complete service outage. "
        "Primary and secondary uplinks disconnected, BGP sessions dropped, and internal routing "
        "protocols non-responsive. Impact analysis shows %1% of services affected with estimated "
        "%2 customers unable to connect. Redundant systems failed to activate due to %3. "
        "Emergency response team activated, ETA for partial restoration: %4 minutes.",

        "SECURITY BREACH DETECTED: Intrusion prevention system identified sophisticated multi-vector "
        "attack pattern targeting %1 vulnerability. %2 unauthorized access attempts recorded "
        "from %3 distinct IP addresses over past %4 minutes. Data exfiltration detected affecting "
        "%5 database tables. Automatic containment procedures initiated, incident response team "
        "notified, and regulatory compliance reporting procedures activated."
    };
    QString templateMsg = longErrorTemplates[m_random.bounded(longErrorTemplates.size())];
    return templateMsg.arg(m_random.bounded(3) + 1)
        .arg(m_random.bounded(4) + 2)
        .arg(m_random.bounded(5) + 1)
        .arg(QStringList{"configuration mismatch", "hardware failure", "software bug", "network partition"}[m_random.bounded(4)])
        .arg(m_random.bounded(120) + 30)
        .arg(m_random.bounded(20) + 5);
}
