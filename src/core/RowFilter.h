// Filtering: conditions on columns (AND), run in parallel on all CPU cores.
#pragma once

#include <QString>
#include <QStringList>

#include <atomic>
#include <vector>

namespace csv {

class CsvDocument;

enum class Op {
    Contains, NotContains, Equals, NotEquals, StartsWith, EndsWith,
    Greater, GreaterOrEqual, Less, LessOrEqual, IsEmpty, IsNotEmpty, Regex
};

struct Condition {
    int column = -1;              // -1 = any column
    Op op = Op::Contains;
    QString value;
};

// Names for the user interface, in the order of the enum.
QStringList operatorNames();
bool operatorNeedsValue(Op op);

// Checks conditions; returns an empty string if they are valid, otherwise a message.
QString validateConditions(const std::vector<Condition> &conditions);

// Returns the data rows (0-based) that match all conditions, in file order.
// `done` counts processed rows for a progress bar; set `cancel` to stop early.
std::vector<quint32> filterRows(const CsvDocument &doc, const std::vector<Condition> &conditions,
                                std::atomic<qint64> &done, const std::atomic<bool> &cancel);

}  // namespace csv
