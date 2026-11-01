// Writes a simple Excel workbook (.xlsx) row by row: one sheet, bold header row that stays
// visible when scrolling, filter buttons, column widths. Numbers are stored as numbers.
#pragma once

#include "ZipWriter.h"

#include <QStringList>
#include <vector>

class XlsxWriter
{
public:
    static constexpr qint64 kMaxRows = 1048576;      // Excel limit per sheet (incl. header)

    explicit XlsxWriter(QIODevice *device);

    // `widths` in Excel character units, one per column.
    bool begin(const std::vector<double> &widths, bool headerRow);
    // `commaDecimal`: treat "12,50" as a number (files that use ';' as delimiter).
    bool writeRow(const QStringList &cells, bool commaDecimal = false);
    bool finish();

    QString errorString() const { return m_zip.errorString(); }

    // Exposed for tests: is the text a number Excel should store as a number?
    static bool numberText(const QString &text, bool commaDecimal, QString *excelValue);
    static QString columnName(int index);            // 0 -> A, 26 -> AA

private:
    bool flush(bool force);

    ZipWriter m_zip;
    QByteArray m_buf;
    qint64 m_row = 0;
    int m_columns = 0;
    bool m_headerRow = true;
    std::vector<QByteArray> m_colNames;
};
