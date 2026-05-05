#ifndef TUNELOGGER_H
#define TUNELOGGER_H

// Persistent log of every tune apply. SQLite-backed so it survives
// across launches without depending on QSettings (which is INI on
// Linux and registry on Windows - bad fit for tabular records).
//
// Schema:
//   tune_log(
//     id            INTEGER PRIMARY KEY AUTOINCREMENT,
//     applied_at    TEXT NOT NULL,         -- ISO-8601, UTC
//     driver_schema TEXT,                  -- e.g. "28F0_100"
//     bin_path      TEXT,                  -- absolute path of MOD bin
//     stage_name    TEXT NOT NULL,         -- "Stage 1", "My tune", etc.
//     cells_changed INTEGER,               -- result of applyStage()
//     rating        INTEGER,               -- 1..5, NULL = unrated
//     notes         TEXT                   -- freeform user notes
//   )
//
// The DB file lives at <data_dir>/tune_log.sqlite. We open it lazily
// on first write and keep the connection open for the app lifetime.

#include <QString>
#include <QDateTime>
#include <QList>

namespace EcuParser {

struct TuneLogEntry {
    int       id            = 0;
    QDateTime appliedAt;
    QString   driverSchema;
    QString   binPath;
    QString   stageName;
    int       cellsChanged  = 0;
    int       rating        = 0;     // 0 = unset
    QString   notes;
};

class TuneLogger
{
public:
    // Open (or create) the SQLite DB at the standard path. Returns
    // false if the file can't be opened. Subsequent calls are no-ops
    // if already open. Logs to qWarning on error.
    static bool ensureOpen();

    // Insert a new entry. Returns the rowid on success, -1 on failure.
    static int recordApply(const QString &driverSchema,
                           const QString &binPath,
                           const QString &stageName,
                           int cellsChanged);

    // Update rating + notes for an existing entry. Returns true on
    // success.
    static bool updateRatingAndNotes(int id, int rating, const QString &notes);

    // Delete one entry. Returns true on success.
    static bool removeEntry(int id);

    // Fetch all entries, newest first.
    static QList<TuneLogEntry> listAll(int limit = 200);

    // Close the connection. Called on shutdown to flush pending writes.
    static void close();
};

} // namespace EcuParser

#endif // TUNELOGGER_H
