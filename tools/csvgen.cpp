// csvgen – creates a large fictional orders CSV for testing.   DEMO PROJECT
//   csvgen [rows] [output.csv]        default: 5000000 rows -> sample_orders_5M.csv
#include "core/SampleGenerator.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>

#include <cstdio>
#include <thread>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    bool ok = true;
    const qint64 rows = args.size() > 1 ? args.at(1).toLongLong(&ok) : 5000000;
    if (!ok || rows <= 0) {
        std::fprintf(stderr, "usage: csvgen [rows] [output.csv]\n");
        return 2;
    }
    const QString path = args.size() > 2 ? args.at(2)
                         : (rows % 1000000 == 0 ? QStringLiteral("sample_orders_%1M.csv").arg(rows / 1000000)
                                                : QStringLiteral("sample_orders_%1.csv").arg(rows));
    std::atomic<qint64> done{0};
    std::atomic<bool> cancel{false};
    QString error;
    QElapsedTimer t;
    t.start();
    bool result = false;
    std::atomic<bool> finished{false};
    std::thread worker([&] {
        result = csv::generateSample(path, rows, done, cancel, &error);
        finished = true;
    });
    while (!finished) {
        std::fprintf(stderr, "\r%lld / %lld rows", static_cast<long long>(done.load()), static_cast<long long>(rows));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    worker.join();
    if (!result) {
        std::fprintf(stderr, "\nerror: %s\n", qPrintable(error));
        return 1;
    }
    std::printf("\r%lld rows written to %s (%.0f MB) in %.1f s\n", static_cast<long long>(rows),
                qPrintable(QFileInfo(path).absoluteFilePath()), QFileInfo(path).size() / 1e6, t.elapsed() / 1000.0);
    return 0;
}
