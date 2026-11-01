#include "CsvDocument.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>

namespace csv {

namespace {

// Windows-1252 differs from Latin-1 only in 0x80..0x9F (euro sign, quotes, dashes ...).
constexpr char16_t kCp1252[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

constexpr qint64 kSampleBytes = 2 * 1024 * 1024;   // used for detection and the header
constexpr int kSampleRows = 1000;

}  // namespace

CsvDocument::CsvDocument(QObject *parent) : QObject(parent) {}

CsvDocument::~CsvDocument() { close(); }

bool CsvDocument::open(const QString &path, const OpenOptions &options, QString *error)
{
    close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        *error = tr("Cannot open the file: %1").arg(m_file.errorString());
        return false;
    }
    m_size = m_file.size();
    if (m_size == 0) {
        *error = tr("The file is empty.");
        m_file.close();
        return false;
    }
    uchar *map = m_file.map(0, m_size);
    if (!map) {
        *error = tr("Cannot map the file into memory: %1").arg(m_file.errorString());
        m_file.close();
        return false;
    }
    m_data = reinterpret_cast<const char *>(map);
    m_path = QFileInfo(path).absoluteFilePath();

    // Byte order mark and encoding.
    const auto *u = reinterpret_cast<const unsigned char *>(m_data);
    m_dataStart = 0;
    if (m_size >= 2 && ((u[0] == 0xFF && u[1] == 0xFE) || (u[0] == 0xFE && u[1] == 0xFF))) {
        *error = tr("This file is UTF-16. Please save it as UTF-8 and open it again.");
        close();
        return false;
    }
    const bool bom = m_size >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF;
    if (bom) m_dataStart = 3;
    const qint64 sampleSize = std::min(m_size - m_dataStart, kSampleBytes);
    const char *sample = m_data + m_dataStart;
    if (options.encoding != Encoding::Auto) m_encoding = options.encoding;
    else m_encoding = (bom || isValidUtf8(sample, sampleSize)) ? Encoding::Utf8 : Encoding::Windows1252;

    m_delimiter = options.delimiter ? options.delimiter : detectDelimiter(sample, sampleSize);
    m_hasHeader = options.hasHeader;

    // Read the first rows directly: column count, header names, numeric columns.
    std::vector<qint64> starts{m_dataStart};
    RecordScanner scanner(m_delimiter);
    scanner.scan(sample, sampleSize, m_dataStart, starts);
    if (sampleSize == m_size - m_dataStart) starts.push_back(m_size + 1);
    const int sampleRecords = std::min<int>(int(starts.size()) - 1, kSampleRows + 1);

    std::vector<int> numericHits, nonEmpty;
    int columns = 0;
    FieldList fields;
    QStringList firstRecord;
    for (int r = 0; r < sampleRecords; ++r) {
        const char *b = m_data + starts[size_t(r)];
        const char *e = m_data + std::min(starts[size_t(r) + 1] - 1, m_size);
        while (e > b && (e[-1] == '\n' || e[-1] == '\r')) --e;
        if (isBlank(b, e)) continue;
        splitRecord(b, e, m_delimiter, fields);
        columns = std::max(columns, int(fields.size()));
        if (firstRecord.isEmpty()) {
            for (const auto &f : fields) firstRecord << fieldText(f).trimmed();
            if (m_hasHeader) continue;
        }
        numericHits.resize(size_t(columns));
        nonEmpty.resize(size_t(columns));
        QByteArray scratch;
        double v;
        for (int c = 0; c < fields.size(); ++c) {
            const QByteArrayView bytes = fieldBytes(fields[c], scratch);
            if (bytes.trimmed().isEmpty()) continue;
            ++nonEmpty[size_t(c)];
            if (parseNumber(bytes, &v)) ++numericHits[size_t(c)];
        }
    }
    if (columns == 0) columns = 1;
    m_headers.clear();
    for (int c = 0; c < columns; ++c) {
        const QString name = (m_hasHeader && c < firstRecord.size()) ? firstRecord.at(c) : QString();
        m_headers << (name.isEmpty() ? tr("Column %1").arg(c + 1) : name);
    }
    m_numeric.assign(size_t(columns), false);
    for (int c = 0; c < columns && c < int(nonEmpty.size()); ++c)
        m_numeric[size_t(c)] = nonEmpty[size_t(c)] > 0 && numericHits[size_t(c)] * 10 >= nonEmpty[size_t(c)] * 9;

