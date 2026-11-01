// A CSV file opened for viewing. The file is memory-mapped (no copy in RAM), and a
// background thread finds where every row starts. Rows are only parsed when needed.
#pragma once

#include "CsvParser.h"

#include <QFile>
#include <QObject>
#include <QStringList>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace csv {

enum class Encoding { Auto, Utf8, Windows1252 };

struct OpenOptions {
    char delimiter = 0;              // 0 = detect
    Encoding encoding = Encoding::Auto;
    bool hasHeader = true;
};

class CsvDocument : public QObject
{
    Q_OBJECT
public:
    explicit CsvDocument(QObject *parent = nullptr);
    ~CsvDocument() override;

    // Opens the file, detects format and header, and starts indexing in the background.
    bool open(const QString &path, const OpenOptions &options, QString *error);
    void close();

    // Blocks until indexing is finished (used by tests and the command line).
    void waitForIndex();

    bool isOpen() const { return m_data != nullptr; }
    bool isIndexComplete() const { return m_indexComplete; }
    QString path() const { return m_path; }
    qint64 fileSize() const { return m_size; }
    char delimiter() const { return m_delimiter; }
    Encoding encoding() const { return m_encoding; }
    bool hasHeader() const { return m_hasHeader; }
    double indexSeconds() const { return m_indexSeconds; }
    int indexProgress() const { return m_progress.load(); }   // 0..1000

    // Data rows indexed so far (header not counted).
    qint64 rowCount() const;
    int columnCount() const { return int(m_headers.size()); }
    const QStringList &headers() const { return m_headers; }
    bool columnLooksNumeric(int column) const;

    // Raw bytes of a data row without the line break.
    void rowBytes(qint64 row, const char **begin, const char **end) const;
    void splitRow(qint64 row, FieldList &fields) const;
    QStringList rowValues(qint64 row) const;

    // Text of a field, decoded with the file encoding and "" unescaped.
    QString fieldText(const FieldRef &field) const;
    QString decode(QByteArrayView bytes) const;

signals:
    void rowsAdded(qint64 rowCount);
    void indexFinished(qint64 rowCount, double seconds);

private:
    void indexThread(int generation);
    void appendStarts(int generation, std::shared_ptr<std::vector<qint64>> chunk, qint64 boundary,
                      bool last, double seconds);

    QFile m_file;
    QString m_path;
    const char *m_data = nullptr;    // mapped file
    qint64 m_size = 0;
    qint64 m_dataStart = 0;          // after a byte order mark
    char m_delimiter = ',';
    Encoding m_encoding = Encoding::Utf8;
    bool m_hasHeader = true;
    QStringList m_headers;
    std::vector<bool> m_numeric;

    std::vector<qint64> m_starts;    // file offset of every record (header first)
    qint64 m_boundary = 0;           // start of the first record that is not indexed yet
    bool m_indexComplete = false;
    double m_indexSeconds = 0;

    std::thread m_indexer;
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_progress{0};
    int m_generation = 0;            // ignores queued chunks from a closed file
};

}  // namespace csv
