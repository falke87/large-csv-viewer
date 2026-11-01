#include "CsvTableModel.h"

#include "core/CsvDocument.h"

#include <QBrush>
#include <QColor>
#include <QLocale>

#include <climits>

CsvTableModel::CsvTableModel(QObject *parent) : QAbstractTableModel(parent) {}

void CsvTableModel::setDocument(csv::CsvDocument *doc)
{
    beginResetModel();
    if (m_doc) disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    m_view.reset();
    m_highlights.clear();
    m_cache.clear();
    m_rows = doc ? int(std::min<qint64>(doc->rowCount(), INT_MAX)) : 0;
    if (doc) connect(doc, &csv::CsvDocument::rowsAdded, this, &CsvTableModel::onRowsAdded);
    endResetModel();
}

void CsvTableModel::setView(RowList rows)
{
    beginResetModel();
    m_view = std::move(rows);
    m_rows = m_view ? int(std::min<size_t>(m_view->size(), INT_MAX))
                    : int(m_doc ? std::min<qint64>(m_doc->rowCount(), INT_MAX) : 0);
    endResetModel();
}

void CsvTableModel::setHighlights(const QList<Highlight> &highlights)
{
    m_highlights = highlights;
    if (m_rows > 0) emit dataChanged(index(0, 0), index(m_rows - 1, columnCount() - 1), {Qt::BackgroundRole});
}

void CsvTableModel::onRowsAdded(qint64 rows)
{
    if (m_view) return;
    const int total = int(std::min<qint64>(rows, INT_MAX));
    if (total <= m_rows) return;
    beginInsertRows({}, m_rows, total - 1);
    m_rows = total;
    endInsertRows();
}

qint64 CsvTableModel::sourceRow(int row) const
{
    return m_view ? qint64((*m_view)[size_t(row)]) : row;
}

const QStringList &CsvTableModel::rowValues(qint64 source) const
{
    if (QStringList *cached = m_cache.object(source)) return *cached;
    auto *values = new QStringList(m_doc->rowValues(source));
    m_cache.insert(source, values);
    return *values;
}

int CsvTableModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_rows; }

int CsvTableModel::columnCount(const QModelIndex &parent) const
{
    return (parent.isValid() || !m_doc) ? 0 : m_doc->columnCount();
}

QVariant CsvTableModel::data(const QModelIndex &index, int role) const
{
    if (!m_doc || !index.isValid() || index.row() >= m_rows) return {};
    switch (role) {
    case Qt::DisplayRole: {
        QString text = rowValues(sourceRow(index.row())).value(index.column());
        if (text.contains(QLatin1Char('\n'))) text.replace(QLatin1Char('\n'), QStringLiteral(" ↵ "));
        return text;
    }
    case Qt::ToolTipRole: {
        const QString text = rowValues(sourceRow(index.row())).value(index.column());
        return text.size() > 40 || text.contains(QLatin1Char('\n')) ? QVariant(text) : QVariant();
    }
    case Qt::TextAlignmentRole:
        return m_doc->columnLooksNumeric(index.column()) ? QVariant(Qt::AlignRight | Qt::AlignVCenter)
                                                         : QVariant(Qt::AlignLeft | Qt::AlignVCenter);
    case Qt::BackgroundRole: {
        if (m_highlights.isEmpty()) return {};
        const QString text = rowValues(sourceRow(index.row())).value(index.column());
        for (const Highlight &h : m_highlights)
            if ((h.column < 0 || h.column == index.column()) && !h.text.isEmpty() &&
                text.contains(h.text, Qt::CaseInsensitive))
                return QBrush(QColor(255, 236, 153));
        return {};
    }
    default:
        return {};
    }
}

QVariant CsvTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (!m_doc) return {};
    if (orientation == Qt::Horizontal) {
        if (role == Qt::DisplayRole) return m_doc->headers().value(section);
        if (role == Qt::ToolTipRole) return m_doc->headers().value(section);
        return {};
    }
    if (role == Qt::DisplayRole && section < m_rows)
        return QLocale(QLocale::English).toString(sourceRow(section) + 1);   // original row: 3,256,254
    if (role == Qt::TextAlignmentRole) return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}
