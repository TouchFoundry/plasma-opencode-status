#include "sessionmodel.h"

#include <algorithm>
#include <tuple>

bool SessionModel::Row::operator==(const Row &other) const
{
    return std::tie(id, title, agent, directory, status, section, kind, depth, children, collapsed)
        == std::tie(other.id, other.title, other.agent, other.directory, other.status, other.section,
                    other.kind, other.depth, other.children, other.collapsed);
}

QHash<int, QByteArray> SessionModel::roleNames() const
{
    return {{RowId, "rowId"}, {Title, "title"}, {Agent, "agent"}, {Directory, "directory"},
            {Status, "status"}, {Depth, "depth"}, {HasChildren, "hasChildren"},
            {Collapsed, "collapsed"}, {Section, "section"}, {Kind, "kind"}};
}

QVariant SessionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const auto &row = m_rows[index.row()];
    switch (role) {
    case RowId: return row.id;
    case Title: return row.title;
    case Agent: return row.agent;
    case Directory: return row.directory;
    case Status: return row.status;
    case Depth: return row.depth;
    case HasChildren: return row.children;
    case Collapsed: return row.collapsed;
    case Section: return row.section;
    case Kind: return row.kind;
    default: return {};
    }
}

QString SessionModel::idAt(int index) const
{
    return index >= 0 && index < m_rows.size() ? m_rows[index].id : QString();
}

void SessionModel::setSnapshot(const QVariantMap &snapshot)
{
    if (m_snapshot == snapshot) return;
    m_snapshot = snapshot;
    rebuild();
    emit snapshotChanged();
}

void SessionModel::toggle(const QString &id)
{
    const int i = indexForId(id);
    if (i < 0 || !m_rows[i].children) return;
    if (m_collapsed.contains(id)) m_collapsed.remove(id); else m_collapsed.insert(id);
    rebuild();
}

void SessionModel::rebuild()
{
    struct Node {
        Row row;
        QString parentId;
        qint64 updated = 0;
        int parent = -1, color = 0;
        bool familyActive = false;
        QList<int> children;
    };
    QList<Node> nodes;
    QHash<QString, int> lookup;
    const auto sessions = m_snapshot.value("sessions").toList();
    const auto active = m_snapshot.value("active").toMap();
    nodes.reserve(sessions.size());
    int running = 0;
    for (const auto &value : sessions) {
        const auto session = value.toMap();
        const auto id = session.value("id").toString();
        const auto time = session.value("time").toMap();
        const bool isActive = active.contains(id);
        if (id.isEmpty() || lookup.contains(id) || (time.value("archived").toLongLong() && !isActive)) continue;
        Node node;
        node.row.id = id;
        node.row.title = session.value("title").toString();
        if (node.row.title.isEmpty()) node.row.title = id;
        node.row.agent = session.value("agent").toString();
        node.row.directory = session.value("location").toMap().value("directory").toString();
        const auto outcome = session.value("outcome").toString();
        node.row.status = isActive ? "running" : outcome == "succeeded" ? "completed"
            : outcome == "failed" || outcome == "interrupted" ? outcome : "idle";
        node.row.kind = "session";
        node.row.collapsed = m_collapsed.contains(id);
        node.parentId = session.value("parentID").toString();
        node.updated = time.value("updated").toLongLong();
        node.familyActive = isActive;
        running += isActive;
        lookup.insert(id, nodes.size());
        nodes.append(node);
    }
    // Prune collapse state for deleted/archived sessions.
    m_collapsed.intersect(QSet<QString>(lookup.keyBegin(), lookup.keyEnd()));
    for (auto &node : nodes) node.parent = lookup.value(node.parentId, -1);
    // Linear-time parent cycle detection, with no recursion even for very deep trees.
    for (int i = 0; i < nodes.size(); ++i) {
        QList<int> path;
        int at = i;
        while (at >= 0 && nodes[at].color == 0) {
            nodes[at].color = 1;
            path.append(at);
            at = nodes[at].parent;
        }
        if (at >= 0 && nodes[at].color == 1) {
            int cycle = at;
            do {
                const int next = nodes[cycle].parent;
                nodes[cycle].parent = -1;
                cycle = next;
            } while (cycle != at);
        }
        for (int index : path) nodes[index].color = 2;
    }
    QList<int> roots;
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes[i].parent < 0) roots.append(i);
        else nodes[nodes[i].parent].children.append(i);
    }
    const auto less = [&nodes](int a, int b) {
        return nodes[a].updated != nodes[b].updated ? nodes[a].updated > nodes[b].updated : nodes[a].row.id < nodes[b].row.id;
    };
    std::sort(roots.begin(), roots.end(), less);
    for (auto &node : nodes) std::sort(node.children.begin(), node.children.end(), less);
    QList<int> order = roots;
    for (int i = 0; i < order.size(); ++i)
        order.append(nodes[order[i]].children);
    for (auto it = order.crbegin(); it != order.crend(); ++it) {
        const int parent = nodes[*it].parent;
        if (parent >= 0) nodes[parent].familyActive |= nodes[*it].familyActive;
    }
    QList<Row> rows;
    rows.reserve(nodes.size() + 4);
    for (const bool activeSection : {true, false}) {
        const QString section = activeSection ? "active" : "inactive";
        Row header;
        header.id = "@" + section;
        header.kind = "header";
        header.section = section;
        rows.append(header);
        QList<QPair<int, int>> stack;
        for (auto it = roots.crbegin(); it != roots.crend(); ++it)
            if (nodes[*it].familyActive == activeSection) stack.append({*it, 0});
        if (stack.isEmpty()) {
            Row empty;
            empty.id = "@empty-" + section;
            empty.kind = "empty";
            empty.section = section;
            rows.append(empty);
        }
        while (!stack.isEmpty()) {
            const auto [index, depth] = stack.takeLast();
            auto &node = nodes[index];
            node.row.depth = depth;
            node.row.children = !node.children.isEmpty();
            node.row.section = section;
            rows.append(node.row);
            if (!node.row.collapsed)
                for (auto it = node.children.crbegin(); it != node.children.crend(); ++it)
                    stack.append({*it, depth + 1});
        }
    }
    const int inactive = nodes.size() - running;
    if (m_activeCount != running || m_inactiveCount != inactive) {
        m_activeCount = running;
        m_inactiveCount = inactive;
        emit countsChanged();
    }
    reconcile(rows);
}

