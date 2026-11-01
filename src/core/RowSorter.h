// Sorting rows by one column, using all CPU cores. Numbers sort as numbers, text as text.
#pragma once

#include <atomic>
#include <vector>
#include <QtGlobal>

namespace csv {

class CsvDocument;

struct SortResult {
    std::vector<quint32> rows;
    bool numeric = false;          // the column was sorted as numbers
};

// Sorts `rows` (data row numbers) by `column`. Empty values always go to the end.
// Equal values keep their original order. `done` goes from 0 to rows.size().
SortResult sortRows(const CsvDocument &doc, std::vector<quint32> rows, int column, bool ascending,
                    std::atomic<qint64> &done, const std::atomic<bool> &cancel);

}  // namespace csv
