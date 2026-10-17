#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// One flat, virtualizable model including section headings. Stable IDs survive refreshes.
class SessionModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap snapshot READ snapshot WRITE setSnapshot NOTIFY snapshotChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countsChanged)
    Q_PROPERTY(int inactiveCount READ inactiveCount NOTIFY countsChanged)
public:
    enum Role { RowId = Qt::UserRole + 1, Title, Agent, Directory, Status, Depth,
                HasChildren, Collapsed, Section, Kind };
    Q_ENUM(Role)
    explicit SessionModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap snapshot() const { return m_snapshot; }
    void setSnapshot(const QVariantMap &snapshot);
    int activeCount() const { return m_activeCount; }
    int inactiveCount() const { return m_inactiveCount; }
    Q_INVOKABLE void toggle(const QString &id);
    Q_INVOKABLE int indexForId(const QString &id) const { return m_indices.value(id, -1); }
    Q_INVOKABLE QString idAt(int index) const;
signals:
    void snapshotChanged();
    void countsChanged();
    void updating();
    void updated();
private:
    struct Row {
        QString id, title, agent, directory, status, section, kind;
        int depth = 0;
        bool children = false, collapsed = false;
        bool operator==(const Row &other) const;
    };
    void rebuild();
    void reconcile(const QList<Row> &rows);
    QVariantMap m_snapshot;
    QList<Row> m_rows;
    QHash<QString, int> m_indices;
    QSet<QString> m_collapsed;
    int m_activeCount = 0, m_inactiveCount = 0;
};
