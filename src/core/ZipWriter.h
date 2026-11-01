// Minimal streaming ZIP writer (deflate, via zlib). Enough to write .xlsx files,
// which are ZIP archives of XML files. Entries are compressed while they are written,
// so even a sheet with a million rows never has to fit into memory.
#pragma once

#include <QByteArray>
#include <QIODevice>
#include <vector>

struct z_stream_s;

class ZipWriter
{
public:
    explicit ZipWriter(QIODevice *device);
    ~ZipWriter();

    bool beginEntry(const QByteArray &name);
    bool write(const char *data, qsizetype size);
    bool write(const QByteArray &data) { return write(data.constData(), data.size()); }
    bool endEntry();
    bool finish();                  // writes the central directory

    QString errorString() const { return m_error; }

private:
    struct Entry {
        QByteArray name;
        quint32 crc = 0;
        quint64 compressed = 0, uncompressed = 0;
        quint64 offset = 0;
    };
    bool flushDeflate(int flushMode);
    bool fail(const QString &message);

    QIODevice *m_dev;
    std::vector<Entry> m_entries;
    z_stream_s *m_z = nullptr;
    QByteArray m_out;
    bool m_inEntry = false;
    quint16 m_dosTime = 0, m_dosDate = 0;
    QString m_error;
};
