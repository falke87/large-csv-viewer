#include "RowFilter.h"

#include "CsvDocument.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>
#include <thread>

namespace csv {

QStringList operatorNames()
{
    return {QCoreApplication::translate("Filter", "contains"),
            QCoreApplication::translate("Filter", "does not contain"),
            QCoreApplication::translate("Filter", "equals"),
            QCoreApplication::translate("Filter", "does not equal"),
            QCoreApplication::translate("Filter", "starts with"),
            QCoreApplication::translate("Filter", "ends with"),
            QStringLiteral("> greater than"), QStringLiteral(">= at least"),
            QStringLiteral("< less than"), QStringLiteral("<= at most"),
            QCoreApplication::translate("Filter", "is empty"),
            QCoreApplication::translate("Filter", "is not empty"),
            QCoreApplication::translate("Filter", "matches regex")};
}

bool operatorNeedsValue(Op op) { return op != Op::IsEmpty && op != Op::IsNotEmpty; }

QString validateConditions(const std::vector<Condition> &conditions)
{
    for (const auto &c : conditions) {
        if (c.op == Op::Regex) {
            QRegularExpression re(c.value);
            if (!re.isValid())
                return QCoreApplication::translate("Filter", "Invalid regular expression \"%1\": %2")
                    .arg(c.value, re.errorString());
        }
        const bool compare = c.op == Op::Greater || c.op == Op::GreaterOrEqual || c.op == Op::Less ||
                             c.op == Op::LessOrEqual;
        if (compare && c.value.trimmed().isEmpty())
            return QCoreApplication::translate("Filter", "Please enter a value to compare with.");
    }
    return {};
}

namespace {

inline char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }

bool startsWithCI(QByteArrayView hay, QByteArrayView lowNeedle)
{
    if (hay.size() < lowNeedle.size()) return false;
    for (qsizetype i = 0; i < lowNeedle.size(); ++i)
        if (lowerAscii(hay[i]) != lowNeedle[i]) return false;
    return true;
}

bool containsCI(QByteArrayView hay, QByteArrayView lowNeedle)
{
    const qsizetype n = lowNeedle.size();
    if (n == 0) return true;
    const char first = lowNeedle[0];
    for (qsizetype i = 0; i + n <= hay.size(); ++i)
        if (lowerAscii(hay[i]) == first && startsWithCI(hay.sliced(i), lowNeedle)) return true;
    return false;
}

QByteArrayView trimmedView(QByteArrayView v) { return v.trimmed(); }

// One condition, prepared for fast checking. Each thread gets its own copy.
struct Compiled {
    int column = -1;
    Op op = Op::Contains;          // positive form (NotContains -> Contains, NotEquals -> Equals)
    bool negate = false;
    bool ascii = true;             // needle is plain ASCII -> compare bytes directly
    QByteArray low;                // lower-case ASCII needle
    QByteArray anchor;             // longest ASCII part of a non-ASCII needle ("wei" in "Weiß")
    QString text;                  // needle as text (for non-ASCII or text comparison)
    bool numeric = false;
    double number = 0;
    QRegularExpression re;

    explicit Compiled(const Condition &c) : column(c.column), op(c.op)
    {
        if (op == Op::NotContains) { op = Op::Contains; negate = true; }
        if (op == Op::NotEquals) { op = Op::Equals; negate = true; }
        text = (op == Op::Regex) ? c.value : c.value.trimmed();
        for (QChar ch : text) ascii = ascii && ch.unicode() < 0x80;
        low = text.toLatin1().toLower();
        if (!ascii) {
            // Quick pre-check for non-ASCII needles: decoding every field is slow, so first
            // look for the longest plain ASCII part with a byte search.
            QByteArray run;
            for (QChar ch : text.toLower()) {
                if (ch.unicode() < 0x80) run.append(char(ch.unicode()));
                else { if (run.size() > anchor.size()) anchor = run; run.clear(); }
            }
            if (run.size() > anchor.size()) anchor = run;
            if (anchor.size() < 2) anchor.clear();
        }
        const QByteArray utf8 = text.toUtf8();
        numeric = parseNumber(QByteArrayView(utf8), &number);
        if (op == Op::Regex)
            re = QRegularExpression(c.value, QRegularExpression::CaseInsensitiveOption |
                                                 QRegularExpression::UseUnicodePropertiesOption);
    }

