// Unit tests for the CSV core (parser, index, filter, sort, export).   ctest / ./tst_core
#include "core/CsvDocument.h"
#include "core/CsvParser.h"
#include "core/Exporter.h"
#include "core/RowFilter.h"
#include "core/RowSorter.h"
#include "core/SampleGenerator.h"
#include "core/XlsxWriter.h"

#include <QTemporaryDir>
#include <QtTest>

#include <numeric>

using namespace csv;

class TestCore : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString write(const QString &name, const QByteArray &content)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(content);
        return path;
    }

    static QStringList split(const QByteArray &line, char d = ',')
    {
        FieldList fields;
        splitRecord(line.constData(), line.constData() + line.size(), d, fields);
        QStringList out;
        QByteArray scratch;
        for (const auto &f : fields) out << QString::fromUtf8(fieldBytes(f, scratch));
        return out;
    }

    std::vector<quint32> filter(const CsvDocument &doc, std::vector<Condition> conds)
    {
        std::atomic<qint64> done{0};
        std::atomic<bool> cancel{false};
        return filterRows(doc, conds, done, cancel);
    }

    std::vector<quint32> allRows(const CsvDocument &doc)
    {
        std::vector<quint32> rows(size_t(doc.rowCount()));
        std::iota(rows.begin(), rows.end(), 0u);
        return rows;
    }