    // Index the whole file in the background.
    m_starts.clear();
    m_boundary = m_dataStart;
    m_indexComplete = false;
    m_stop = false;
    m_progress = 0;
    const int generation = ++m_generation;
    m_indexer = std::thread([this, generation] { indexThread(generation); });
    return true;
}

void CsvDocument::close()
{
    m_stop = true;
    if (m_indexer.joinable()) m_indexer.join();
    ++m_generation;
    if (m_data) {
        m_file.unmap(reinterpret_cast<uchar *>(const_cast<char *>(m_data)));
        m_data = nullptr;
    }
    m_file.close();
    m_starts.clear();
    m_starts.shrink_to_fit();
    m_headers.clear();
    m_numeric.clear();
    m_size = 0;
    m_indexComplete = false;
}

void CsvDocument::waitForIndex()
{
    if (m_indexer.joinable()) m_indexer.join();
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);   // deliver the queued chunks
}

void CsvDocument::indexThread(int generation)
{
    QElapsedTimer timer, sincePost;
    timer.start();
    sincePost.start();
    constexpr qint64 kBlock = 4 * 1024 * 1024;
    RecordScanner scanner(m_delimiter);
    std::vector<qint64> found;
    found.reserve(1 << 17);
    auto pending = std::make_shared<std::vector<qint64>>();
    bool firstPost = true;
    qint64 current = m_dataStart;   // start of the record whose end is not known yet

    auto post = [&](bool last) {
        const double seconds = timer.nsecsElapsed() / 1e9;
        const qint64 boundary = last ? m_size + 1 : current;
        QMetaObject::invokeMethod(this, [this, generation, chunk = pending, boundary, last, seconds] {
            appendStarts(generation, chunk, boundary, last, seconds);
        }, Qt::QueuedConnection);
        pending = std::make_shared<std::vector<qint64>>();
        firstPost = false;
        sincePost.restart();
    };

    for (qint64 pos = m_dataStart; pos < m_size && !m_stop; pos += kBlock) {
        const qint64 n = std::min(kBlock, m_size - pos);
        found.clear();
        scanner.scan(m_data + pos, n, pos, found);
        for (qint64 next : found) {
            if (!isBlank(m_data + current, m_data + next - 1)) pending->push_back(current);
            current = next;
        }
        m_progress = int((pos + n) * 1000 / m_size);
        if ((firstPost && pending->size() >= 2000) || pending->size() >= 500000 || sincePost.elapsed() >= 150)
            post(false);
    }
    if (m_stop) return;
    if (current < m_size && !isBlank(m_data + current, m_data + m_size)) pending->push_back(current);
    m_progress = 1000;
    post(true);
}

void CsvDocument::appendStarts(int generation, std::shared_ptr<std::vector<qint64>> chunk,
                               qint64 boundary, bool last, double seconds)
{
    if (generation != m_generation) return;
    m_starts.insert(m_starts.end(), chunk->begin(), chunk->end());
    m_boundary = boundary;
    if (last) {
        m_indexComplete = true;
        m_indexSeconds = seconds;
    }
    emit rowsAdded(rowCount());
    if (last) emit indexFinished(rowCount(), seconds);
}

qint64 CsvDocument::rowCount() const
{
    return std::max<qint64>(0, qint64(m_starts.size()) - (m_hasHeader ? 1 : 0));
}

bool CsvDocument::columnLooksNumeric(int column) const
{
    return column >= 0 && size_t(column) < m_numeric.size() && m_numeric[size_t(column)];
}

void CsvDocument::rowBytes(qint64 row, const char **begin, const char **end) const
{
    const size_t i = size_t(row + (m_hasHeader ? 1 : 0));
    const qint64 next = i + 1 < m_starts.size() ? m_starts[i + 1] : m_boundary;
    const char *b = m_data + m_starts[i];
    const char *e = m_data + std::min(next - 1, m_size);
    while (e > b && (e[-1] == '\n' || e[-1] == '\r')) --e;
    *begin = b;
    *end = e;
}

void CsvDocument::splitRow(qint64 row, FieldList &fields) const
{
    const char *b, *e;
    rowBytes(row, &b, &e);
    splitRecord(b, e, m_delimiter, fields);
}

QStringList CsvDocument::rowValues(qint64 row) const
{
    FieldList fields;
    splitRow(row, fields);
    QStringList out;
    out.reserve(fields.size());
    for (const auto &f : fields) out << fieldText(f);
    return out;
}

QString CsvDocument::fieldText(const FieldRef &field) const
{
    QByteArray scratch;
    return decode(fieldBytes(field, scratch));
}

QString CsvDocument::decode(QByteArrayView bytes) const
{
    if (m_encoding == Encoding::Utf8) return QString::fromUtf8(bytes);
    QString s = QString::fromLatin1(bytes);
    for (QChar &ch : s)
        if (ch.unicode() >= 0x80 && ch.unicode() <= 0x9F) ch = QChar(kCp1252[ch.unicode() - 0x80]);
    return s;
}

}  // namespace csv
