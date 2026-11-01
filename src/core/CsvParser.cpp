#include "CsvParser.h"

#include <array>
#include <charconv>
#include <cstring>

namespace csv {

void splitRecord(const char *begin, const char *end, char delimiter, FieldList &out)
{
    out.clear();
    const char *p = begin;
    for (;;) {
        FieldRef f;
        if (p < end && *p == '"') {
            // Quoted field: runs until a quote that is not doubled.
            const char *start = ++p;
            while (p < end) {
                if (*p == '"') {
                    if (p + 1 < end && p[1] == '"') { p += 2; continue; }
                    break;
                }
                ++p;
            }
            f.data = start;
            f.size = p - start;
            f.quoted = true;
            if (p < end) ++p;                              // closing quote
            while (p < end && *p != delimiter) ++p;        // tolerate junk after the quote
        } else {
            const char *start = p;
            while (p < end && *p != delimiter) ++p;
            f.data = start;
            f.size = p - start;
        }
        out.append(f);
        if (p >= end) break;
        ++p;                                               // delimiter
        if (p == end) { out.append(FieldRef{p, 0, false}); break; }   // trailing empty field
    }
}

QByteArrayView fieldBytes(const FieldRef &field, QByteArray &scratch)
{
    if (!field.quoted || !std::memchr(field.data, '"', size_t(field.size)))
        return QByteArrayView(field.data, field.size);
    scratch.resize(0);
    scratch.reserve(field.size);
    for (qsizetype i = 0; i < field.size; ++i) {
        scratch.append(field.data[i]);
        if (field.data[i] == '"' && i + 1 < field.size && field.data[i + 1] == '"') ++i;
    }
    return QByteArrayView(scratch);
}

void RecordScanner::scan(const char *data, qsizetype size, qint64 base, std::vector<qint64> &starts)
{
    qsizetype i = 0;
    // Fast path: outside quotes and no quote character in this block -> only look for '\n'.
    if ((m_state == FieldStart || m_state == Unquoted) && !std::memchr(data, '"', size_t(size))) {
        const char *p = data;
        const char *end = data + size;
        while (p < end) {
            const char *nl = static_cast<const char *>(std::memchr(p, '\n', size_t(end - p)));
            if (!nl) break;
            starts.push_back(base + (nl - data) + 1);
            p = nl + 1;
        }
        if (size > 0) {
            const char last = data[size - 1];
            m_state = (last == '\n' || last == m_delimiter) ? FieldStart : Unquoted;
        }
        return;
    }

    State s = m_state;
    const char d = m_delimiter;
    for (; i < size; ++i) {
        const char c = data[i];
        switch (s) {
        case FieldStart:
            if (c == '"') s = Quoted;
            else if (c == '\n') starts.push_back(base + i + 1);
            else if (c != d) s = Unquoted;
            break;
        case Unquoted:
            if (c == d) s = FieldStart;
            else if (c == '\n') { starts.push_back(base + i + 1); s = FieldStart; }
            break;
        case Quoted:
            if (c == '"') s = QuoteInQuoted;
            break;
        case QuoteInQuoted:
            if (c == '"') s = Quoted;                       // escaped quote ""
            else if (c == d) s = FieldStart;
            else if (c == '\n') { starts.push_back(base + i + 1); s = FieldStart; }
            else s = Unquoted;                              // malformed, keep going
            break;
        }
    }
    m_state = s;
}

char detectDelimiter(const char *data, qsizetype size)
{
    static constexpr std::array<char, 4> candidates = {',', ';', '\t', '|'};
    constexpr int maxLines = 50;
    int bestScore = -1, bestMode = 0;
    char best = ',';

    for (char d : candidates) {
        // Count the delimiter per line (outside quotes) for the first lines.
        std::array<int, maxLines> counts{};
        int lines = 0, count = 0;
        bool inQuotes = false;
        for (qsizetype i = 0; i < size && lines < maxLines; ++i) {
            const char c = data[i];
            if (c == '"') inQuotes = !inQuotes;
            else if (!inQuotes && c == d) ++count;
            else if (!inQuotes && c == '\n') { counts[size_t(lines++)] = count; count = 0; }
        }
        if (lines < maxLines && count > 0) counts[size_t(lines++)] = count;
        if (lines == 0) continue;

        // Score: how many lines share the most common non-zero count.
        int mode = 0, modeLines = 0;
        for (int a = 0; a < lines; ++a) {
            if (counts[size_t(a)] == 0) continue;
            int same = 0;
            for (int b = 0; b < lines; ++b) same += counts[size_t(b)] == counts[size_t(a)];
            if (same > modeLines || (same == modeLines && counts[size_t(a)] > mode)) {
                modeLines = same;
                mode = counts[size_t(a)];
            }
        }
        if (modeLines > bestScore || (modeLines == bestScore && mode > bestMode)) {
            bestScore = modeLines;
            bestMode = mode;
            best = d;
        }
    }
    return best;
}

bool isValidUtf8(const char *data, qsizetype size)
{
    const auto *s = reinterpret_cast<const unsigned char *>(data);
    qsizetype i = 0;
    while (i < size) {
        const unsigned char c = s[i];
        int extra;
        if (c < 0x80) { ++i; continue; }
        else if ((c & 0xE0) == 0xC0 && c >= 0xC2) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4) extra = 3;
        else return false;
        if (i + extra >= size) return true;                 // cut off at the end of the sample
        for (int k = 1; k <= extra; ++k)
            if ((s[i + k] & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}

bool parseNumber(QByteArrayView text, double *out)
{
    const char *b = text.data();
    const char *e = b + text.size();
    while (b < e && (*b == ' ' || *b == '\t')) ++b;
    while (e > b && (e[-1] == ' ' || e[-1] == '\t')) --e;
    if (b == e || e - b > 40) return false;

    int dots = 0, commas = 0, digits = 0;
    const char *lastDot = nullptr, *lastComma = nullptr;
    for (const char *p = b; p < e; ++p) {
        const char c = *p;
        if (c >= '0' && c <= '9') ++digits;
        else if (c == '.') { ++dots; lastDot = p; }
        else if (c == ',') { ++commas; lastComma = p; }
        else if ((c == '-' || c == '+') && p == b) continue;
        else return false;
    }
    if (digits == 0) return false;

    // Decide which character is the decimal sign; the other one groups thousands.
    char decimal = 0;
    if (dots && commas) decimal = lastDot > lastComma ? '.' : ',';
    else if (dots == 1) decimal = '.';
    else if (commas == 1) decimal = ',';
    if ((decimal == '.' && dots > 1) || (decimal == ',' && commas > 1)) return false;

    char buf[48];
    int n = 0;
    for (const char *p = b; p < e; ++p) {
        const char c = *p;
        if (c == '+') continue;
        if (c == '.' || c == ',') {
            if (c == decimal) buf[n++] = '.';
            continue;                                       // thousands separator
        }
        buf[n++] = c;
    }
    double v = 0;
    const auto res = std::from_chars(buf, buf + n, v);
    if (res.ec != std::errc() || res.ptr != buf + n) return false;
    *out = v;
    return true;
}

bool isBlank(const char *begin, const char *end)
{
    for (const char *p = begin; p < end; ++p)
        if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') return false;
    return true;
}

}  // namespace csv