private slots:
    void splitFields()
    {
        QCOMPARE(split("a,b,c"), (QStringList{"a", "b", "c"}));
        QCOMPARE(split("a,,c,"), (QStringList{"a", "", "c", ""}));
        QCOMPARE(split("\"Müller, Anna\",42"), (QStringList{"Müller, Anna", "42"}));
        QCOMPARE(split("\"say \"\"hi\"\"\",x"), (QStringList{"say \"hi\"", "x"}));
        QCOMPARE(split("5'11\",tall"), (QStringList{"5'11\"", "tall"}));         // quote inside a field
        QCOMPARE(split("a;b;\"c;d\"", ';'), (QStringList{"a", "b", "c;d"}));
        QCOMPARE(split(""), (QStringList{""}));
    }

    void scannerKeepsQuotedLineBreaks()
    {
        const QByteArray data = "id,note\r\n1,\"line one\nline two\"\r\n2,\"say \"\"x\"\"\"\n3,plain";
        // Feed one byte at a time: state must survive block borders.
        RecordScanner sc(',');
        std::vector<qint64> starts;
        for (int i = 0; i < data.size(); ++i) sc.scan(data.constData() + i, 1, i, starts);
        QCOMPARE(starts.size(), size_t(3));
        QCOMPARE(data.mid(int(starts[0]), 2), QByteArray("1,"));
        QCOMPARE(data.mid(int(starts[1]), 2), QByteArray("2,"));
        QCOMPARE(data.mid(int(starts[2]), 2), QByteArray("3,"));

        RecordScanner whole(',');                   // and in one block (fast path not allowed: quotes)
        std::vector<qint64> starts2;
        whole.scan(data.constData(), data.size(), 0, starts2);
        QCOMPARE(starts2, starts);
    }

    void delimiterDetection()
    {
        const QByteArray semi = "Name;Betrag;Datum\nMüller, Anna;12,50;01.02.2026\nSchmidt;3,99;02.02.2026\n";
        QCOMPARE(detectDelimiter(semi.constData(), semi.size()), ';');
        const QByteArray comma = "a,b,c\n1,2,3\n4,5,6\n";
        QCOMPARE(detectDelimiter(comma.constData(), comma.size()), ',');
        const QByteArray tab = "a\tb\n1\t2\n";
        QCOMPARE(detectDelimiter(tab.constData(), tab.size()), '\t');
    }

    void numbers()
    {
        double v = 0;
        QVERIFY(parseNumber("1234", &v) && v == 1234);
        QVERIFY(parseNumber(" -12.5 ", &v) && v == -12.5);
        QVERIFY(parseNumber("1,234.50", &v) && v == 1234.5);
        QVERIFY(parseNumber("1.234,50", &v) && v == 1234.5);
        QVERIFY(parseNumber("12,5", &v) && v == 12.5);
        QVERIFY(!parseNumber("12a", &v));
        QVERIFY(!parseNumber("", &v));
        QVERIFY(!parseNumber("2026-01-05", &v));
        QVERIFY(!parseNumber("1.2.3,4,5", &v));
    }

    void documentBasics()
    {
        // BOM, CRLF, blank lines, quoted line break, no line break at the end.
        const QByteArray data = "\xEF\xBB\xBFid,name,amount\r\n1,Anna,10.5\r\n\r\n2,\"Bob\nJr.\",3\r\n3,Chloé,\r\n4,Dana,7";
        CsvDocument doc;
        QString err;
        QVERIFY2(doc.open(write("basic.csv", data), {}, &err), qPrintable(err));
        doc.waitForIndex();
        QVERIFY(doc.isIndexComplete());
        QCOMPARE(doc.rowCount(), 4);
        QCOMPARE(doc.headers(), (QStringList{"id", "name", "amount"}));
        QCOMPARE(doc.rowValues(0), (QStringList{"1", "Anna", "10.5"}));
        QCOMPARE(doc.rowValues(1), (QStringList{"2", "Bob\nJr.", "3"}));
        QCOMPARE(doc.rowValues(2), (QStringList{"3", "Chloé", ""}));
        QCOMPARE(doc.rowValues(3), (QStringList{"4", "Dana", "7"}));
        QVERIFY(doc.columnLooksNumeric(2));
        QVERIFY(!doc.columnLooksNumeric(1));
        QCOMPARE(doc.encoding(), Encoding::Utf8);
    }

    void windows1252()
    {
        const QByteArray data = "Name;Preis\nM\xFCller;\x80 12,50\nGro\xDF;3\n";   // ü, €, ß in Windows-1252
        CsvDocument doc;
        QString err;
        QVERIFY(doc.open(write("cp1252.csv", data), {}, &err));
        doc.waitForIndex();
        QCOMPARE(doc.encoding(), Encoding::Windows1252);
        QCOMPARE(doc.delimiter(), ';');
        QCOMPARE(doc.rowValues(0), (QStringList{QStringLiteral("Müller"), QStringLiteral("€ 12,50")}));
        QCOMPARE(doc.rowValues(1).first(), QStringLiteral("Groß"));
        // Search with a non-ASCII word in a Windows-1252 file.
        QCOMPARE(filter(doc, {{0, Op::Contains, QStringLiteral("MÜLL")}}), std::vector<quint32>{0});
    }

    void noHeader()
    {
        CsvDocument doc;
        QString err;
        OpenOptions o;
        o.hasHeader = false;
        QVERIFY(doc.open(write("nohead.csv", "1,2\n3,4\n"), o, &err));
        doc.waitForIndex();
        QCOMPARE(doc.rowCount(), 2);
        QCOMPARE(doc.headers(), (QStringList{"Column 1", "Column 2"}));
    }

    void filters()
    {
        const QByteArray data =
            "id,name,city,amount,date,note\n"
            "1,Anna Müller,München,10.50,2026-01-05,\n"
            "2,Bob Smith,Berlin,200,2026-02-10,VIP\n"
            "3,Chloé Dubois,Paris,5.00,2025-12-31,\n"
            "4,DANA white,Berlin,,2026-03-01,call first\n"
            "5,Emil,Köln,1.234,2026-01-20,\n";
        CsvDocument doc;
        QString err;
        QVERIFY(doc.open(write("filter.csv", data), {}, &err));
        doc.waitForIndex();
        using V = std::vector<quint32>;
        QCOMPARE(filter(doc, {{-1, Op::Contains, "berlin"}}), (V{1, 3}));                // any column, case
        QCOMPARE(filter(doc, {{1, Op::Contains, QStringLiteral("müller")}}), (V{0}));      // non-ASCII
        QCOMPARE(filter(doc, {{2, Op::Equals, "berlin"}, {1, Op::StartsWith, "dana"}}), (V{3}));   // AND
        QCOMPARE(filter(doc, {{3, Op::Greater, "9"}}), (V{0, 1}));                         // numeric, empty ignored
        QCOMPARE(filter(doc, {{3, Op::LessOrEqual, "5"}}), (V{2, 4}));                     // 1.234 is 1.234
        QCOMPARE(filter(doc, {{3, Op::Equals, "5"}}), (V{2}));                             // 5 == 5.00
        QCOMPARE(filter(doc, {{4, Op::GreaterOrEqual, "2026-01-20"}}), (V{1, 3, 4}));       // ISO dates as text
        QCOMPARE(filter(doc, {{5, Op::IsNotEmpty, ""}}), (V{1, 3}));
        QCOMPARE(filter(doc, {{5, Op::IsEmpty, ""}}), (V{0, 2, 4}));
        QCOMPARE(filter(doc, {{-1, Op::NotContains, "berlin"}}), (V{0, 2, 4}));
        QCOMPARE(filter(doc, {{1, Op::Regex, "^[a-c].* (smith|dubois)$"}}), (V{1, 2}));
        QCOMPARE(filter(doc, {{1, Op::EndsWith, "WHITE"}}), (V{3}));
        QVERIFY(!validateConditions({{1, Op::Regex, "("}}).isEmpty());
        QVERIFY(!validateConditions({{1, Op::Greater, " "}}).isEmpty());
    }

    void sorting()
    {
        const QByteArray data = "name,amount\nb,10\na,\nC,2\nd,10\ne,abc\n";
        CsvDocument doc;
        QString err;
        QVERIFY(doc.open(write("sort.csv", data), {}, &err));
        doc.waitForIndex();
        std::atomic<qint64> done{0};
        std::atomic<bool> cancel{false};
        using V = std::vector<quint32>;
        SortResult r = sortRows(doc, allRows(doc), 1, true, done, cancel);
        QVERIFY(r.numeric == false);                   // "abc" -> 1 of 4 not numeric: sorted as text
        r = sortRows(doc, {0, 2, 3}, 1, true, done, cancel);
        QVERIFY(r.numeric);
        QCOMPARE(r.rows, (V{2, 0, 3}));                // 2, 10, 10 (stable: b before d)
        r = sortRows(doc, {0, 1, 2, 3}, 1, false, done, cancel);
        QCOMPARE(r.rows, (V{0, 3, 2, 1}));             // descending, empty last
        r = sortRows(doc, allRows(doc), 0, true, done, cancel);
        QCOMPARE(r.rows, (V{1, 0, 2, 3, 4}));          // a b C d e (case-insensitive)
    }

    void xlsxHelpers()
    {
        QCOMPARE(XlsxWriter::columnName(0), QStringLiteral("A"));
        QCOMPARE(XlsxWriter::columnName(25), QStringLiteral("Z"));
        QCOMPARE(XlsxWriter::columnName(26), QStringLiteral("AA"));
        QCOMPARE(XlsxWriter::columnName(701), QStringLiteral("ZZ"));
        QString v;
        QVERIFY(XlsxWriter::numberText("12.5", false, &v) && v == "12.5");
        QVERIFY(XlsxWriter::numberText("-3", false, &v));
        QVERIFY(XlsxWriter::numberText("12,5", true, &v) && v == "12.5");
        QVERIFY(!XlsxWriter::numberText("12,5", false, &v));
        QVERIFY(!XlsxWriter::numberText("00123", false, &v));          // IDs stay text
        QVERIFY(!XlsxWriter::numberText("2026-01-05", false, &v));
        QVERIFY(!XlsxWriter::numberText("1234567890123456", false, &v));   // > 15 digits
    }

    void exportCsvAndXlsx()
    {
        const QByteArray data = "id,name,amount\n1,\"Müller, Anna\",10.5\n2,Bob,3\n3,\"say \"\"hi\"\"\",7\n";
        CsvDocument doc;
        QString err;
        QVERIFY(doc.open(write("exp.csv", data), {}, &err));
        doc.waitForIndex();
        std::atomic<qint64> done{0};
        std::atomic<bool> cancel{false};

        ExportOptions o;
        o.format = ExportOptions::Csv;
        o.path = m_dir.filePath("out.csv");
        o.columns = {1, 2};
        o.csvDelimiter = ';';
        o.csvUtf8Bom = false;
        const std::vector<quint32> rows{2, 0};
        ExportResult r = exportRows(doc, &rows, o, done, cancel);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.rows, 2);
        QFile f(o.path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("name;amount\r\n\"say \"\"hi\"\"\";7\r\nMüller, Anna;10.5\r\n"));

        o.format = ExportOptions::Xlsx;
        o.path = m_dir.filePath("out.xlsx");
        o.columns = {0, 1, 2};
        r = exportRows(doc, nullptr, o, done, cancel);
        QVERIFY2(r.ok, qPrintable(r.error));
        QFile x(o.path);
        QVERIFY(x.open(QIODevice::ReadOnly));
        QCOMPARE(x.read(2), QByteArray("PK"));
        // Full check of the workbook with openpyxl: tests/check_xlsx.py (set LCV_KEEP_XLSX=path).
        if (qEnvironmentVariableIsSet("LCV_KEEP_XLSX")) {
            QFile::remove(qEnvironmentVariable("LCV_KEEP_XLSX"));
            QFile::copy(o.path, qEnvironmentVariable("LCV_KEEP_XLSX"));
        }

        // Cancelled export leaves no file.
        o.path = m_dir.filePath("cancelled.xlsx");
        cancel = true;
        std::vector<quint32> many(20000, 0);
        r = exportRows(doc, &many, o, done, cancel);
        QVERIFY(r.cancelled);
        QVERIFY(!QFile::exists(o.path));
    }

    void sampleGenerator()
    {
        const QString path = m_dir.filePath("sample.csv");
        std::atomic<qint64> done{0};
        std::atomic<bool> cancel{false};
        QString err;
        QVERIFY(generateSample(path, 20000, done, cancel, &err));
        CsvDocument doc;
        QVERIFY(doc.open(path, {}, &err));
        doc.waitForIndex();
        QCOMPARE(doc.rowCount(), 20000);                   // multi-line notes do not add rows
        QCOMPARE(doc.columnCount(), 16);
        QCOMPARE(doc.headers().first(), QStringLiteral("order_id"));
        QCOMPARE(doc.rowValues(19999).first(), QStringLiteral("ORD-00020000"));
        const auto multiline = filter(doc, {{15, Op::Contains, "Thank you"}});
        QVERIFY(!multiline.empty());
        QVERIFY(doc.rowValues(multiline.front()).at(15).contains('\n'));
        for (qint64 r = 0; r < doc.rowCount(); r += 997) QCOMPARE(doc.rowValues(r).size(), 16);
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
