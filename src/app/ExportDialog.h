// Export settings: which rows, which columns, Excel or CSV, file name.
#pragma once

#include "core/Exporter.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QRadioButton;
class QGroupBox;
class QPushButton;

class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    ExportDialog(const QStringList &columns, qint64 viewRows, qint64 allRows, bool viewIsFiltered,
                 const QString &sourcePath, QWidget *parent = nullptr, const QList<int> &uncheckedColumns = {});

    csv::ExportOptions options() const;
    bool exportView() const;         // true: current (filtered/sorted) rows, false: all rows

private:
    void updateState();
    void browse();
    qint64 selectedRowCount() const;

    qint64 m_viewRows, m_allRows;
    QString m_sourcePath;
    QRadioButton *m_rowsView, *m_rowsAll, *m_xlsx, *m_csv;
    QListWidget *m_columns;
    QCheckBox *m_header, *m_bom;
    QComboBox *m_delimiter;
    QGroupBox *m_csvBox;
    QLineEdit *m_path;
    QLabel *m_excelNote;
    QPushButton *m_ok = nullptr;
};
