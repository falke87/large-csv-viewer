#include "Exporter.h"

#include "CsvDocument.h"
#include "XlsxWriter.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace csv {

QByteArray csvEscape(const QByteArray &value, char delimiter)
{
    const bool needsQuotes = value.contains(delimiter) || value.contains('"') || value.contains('\n') ||
                             value.contains('\r') || value.startsWith(' ') || value.endsWith(' ');
    if (!needsQuotes) return value;
    QByteArray out = value;
    out.replace("\"", "\"\"");
    return '"' + out + '"';
}

ExportResult exportRows(const CsvDocument &doc, const std::vector<quint32> *rows, const ExportOptions &opt,
                        std::atomic<qint64> &done, const std::atomic<bool> &cancel)
{
    ExportResult res;
    const qint64 total = rows ? qint64(rows->size()) : doc.rowCount();
    auto rowAt = [&](qint64 i) -> qint64 { return rows ? qint64((*rows)[size_t(i)]) : i; };

    if (opt.format == ExportOptions::Xlsx && total + (opt.includeHeader ? 1 : 0) > XlsxWriter::kMaxRows) {
        res.error = QCoreApplication::translate("Export",
            "Excel can hold at most 1,048,576 rows per sheet. Please filter further or export as CSV.");
        return res;
    }

    QSaveFile file(opt.path);
    if (!file.open(QIODevice::WriteOnly)) {
        res.error = file.errorString();
        return res;
    }

    FieldList fields;
    QByteArray scratch;
    const auto &cols = opt.columns;
    const bool utf8 = doc.encoding() == Encoding::Utf8;

    if (opt.format == ExportOptions::Xlsx) {
        // Column widths from the header and the first rows.
        std::vector<double> widths(cols.size(), 8.0);
        for (size_t c = 0; c < cols.size(); ++c)
            widths[c] = std::max(widths[c], doc.headers().value(cols[c]).size() + 3.0);
        for (qint64 i = 0; i < std::min<qint64>(total, 200); ++i) {
            doc.splitRow(rowAt(i), fields);
            for (size_t c = 0; c < cols.size(); ++c)
                if (cols[c] < fields.size())
                    widths[c] = std::max(widths[c], std::min(60.0, doc.fieldText(fields[cols[c]]).size() + 2.0));
        }

        XlsxWriter xlsx(&file);
        bool ok = xlsx.begin(widths, opt.includeHeader);
        QStringList cells;
        if (ok && opt.includeHeader) {
            for (int c : cols) cells << doc.headers().value(c);
            ok = xlsx.writeRow(cells);
        }
        const bool commaDecimal = doc.delimiter() == ';';
        for (qint64 i = 0; ok && i < total; ++i) {
            if ((i & 4095) == 4095) {
                done = i;
                if (cancel) break;
            }
            doc.splitRow(rowAt(i), fields);
            cells.clear();
            for (int c : cols) cells << (c < fields.size() ? doc.fieldText(fields[c]) : QString());
            ok = xlsx.writeRow(cells, commaDecimal);
        }
        if (ok && !cancel) ok = xlsx.finish();
        if (!ok && !cancel) res.error = xlsx.errorString().isEmpty() ? file.errorString() : xlsx.errorString();
    } else {
        QByteArray buf;
        buf.reserve(1 << 20);
        if (opt.csvUtf8Bom) buf.append("\xEF\xBB\xBF");
        const char d = opt.csvDelimiter;
        if (opt.includeHeader) {
            for (size_t c = 0; c < cols.size(); ++c) {
                if (c) buf.append(d);
                buf.append(csvEscape(doc.headers().value(cols[c]).toUtf8(), d));
            }
            buf.append("\r\n");
        }
        bool ok = true;
        for (qint64 i = 0; i < total && ok; ++i) {
            if ((i & 4095) == 4095) {
                done = i;
                if (cancel) break;
            }
            doc.splitRow(rowAt(i), fields);
            for (size_t c = 0; c < cols.size(); ++c) {
                if (c) buf.append(d);
                if (cols[c] >= fields.size()) continue;
                const QByteArrayView bytes = fieldBytes(fields[cols[c]], scratch);
                // UTF-8 files: copy the bytes. Windows-1252 files: convert to UTF-8.
                buf.append(csvEscape(utf8 ? bytes.toByteArray() : doc.decode(bytes).toUtf8(), d));
            }
            buf.append("\r\n");
            if (buf.size() >= (1 << 20)) {
                ok = file.write(buf) == buf.size();
                buf.clear();
            }
        }
        if (ok && !cancel) ok = file.write(buf) == buf.size();
        if (!ok && !cancel) res.error = file.errorString();
    }

    if (cancel) {
        file.cancelWriting();
        res.cancelled = true;
        return res;
    }
    if (!res.error.isEmpty()) {
        file.cancelWriting();
        return res;
    }
    if (!file.commit()) {
        res.error = file.errorString();
        return res;
    }
    res.ok = true;
    res.rows = total;
    res.bytes = QFileInfo(opt.path).size();
    done = total;
    return res;
}

}  // namespace csv
