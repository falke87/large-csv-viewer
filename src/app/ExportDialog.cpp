#include "ExportDialog.h"

#include "core/XlsxWriter.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

ExportDialog::ExportDialog(const QStringList &columns, qint64 viewRows, qint64 allRows, bool viewIsFiltered,
                           const QString &sourcePath, QWidget *parent, const QList<int> &uncheckedColumns)
    : QDialog(parent), m_viewRows(viewRows), m_allRows(allRows), m_sourcePath(sourcePath)
{
    setWindowTitle(tr("Export rows"));
    const QLocale en(QLocale::English);
    auto *layout = new QVBoxLayout(this);

    // Rows
    auto *rowsBox = new QGroupBox(tr("Rows"));
    auto *rowsLayout = new QVBoxLayout(rowsBox);
    m_rowsView = new QRadioButton(tr("Current view: %1 rows%2").arg(en.toString(viewRows),
                                  viewIsFiltered ? tr(" (filtered / sorted)") : QString()));
    m_rowsAll = new QRadioButton(tr("All rows in the file: %1").arg(en.toString(allRows)));
    m_rowsView->setChecked(true);
    m_rowsView->setEnabled(viewIsFiltered);
    if (!viewIsFiltered) m_rowsAll->setChecked(true);
    rowsLayout->addWidget(m_rowsView);
    rowsLayout->addWidget(m_rowsAll);

    // Format
    auto *formatBox = new QGroupBox(tr("Format"));
    auto *formatLayout = new QVBoxLayout(formatBox);
    m_xlsx = new QRadioButton(tr("Excel workbook (.xlsx) – header row, filter buttons, numbers as numbers"));
    m_csv = new QRadioButton(tr("CSV (.csv) – no row limit"));
    m_xlsx->setChecked(true);
    m_excelNote = new QLabel;
    m_excelNote->setStyleSheet(QStringLiteral("color:#a15c00"));
    m_excelNote->setWordWrap(true);
    formatLayout->addWidget(m_xlsx);
    formatLayout->addWidget(m_csv);
    formatLayout->addWidget(m_excelNote);

    m_csvBox = new QGroupBox(tr("CSV options"));
    auto *csvLayout = new QFormLayout(m_csvBox);
    m_delimiter = new QComboBox;
    m_delimiter->addItem(tr("Comma  ,"), QChar(','));
    m_delimiter->addItem(tr("Semicolon  ;  (German Excel)"), QChar(';'));
    m_delimiter->addItem(tr("Tab"), QChar('\t'));
    m_bom = new QCheckBox(tr("UTF-8 with BOM (Excel shows umlauts and accents correctly)"));
    m_bom->setChecked(true);
    csvLayout->addRow(tr("Delimiter:"), m_delimiter);
    csvLayout->addRow(QString(), m_bom);

    // Columns
    auto *colBox = new QGroupBox(tr("Columns"));
    auto *colLayout = new QVBoxLayout(colBox);
    m_columns = new QListWidget;
    for (int i = 0; i < columns.size(); ++i) {               // hidden columns start unchecked
        auto *item = new QListWidgetItem(columns.at(i), m_columns);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(uncheckedColumns.contains(i) ? Qt::Unchecked : Qt::Checked);
    }
    m_columns->setMinimumHeight(170);
    auto *colButtons = new QHBoxLayout;
    auto *all = new QPushButton(tr("All"));
    auto *none = new QPushButton(tr("None"));
    m_header = new QCheckBox(tr("Include header row"));
    m_header->setChecked(true);
    colButtons->addWidget(all);
    colButtons->addWidget(none);
    colButtons->addStretch();
    colButtons->addWidget(m_header);
    colLayout->addWidget(m_columns);
    colLayout->addLayout(colButtons);

    // File
    auto *fileLayout = new QHBoxLayout;
    m_path = new QLineEdit;
    auto *browseBtn = new QPushButton(tr("Browse…"));
    fileLayout->addWidget(new QLabel(tr("Save as:")));
    fileLayout->addWidget(m_path, 1);
    fileLayout->addWidget(browseBtn);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_ok = box->button(QDialogButtonBox::Ok);
    m_ok->setText(tr("Export"));

    auto *top = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    left->addWidget(rowsBox);
    left->addWidget(formatBox);
    left->addWidget(m_csvBox);
    left->addStretch();
    top->addLayout(left, 3);
    top->addWidget(colBox, 2);
    layout->addLayout(top);
    layout->addLayout(fileLayout);
    layout->addWidget(box);

    connect(all, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_columns->count(); ++i) m_columns->item(i)->setCheckState(Qt::Checked);
    });
    connect(none, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_columns->count(); ++i) m_columns->item(i)->setCheckState(Qt::Unchecked);
    });
    connect(m_columns, &QListWidget::itemChanged, this, &ExportDialog::updateState);
    for (auto *b : {m_rowsView, m_rowsAll, m_xlsx, m_csv})
        connect(b, &QRadioButton::toggled, this, &ExportDialog::updateState);
    connect(m_path, &QLineEdit::textChanged, this, &ExportDialog::updateState);
    connect(browseBtn, &QPushButton::clicked, this, &ExportDialog::browse);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const QFileInfo src(sourcePath);
    m_path->setText(src.absolutePath() + QLatin1Char('/') + src.completeBaseName() +
                    (viewIsFiltered ? QStringLiteral("_filtered") : QStringLiteral("_export")) +
                    QStringLiteral(".xlsx"));
    updateState();
    resize(860, 520);
}