    // Does one field satisfy the positive form of the condition?
    bool test(const CsvDocument &doc, QByteArrayView raw) const
    {
        const QByteArrayView v = trimmedView(raw);
        const bool textOp = op == Op::Contains || op == Op::StartsWith || op == Op::EndsWith ||
                            (op == Op::Equals && !numeric);
        if (textOp && !anchor.isEmpty() && !containsCI(raw, QByteArrayView(anchor))) return false;
        switch (op) {
        case Op::IsEmpty: return v.isEmpty();
        case Op::IsNotEmpty: return !v.isEmpty();
        case Op::Contains:
            return ascii ? containsCI(raw, QByteArrayView(low))
                         : doc.decode(raw).contains(text, Qt::CaseInsensitive);
        case Op::StartsWith:
            return ascii ? startsWithCI(v, QByteArrayView(low))
                         : doc.decode(v).startsWith(text, Qt::CaseInsensitive);
        case Op::EndsWith:
            if (ascii) return v.size() >= low.size() && startsWithCI(v.last(low.size()), QByteArrayView(low));
            return doc.decode(v).endsWith(text, Qt::CaseInsensitive);
        case Op::Equals: {
            double x;
            if (numeric && parseNumber(v, &x)) return x == number;
            if (ascii) return v.size() == low.size() && startsWithCI(v, QByteArrayView(low));
            return doc.decode(v).compare(text, Qt::CaseInsensitive) == 0;
        }
        case Op::Greater: case Op::GreaterOrEqual: case Op::Less: case Op::LessOrEqual: {
            if (v.isEmpty()) return false;
            int cmp;
            double x;
            if (numeric) {
                if (!parseNumber(v, &x)) return false;
                cmp = x < number ? -1 : (x > number ? 1 : 0);
            } else {
                cmp = doc.decode(v).compare(text, Qt::CaseInsensitive);   // e.g. ISO dates
            }
            if (op == Op::Greater) return cmp > 0;
            if (op == Op::GreaterOrEqual) return cmp >= 0;
            if (op == Op::Less) return cmp < 0;
            return cmp <= 0;
        }
        case Op::Regex: return re.match(doc.decode(raw)).hasMatch();
        default: return false;
        }
    }

    bool matches(const CsvDocument &doc, const FieldList &fields, QByteArray &scratch) const
    {
        bool hit = false;
        if (column < 0) {
            for (const auto &f : fields)
                if (test(doc, fieldBytes(f, scratch))) { hit = true; break; }
        } else {
            hit = column < fields.size() ? test(doc, fieldBytes(fields[column], scratch))
                                         : test(doc, QByteArrayView());
        }
        return hit != negate;
    }
};

}  // namespace

std::vector<quint32> filterRows(const CsvDocument &doc, const std::vector<Condition> &conditions,
                                std::atomic<qint64> &done, const std::atomic<bool> &cancel)
{
    const qint64 rows = doc.rowCount();
    const int threads = int(std::clamp<unsigned>(std::thread::hardware_concurrency(), 1u, 16u));
    std::vector<std::vector<quint32>> parts(static_cast<size_t>(threads));
    std::vector<std::thread> pool;

    for (int t = 0; t < threads; ++t) {
        const qint64 from = rows * t / threads;
        const qint64 to = rows * (t + 1) / threads;
        pool.emplace_back([&, t, from, to] {
            std::vector<Compiled> compiled;
            for (const auto &c : conditions) compiled.emplace_back(c);
            FieldList fields;
            QByteArray scratch;
            auto &out = parts[size_t(t)];
            for (qint64 r = from; r < to; ++r) {
                if (((r - from) & 8191) == 8191) {
                    done += 8192;
                    if (cancel) return;
                }
                doc.splitRow(r, fields);
                bool ok = true;
                for (const auto &c : compiled)
                    if (!c.matches(doc, fields, scratch)) { ok = false; break; }
                if (ok) out.push_back(quint32(r));
            }
        });
    }
    for (auto &th : pool) th.join();

    std::vector<quint32> result;
    size_t total = 0;
    for (const auto &p : parts) total += p.size();
    result.reserve(total);
    for (const auto &p : parts) result.insert(result.end(), p.begin(), p.end());
    done = rows;
    return result;
}

}  // namespace csv