void SessionModel::reconcile(const QList<Row> &rows)
{
    if (m_rows == rows) return;
    emit updating();
    QHash<QString, int> desired;
    desired.reserve(rows.size());
    for (int i = 0; i < rows.size(); ++i) desired.insert(rows[i].id, i);
    // Remove contiguous runs from the end; existing delegates are never reset.
    for (int i = m_rows.size() - 1; i >= 0;) {
        if (desired.contains(m_rows[i].id)) { --i; continue; }
        const int last = i;
        while (i >= 0 && !desired.contains(m_rows[i].id)) --i;
        beginRemoveRows({}, i + 1, last);
        m_rows.remove(i + 1, last - i);
        endRemoveRows();
    }
    QSet<QString> existing;
    for (const auto &row : std::as_const(m_rows)) existing.insert(row.id);
    QList<Row> added;
    for (const auto &row : rows) if (!existing.contains(row.id)) added.append(row);
    if (!added.isEmpty()) {
        beginInsertRows({}, m_rows.size(), m_rows.size() + added.size() - 1);
        m_rows.append(added);
        endInsertRows();
    }
    bool reorder = false;
    for (int i = 0; i < rows.size(); ++i) reorder |= m_rows[i].id != rows[i].id;
    if (reorder) {
        emit layoutAboutToBeChanged();
        const auto oldPersistent = persistentIndexList();
        QModelIndexList newPersistent;
        for (const auto &old : oldPersistent) newPersistent.append(index(desired.value(m_rows[old.row()].id)));
        QList<Row> reordered(rows.size());
        for (const auto &row : std::as_const(m_rows)) reordered[desired.value(row.id)] = row;
        m_rows = std::move(reordered);
        changePersistentIndexList(oldPersistent, newPersistent);
        emit layoutChanged();
    }
    for (int i = 0; i < rows.size(); ++i) {
        if (m_rows[i] == rows[i]) continue;
        m_rows[i] = rows[i];
        emit dataChanged(index(i), index(i));
    }
    m_indices = std::move(desired);
    emit updated();
}
