// Low-level CSV helpers: record scanning, field splitting, detection of delimiter and
// encoding, number parsing. Everything works on raw bytes so it is fast on huge files.
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QVarLengthArray>
#include <vector>

namespace csv {

// One field of a record, pointing into the file data (no copy).
struct FieldRef {
    const char *data = nullptr;  // content without the surrounding quotes
    qsizetype size = 0;
    bool quoted = false;         // quoted fields may contain "" that must be unescaped
};
using FieldList = QVarLengthArray<FieldRef, 32>;

// Splits one record (line break already removed) into fields.
// Follows RFC 4180: quotes only count at the start of a field, "" is an escaped quote.
void splitRecord(const char *begin, const char *end, char delimiter, FieldList &out);

// Bytes of a field with "" unescaped. Uses `scratch` only when a copy is needed.
QByteArrayView fieldBytes(const FieldRef &field, QByteArray &scratch);

// Finds where records start. Feed the file in blocks; the state is kept between calls,
// so a quoted field may contain line breaks and may cross block borders.
class RecordScanner
{
public:
    explicit RecordScanner(char delimiter) : m_delimiter(delimiter) {}

    // Scans `size` bytes at absolute file offset `base` and appends the offset of every
    // record start that follows a line break outside quotes.
    void scan(const char *data, qsizetype size, qint64 base, std::vector<qint64> &starts);

private:
    enum State { FieldStart, Unquoted, Quoted, QuoteInQuoted };
    char m_delimiter;
    State m_state = FieldStart;
};

// Picks the delimiter (',', ';', '\t' or '|') that splits the first lines most consistently.
char detectDelimiter(const char *data, qsizetype size);

// True if the bytes are valid UTF-8 (a character cut off at the very end is accepted).
bool isValidUtf8(const char *data, qsizetype size);

// Parses "1234", "-12.5", "1,234.50", "1.234,50", "12,5" (comma as decimal sign).
// Surrounding spaces are allowed. Returns false for anything else.
bool parseNumber(QByteArrayView text, double *out);

// True if a record (without line break) is empty or only whitespace.
bool isBlank(const char *begin, const char *end);

}  // namespace csv
