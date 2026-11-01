#include "ZipWriter.h"

#include <QDateTime>
#include <zlib.h>

namespace {

void put16(QByteArray &b, quint16 v) { b.append(char(v & 0xFF)).append(char(v >> 8)); }
void put32(QByteArray &b, quint32 v) { put16(b, quint16(v & 0xFFFF)); put16(b, quint16(v >> 16)); }

constexpr quint64 kMax32 = 0xFFFFFFFFull;
constexpr int kOutChunk = 256 * 1024;

}  // namespace

ZipWriter::ZipWriter(QIODevice *device) : m_dev(device)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDate d = now.date();
    const QTime t = now.time();
    m_dosTime = quint16((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
    m_dosDate = quint16(((d.year() - 1980) << 9) | (d.month() << 5) | d.day());
}

ZipWriter::~ZipWriter()
{
    if (m_z) {
        deflateEnd(m_z);
        delete m_z;
    }
}

bool ZipWriter::fail(const QString &message)
{
    if (m_error.isEmpty()) m_error = message;
    return false;
}

bool ZipWriter::beginEntry(const QByteArray &name)
{
    if (m_inEntry && !endEntry()) return false;
    Entry e;
    e.name = name;
    e.offset = quint64(m_dev->pos());
    if (e.offset > kMax32) return fail(QStringLiteral("File too large for ZIP"));

    QByteArray h;
    put32(h, 0x04034b50);
    put16(h, 20);            // version needed
    put16(h, 0x0800);        // file names are UTF-8
    put16(h, 8);             // deflate
    put16(h, m_dosTime);
    put16(h, m_dosDate);
    put32(h, 0);             // crc, sizes: filled in by endEntry()
    put32(h, 0);
    put32(h, 0);
    put16(h, quint16(name.size()));
    put16(h, 0);
    h.append(name);
    if (m_dev->write(h) != h.size()) return fail(m_dev->errorString());

    if (!m_z) m_z = new z_stream_s{};
    else deflateEnd(m_z);
    *m_z = z_stream_s{};
    if (deflateInit2(m_z, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return fail(QStringLiteral("zlib init failed"));
    m_out.resize(kOutChunk);
    m_entries.push_back(e);
    m_inEntry = true;
    return true;
}

bool ZipWriter::flushDeflate(int flushMode)
{
    Entry &e = m_entries.back();
    int ret;
    do {
        m_z->next_out = reinterpret_cast<Bytef *>(m_out.data());
        m_z->avail_out = uInt(m_out.size());
        ret = deflate(m_z, flushMode);
        if (ret == Z_STREAM_ERROR) return fail(QStringLiteral("zlib error"));
        const qsizetype produced = m_out.size() - qsizetype(m_z->avail_out);
        if (produced > 0) {
            if (m_dev->write(m_out.constData(), produced) != produced) return fail(m_dev->errorString());
            e.compressed += quint64(produced);
        }
    } while (m_z->avail_out == 0 || (flushMode == Z_FINISH && ret != Z_STREAM_END));
    return true;
}

bool ZipWriter::write(const char *data, qsizetype size)
{
    if (!m_inEntry) return fail(QStringLiteral("No open ZIP entry"));
    Entry &e = m_entries.back();
    e.crc = quint32(crc32(e.crc, reinterpret_cast<const Bytef *>(data), uInt(size)));
    e.uncompressed += quint64(size);
    m_z->next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data));
    m_z->avail_in = uInt(size);
    return flushDeflate(Z_NO_FLUSH);
}

bool ZipWriter::endEntry()
{
    if (!m_inEntry) return true;
    m_inEntry = false;
    m_z->next_in = nullptr;
    m_z->avail_in = 0;
    if (!flushDeflate(Z_FINISH)) return false;

    const Entry &e = m_entries.back();
    if (e.compressed > kMax32 || e.uncompressed > kMax32)
        return fail(QStringLiteral("Entry too large for ZIP (over 4 GB)"));
    QByteArray sizes;
    put32(sizes, e.crc);
    put32(sizes, quint32(e.compressed));
    put32(sizes, quint32(e.uncompressed));
    const qint64 end = m_dev->pos();
    if (!m_dev->seek(qint64(e.offset) + 14) || m_dev->write(sizes) != sizes.size() || !m_dev->seek(end))
        return fail(m_dev->errorString());
    return true;
}

bool ZipWriter::finish()
{
    if (!endEntry()) return false;
    const quint64 cdStart = quint64(m_dev->pos());
    QByteArray cd;
    for (const Entry &e : m_entries) {
        put32(cd, 0x02014b50);
        put16(cd, 20);       // made by
        put16(cd, 20);       // needed
        put16(cd, 0x0800);
        put16(cd, 8);
        put16(cd, m_dosTime);
        put16(cd, m_dosDate);
        put32(cd, e.crc);
        put32(cd, quint32(e.compressed));
        put32(cd, quint32(e.uncompressed));
        put16(cd, quint16(e.name.size()));
        put16(cd, 0);        // extra
        put16(cd, 0);        // comment
        put16(cd, 0);        // disk
        put16(cd, 0);        // internal attributes
        put32(cd, 0);        // external attributes
        put32(cd, quint32(e.offset));
        cd.append(e.name);
    }
    QByteArray eocd;
    put32(eocd, 0x06054b50);
    put16(eocd, 0);
    put16(eocd, 0);
    put16(eocd, quint16(m_entries.size()));
    put16(eocd, quint16(m_entries.size()));
    put32(eocd, quint32(cd.size()));
    put32(eocd, quint32(cdStart));
    put16(eocd, 0);
    if (cdStart > kMax32) return fail(QStringLiteral("File too large for ZIP"));
    if (m_dev->write(cd) != cd.size() || m_dev->write(eocd) != eocd.size()) return fail(m_dev->errorString());
    return true;
}
