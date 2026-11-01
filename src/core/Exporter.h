// Saves rows (all, or the filtered/sorted view) and chosen columns as .xlsx or .csv.
#pragma once

#include <QString>
#include <atomic>
#include <vector>

namespace csv {

class CsvDocument;

struct ExportOptions {
    enum Format { Xlsx, Csv } format = Xlsx;
    QString path;
    std::vector<int> columns;        // column indexes, in output order
    bool includeHeader = true;
    char csvDelimiter = ',';
    bool csvUtf8Bom = true;          // lets Excel detect UTF-8 (umlauts, accents)
};

struct ExportResult {
    bool ok = false;
    bool cancelled = false;
    qint64 rows = 0;
    qint64 bytes = 0;
    QString error;
};

// `rows` = data row numbers to write in this order; nullptr = all rows in file order.
// Writes to a temporary file first, so a cancelled export never leaves a broken file.
ExportResult exportRows(const CsvDocument &doc, const std::vector<quint32> *rows, const ExportOptions &options,
                        std::atomic<qint64> &done, const std::atomic<bool> &cancel);

// Quotes a value for CSV if needed.
QByteArray csvEscape(const QByteArray &value, char delimiter);

}  // namespace csv