qint64 ExportDialog::selectedRowCount() const { return m_rowsView->isChecked() ? m_viewRows : m_allRows; }

void ExportDialog::updateState()
{
    // Excel: at most 1,048,576 rows including the header.
    const bool tooMany = selectedRowCount() + 1 > XlsxWriter::kMaxRows;
    m_xlsx->setEnabled(!tooMany);
    if (tooMany && m_xlsx->isChecked()) m_csv->setChecked(true);
    m_excelNote->setText(tooMany ? tr("Excel holds at most 1,048,576 rows per sheet – use CSV, or filter first.")
                                 : QString());
    m_excelNote->setVisible(tooMany);
    m_csvBox->setEnabled(m_csv->isChecked());

    // Keep the file extension in line with the format.
    QString p = m_path->text();
    const QString ext = m_csv->isChecked() ? QStringLiteral(".csv") : QStringLiteral(".xlsx");
    if (!p.isEmpty() && !p.endsWith(ext, Qt::CaseInsensitive)) {
        const QFileInfo fi(p);
        p = fi.path() + QLatin1Char('/') + fi.completeBaseName() + ext;
        QSignalBlocker block(m_path);
        m_path->setText(p);
    }
    int checked = 0;
    for (int i = 0; i < m_columns->count(); ++i) checked += m_columns->item(i)->checkState() == Qt::Checked;
    if (m_ok) m_ok->setEnabled(checked > 0 && !m_path->text().trimmed().isEmpty() && selectedRowCount() > 0);
}

void ExportDialog::browse()
{
    const QString filter = m_csv->isChecked() ? tr("CSV files (*.csv)") : tr("Excel workbooks (*.xlsx)");
    const QString p = QFileDialog::getSaveFileName(this, tr("Export to"), m_path->text(), filter);
    if (!p.isEmpty()) m_path->setText(p);
}

bool ExportDialog::exportView() const { return m_rowsView->isChecked(); }

csv::ExportOptions ExportDialog::options() const
{
    csv::ExportOptions o;
    o.format = m_csv->isChecked() ? csv::ExportOptions::Csv : csv::ExportOptions::Xlsx;
    o.path = m_path->text().trimmed();
    for (int i = 0; i < m_columns->count(); ++i)
        if (m_columns->item(i)->checkState() == Qt::Checked) o.columns.push_back(i);
    o.includeHeader = m_header->isChecked();
    o.csvDelimiter = m_delimiter->currentData().toChar().toLatin1();
    o.csvUtf8Bom = m_bom->isChecked();
    return o;
}
