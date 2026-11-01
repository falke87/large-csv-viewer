// Panel with filter conditions: [column] [operator] [value] [x], combined with AND.
#pragma once

#include "core/RowFilter.h"

#include <QWidget>

class QComboBox;
class QLineEdit;
class QVBoxLayout;
class QLabel;

class FilterBar : public QWidget
{
    Q_OBJECT
public:
    explicit FilterBar(QWidget *parent = nullptr);

    void setColumns(const QStringList &columns);
    std::vector<csv::Condition> conditions() const;
    void addCondition(int column = -1, csv::Op op = csv::Op::Contains, const QString &value = {});
    void clear();                    // back to one empty condition
    void editColumn(int column);     // fills an empty row (or adds one) for this column

signals:
    void applyRequested();
    void clearRequested();

private:
    struct Row { QWidget *widget; QComboBox *column; QComboBox *op; QLineEdit *value; };
    void removeRow(QWidget *widget);

    QStringList m_columns;
    QList<Row> m_rows;
    QVBoxLayout *m_rowsLayout;
};
