#include "RowSorter.h"

#include "CsvDocument.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

namespace csv {

namespace {

struct TextKey {
    const char *data = nullptr;
    quint32 size = 0;
};

inline unsigned char lowerAscii(unsigned char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int compareText(const TextKey &a, const TextKey &b)
{
    const quint32 n = std::min(a.size, b.size);
    for (quint32 i = 0; i < n; ++i) {
        const unsigned char x = lowerAscii(static_cast<unsigned char>(a.data[i]));
        const unsigned char y = lowerAscii(static_cast<unsigned char>(b.data[i]));
        if (x != y) return x < y ? -1 : 1;
    }
    return a.size == b.size ? 0 : (a.size < b.size ? -1 : 1);
}

template <typename Fn>
void parallelFor(qint64 count, int threads, Fn fn)
{
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([=] { fn(count * t / threads, count * (t + 1) / threads); });
    for (auto &th : pool) th.join();
}

// Sorts `perm` with `less` on all cores: sort parts in parallel, then merge them.
template <typename Less>
void parallelSort(std::vector<quint32> &perm, int threads, Less less)
{
    const qint64 n = qint64(perm.size());
    if (n < 100000) threads = 1;
    std::vector<qint64> bounds;
    for (int t = 0; t <= threads; ++t) bounds.push_back(n * t / threads);
    parallelFor(threads, threads, [&](qint64 from, qint64 to) {
        for (qint64 t = from; t < to; ++t)
            std::sort(perm.begin() + bounds[size_t(t)], perm.begin() + bounds[size_t(t) + 1], less);
    });
    for (size_t width = 1; width < size_t(threads); width *= 2) {
        std::vector<std::thread> pool;
        for (size_t i = 0; i + width < size_t(threads); i += 2 * width) {
            const qint64 a = bounds[i], m = bounds[i + width], b = bounds[std::min(i + 2 * width, size_t(threads))];
            pool.emplace_back([&perm, a, m, b, less] {
                std::inplace_merge(perm.begin() + a, perm.begin() + m, perm.begin() + b, less);
            });
        }
        for (auto &th : pool) th.join();
    }
}

}  // namespace

SortResult sortRows(const CsvDocument &doc, std::vector<quint32> rows, int column, bool ascending,
                    std::atomic<qint64> &done, const std::atomic<bool> &cancel)
{
    SortResult result;
    const qint64 n = qint64(rows.size());
    const int threads = int(std::clamp<unsigned>(std::thread::hardware_concurrency(), 1u, 16u));

    // Numbers or text? Look at up to 2000 rows spread over the data.
    {
        FieldList fields;
        QByteArray scratch;
        int nonEmpty = 0, numbers = 0;
        const qint64 step = std::max<qint64>(1, n / 2000);
        for (qint64 i = 0; i < n; i += step) {
            doc.splitRow(rows[size_t(i)], fields);
            if (column >= fields.size()) continue;
            const QByteArrayView v = fieldBytes(fields[column], scratch).trimmed();
            if (v.isEmpty()) continue;
            ++nonEmpty;
            double x;
            numbers += parseNumber(v, &x);
        }
        result.numeric = nonEmpty > 0 && numbers * 100 >= nonEmpty * 95;
    }

    std::vector<double> numKeys;
    std::vector<TextKey> textKeys;
    if (result.numeric) numKeys.assign(size_t(n), std::numeric_limits<double>::quiet_NaN());
    else textKeys.resize(size_t(n));

    // Extract the sort key of every row (in parallel).
    parallelFor(n, threads, [&](qint64 from, qint64 to) {
        FieldList fields;
        QByteArray scratch;
        for (qint64 i = from; i < to; ++i) {
            if (((i - from) & 16383) == 16383) {
                done += 16384 * 7 / 10;
                if (cancel) return;
            }
            doc.splitRow(rows[size_t(i)], fields);
            if (column >= fields.size()) continue;
            const FieldRef &f = fields[column];
            if (result.numeric) {
                double x;
                if (parseNumber(fieldBytes(f, scratch), &x)) numKeys[size_t(i)] = x;
            } else {
                QByteArrayView v = QByteArrayView(f.data, f.size).trimmed();   // points into the file
                textKeys[size_t(i)] = {v.data(), quint32(v.size())};
            }
        }
    });
    if (cancel) return result;
    done = n * 7 / 10;

    std::vector<quint32> perm(static_cast<size_t>(n));
    for (qint64 i = 0; i < n; ++i) perm[size_t(i)] = quint32(i);

    if (result.numeric) {
        parallelSort(perm, threads, [&](quint32 a, quint32 b) {
            const double x = numKeys[a], y = numKeys[b];
            const bool nx = std::isnan(x), ny = std::isnan(y);
            if (nx != ny) return ny;                       // empty values last
            if (!nx && x != y) return ascending ? x < y : x > y;
            return a < b;                                  // keep original order
        });
    } else {
        parallelSort(perm, threads, [&](quint32 a, quint32 b) {
            const TextKey &x = textKeys[a], &y = textKeys[b];
            if ((x.size == 0) != (y.size == 0)) return y.size == 0;
            const int c = compareText(x, y);
            if (c != 0) return ascending ? c < 0 : c > 0;
            return a < b;
        });
    }

    result.rows.resize(size_t(n));
    for (qint64 i = 0; i < n; ++i) result.rows[size_t(i)] = rows[perm[size_t(i)]];
    done = n;
    return result;
}

}  // namespace csv
