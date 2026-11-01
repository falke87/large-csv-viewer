// Creates a large, realistic-looking but fully fictional orders CSV for testing.
// Same row count -> same file (deterministic random numbers).
#pragma once

#include <QString>
#include <atomic>

namespace csv {

// Writes `rows` data rows (plus header) to `path`. `done` counts written rows.
bool generateSample(const QString &path, qint64 rows, std::atomic<qint64> &done,
                    const std::atomic<bool> &cancel, QString *error);

}  // namespace csv
