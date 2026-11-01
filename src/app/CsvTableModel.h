// Table model on top of CsvDocument. Rows are parsed only when the view asks for them
// (only the visible ones), with a small cache. A "view" (filtered/sorted row list) can be
// set; the row header always shows the original row number from the file.
#pragma once

#include <QAbstractTableModel>
#include <QCache>

#include <memory>
#include <vector>

namespace csv { class CsvDocument; }

using RowList = std::shared_ptr<const std::vector<quint32>>;

class CsvTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit CsvTableModel(QObject *parent = nullptr);

    void setDocument(csv::CsvDocument *doc);
    void setView(RowList rows);                  // nullptr = all rows in file order
    const RowList &view() const { return m_view; }
    qint64 sourceRow(int row) const;

    // Cells that contain one of these texts get a yellow background.
    struct Highlight { int column; QString text; };   // column -1 = any
    void setHighlights(const QList<Highlight> &highlights);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    void onRowsAdded(qint64 rows);
    const QStringList &rowValues(qint64 sourceRow) const;

    csv::CsvDocument *m_doc = nullptr;
    RowList m_view;
    int m_rows = 0;
    QList<Highlight> m_highlights;
    mutable QCache<qint64, QStringList> m_cache{20000};
};
