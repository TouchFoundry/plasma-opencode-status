#pragma once

#include <QObject>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <QDateTime>
#include <QElapsedTimer>
#include <QtQml/qqmlregistration.h>

class Monitor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString executable MEMBER m_executable NOTIFY executableChanged)
    Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY intervalChanged)
    Q_PROPERTY(int maxDays READ maxDays WRITE setMaxDays NOTIFY maxDaysChanged)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY snapshotChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QDateTime updatedAt READ updatedAt NOTIFY stateChanged)
    Q_PROPERTY(bool hasSnapshot READ hasSnapshot NOTIFY stateChanged)

public:
    explicit Monitor(QObject *parent = nullptr);
    ~Monitor() override;
    int interval() const { return m_interval; }
    void setInterval(int seconds);
    int maxDays() const { return m_maxDays; }
    void setMaxDays(int days);
    QVariantMap snapshot() const { return m_snapshot; }
    QString error() const { return m_error; }
    bool busy() const { return m_busy; }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QDateTime updatedAt() const { return m_updatedAt; }
    bool hasSnapshot() const { return m_updatedAt.isValid(); }
    Q_INVOKABLE void refresh();

signals:
    void executableChanged();
    void intervalChanged();
    void maxDaysChanged();
    void snapshotChanged();
    void stateChanged();
    void enabledChanged();

private:
    friend class MonitorTest;
    enum class Request { Sessions, Active, Missing };
    void request(const QString &path, Request kind);
    void finished(int code, QProcess::ExitStatus status);
    void fail(const QString &message);
    void complete();
    void fetchMissing();
    void poll(bool full);
    void schedule();

    QString m_executable = QStringLiteral("opencode");
    QTimer m_timer;
    QTimer m_timeout;
    QProcess m_process;
    bool m_busy = false;
    bool m_enabled = false;
    bool m_cancelled = false;
    bool m_full = true;
    int m_failures = 0;
    int m_interval = 30;
    int m_maxDays = 1;
    qint64 m_cutoff = 0;
    QDateTime m_updatedAt;
    QElapsedTimer m_reconciled;
    qint64 m_watermark = 0;
    qint64 m_pendingWatermark = 0;
    Request m_request = Request::Sessions;
    QString m_error;
    QVariantMap m_snapshot;
    QVariantMap m_active;
    QMap<QString, QVariantMap> m_cache;
    QMap<QString, QVariantMap> m_pending;
    QSet<QString> m_ids;
    QSet<QString> m_cursors;
    QStringList m_missing;
};
