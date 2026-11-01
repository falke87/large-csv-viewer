#include "XlsxWriter.h"

namespace {

const char *kContentTypes =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>
</Types>)";

const char *kRootRels =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>)";

const char *kWorkbookRels =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>)";

// Style 0: normal. Style 1: bold header on a light blue background.
const char *kStyles =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
<fonts count="2"><font><sz val="11"/><name val="Calibri"/><family val="2"/></font><font><b/><sz val="11"/><name val="Calibri"/><family val="2"/></font></fonts>
<fills count="3"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill><fill><patternFill patternType="solid"><fgColor rgb="FFDCE6F1"/><bgColor indexed="64"/></patternFill></fill></fills>
<borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders>
<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>
<cellXfs count="2"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/><xf numFmtId="0" fontId="1" fillId="2" borderId="0" xfId="0" applyFont="1" applyFill="1"/></cellXfs>
<cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>
</styleSheet>)";

void appendEscaped(QByteArray &out, const QString &text)
{
    const QByteArray utf8 = text.left(32767).toUtf8();   // Excel cell limit
    for (char c : utf8) {
        switch (c) {
        case '&': out.append("&amp;"); break;
        case '<': out.append("&lt;"); break;
        case '>': out.append("&gt;"); break;
        case '"': out.append("&quot;"); break;
        default:
            // XML 1.0 does not allow most control characters.
            if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r') break;
            out.append(c);
        }
    }
}

}  // namespace

XlsxWriter::XlsxWriter(QIODevice *device) : m_zip(device) {}

QString XlsxWriter::columnName(int index)
{
    QString name;
    for (int n = index + 1; n > 0; n = (n - 1) / 26) name.prepend(QChar('A' + (n - 1) % 26));
    return name;
}

bool XlsxWriter::numberText(const QString &text, bool commaDecimal, QString *excelValue)
{
    // Plain numbers only: optional minus, no leading zeros (keeps IDs like 00123 as text),
    // at most 15 significant digits (Excel precision).
    const QString t = text.trimmed();
    if (t.isEmpty() || t.size() > 20) return false;
    const QChar decimal = commaDecimal ? QLatin1Char(',') : QLatin1Char('.');
    int i = 0, digits = 0, intDigits = 0;
    bool seenDecimal = false;
    if (t[0] == QLatin1Char('-')) i = 1;
    if (i >= t.size()) return false;
    for (int k = i; k < t.size(); ++k) {
        const QChar c = t[k];
        if (c.isDigit() && c.unicode() < 128) {
            ++digits;
            if (!seenDecimal) ++intDigits;
        } else if (c == decimal && !seenDecimal && k > i && k + 1 < t.size()) {
            seenDecimal = true;
        } else {
            return false;
        }
    }
    if (digits == 0 || digits > 15) return false;
    if (intDigits > 1 && t[i] == QLatin1Char('0')) return false;
    if (excelValue) {
        *excelValue = t;
        if (commaDecimal) excelValue->replace(QLatin1Char(','), QLatin1Char('.'));
    }
    return true;
}

bool XlsxWriter::begin(const std::vector<double> &widths, bool headerRow)
{
    m_columns = int(widths.size());
    m_headerRow = headerRow;
    m_colNames.clear();
    for (int c = 0; c < m_columns; ++c) m_colNames.push_back(columnName(c).toLatin1());

    if (!m_zip.beginEntry("[Content_Types].xml") || !m_zip.write(QByteArray(kContentTypes))) return false;
    if (!m_zip.beginEntry("_rels/.rels") || !m_zip.write(QByteArray(kRootRels))) return false;
    if (!m_zip.beginEntry("xl/_rels/workbook.xml.rels") || !m_zip.write(QByteArray(kWorkbookRels))) return false;
    if (!m_zip.beginEntry("xl/styles.xml") || !m_zip.write(QByteArray(kStyles))) return false;
    if (!m_zip.beginEntry("xl/worksheets/sheet1.xml")) return false;

    m_buf = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)";
    if (headerRow)
        m_buf += R"(<sheetViews><sheetView workbookViewId="0"><pane ySplit="1" topLeftCell="A2" activePane="bottomLeft" state="frozen"/></sheetView></sheetViews>)";
    m_buf += "<cols>";
    for (int c = 0; c < m_columns; ++c)
        m_buf += QStringLiteral(R"(<col min="%1" max="%1" width="%2" customWidth="1"/>)")
                     .arg(c + 1).arg(widths[size_t(c)], 0, 'f', 1).toLatin1();
    m_buf += "</cols><sheetData>";
    m_row = 0;
    return true;
}

bool XlsxWriter::writeRow(const QStringList &cells, bool commaDecimal)
{
    if (m_row >= kMaxRows) return false;
    ++m_row;
    const QByteArray r = QByteArray::number(m_row);
    const bool header = m_headerRow && m_row == 1;
    m_buf += "<row r=\"" + r + "\">";
    QString value;
    const int n = std::min<int>(int(cells.size()), m_columns);
    for (int c = 0; c < n; ++c) {
        const QString &text = cells.at(c);
        if (text.isEmpty()) continue;
        const QByteArray ref = m_colNames[size_t(c)] + r;
        if (!header && numberText(text, commaDecimal, &value)) {
            m_buf += "<c r=\"" + ref + "\"><v>" + value.toLatin1() + "</v></c>";
        } else {
            m_buf += "<c r=\"" + ref + (header ? "\" s=\"1\"" : "\"") + " t=\"inlineStr\"><is><t xml:space=\"preserve\">";
            appendEscaped(m_buf, text);
            m_buf += "</t></is></c>";
        }
    }
    m_buf += "</row>";
    return flush(false);
}

bool XlsxWriter::flush(bool force)
{
    if (!force && m_buf.size() < 512 * 1024) return true;
    const bool ok = m_zip.write(m_buf);
    m_buf.clear();
    return ok;
}

bool XlsxWriter::finish()
{
    m_buf += "</sheetData>";
    const QByteArray range = "A1:" + columnName(std::max(0, m_columns - 1)).toLatin1() +
                             QByteArray::number(std::max<qint64>(1, m_row));
    if (m_headerRow && m_row > 1) m_buf += "<autoFilter ref=\"" + range + "\"/>";
    m_buf += "</worksheet>";
    if (!flush(true)) return false;

    QByteArray wb = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<bookViews><workbookView/></bookViews><sheets><sheet name="Export" sheetId="1" r:id="rId1"/></sheets>)";
    if (m_headerRow && m_row > 1) {
        QByteArray abs = "$A$1:$" + columnName(std::max(0, m_columns - 1)).toLatin1() + "$" +
                         QByteArray::number(m_row);
        wb += R"(<definedNames><definedName name="_xlnm._FilterDatabase" localSheetId="0" hidden="1">Export!)" +
              abs + "</definedName></definedNames>";
    }
    wb += "</workbook>";
    if (!m_zip.beginEntry("xl/workbook.xml") || !m_zip.write(wb)) return false;
    return m_zip.finish();
}
