// csvbench – measures open, filter, sort and export on a CSV file (no window).
//   csvbench file.csv
#include "core/CsvDocument.h"
#include "core/Exporter.h"
#include "core/RowFilter.h"
#include "core/RowSorter.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>
#include <numeric>
#include <thread>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: csvbench file.csv\n");
        return 2;
    }
    std::printf("CPU threads: %u\n", std::thread::hardware_concurrency());
    QElapsedTimer t;
    t.start();
    csv::CsvDocument doc;
    QString err;
    if (!doc.open(QString::fromLocal8Bit(argv[1]), {}, &err)) {
        std::fprintf(stderr, "%s\n", qPrintable(err));
        return 1;
    }
    doc.waitForIndex();
    std::printf("open + index : %8.2f s  %lld rows, %d columns, %.0f MB\n", t.nsecsElapsed() / 1e9,
                static_cast<long long>(doc.rowCount()), doc.columnCount(), doc.fileSize() / 1e6);

    std::atomic<qint64> done{0};
    std::atomic<bool> cancel{false};
    auto run = [&](const char *label, std::vector<csv::Condition> c) {
        t.restart();
        const auto rows = csv::filterRows(doc, c, done, cancel);
        std::printf("%-13s: %8.2f s  %zu rows\n", label, t.nsecsElapsed() / 1e9, rows.size());
        return rows;
    };
    run("search 'Weiß'", {{-1, csv::Op::Contains, QStringLiteral("Weiß")}});
    run("search 'paypal'", {{-1, csv::Op::Contains, QStringLiteral("paypal")}});
    const auto f = run("city+total", {{6, csv::Op::Equals, QStringLiteral("München")},
                                      {12, csv::Op::Greater, QStringLiteral("500")}});

    std::vector<quint32> all(size_t(doc.rowCount()));
    std::iota(all.begin(), all.end(), 0u);
    t.restart();
    auto s = csv::sortRows(doc, all, 12, false, done, cancel);
    std::printf("sort total   : %8.2f s  (numeric=%d)\n", t.nsecsElapsed() / 1e9, s.numeric);
    t.restart();
    s = csv::sortRows(doc, all, 3, true, done, cancel);
    std::printf("sort name    : %8.2f s  (numeric=%d)\n", t.nsecsElapsed() / 1e9, s.numeric);

    QTemporaryDir dir;
    csv::ExportOptions o;
    o.path = dir.filePath("out.xlsx");
    for (int c = 0; c < doc.columnCount(); ++c) o.columns.push_back(c);
    t.restart();
    auto r = csv::exportRows(doc, &f, o, done, cancel);
    std::printf("export xlsx  : %8.2f s  %lld rows, %.1f MB  %s\n", t.nsecsElapsed() / 1e9,
                static_cast<long long>(r.rows), r.bytes / 1e6, qPrintable(r.error));
    return 0;
}
