#include "TuneLogger.h"

#include "../gui/AppPaths.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace EcuParser {

namespace {

const char *kConnName = "EcuParserTuneLog";

bool g_opened = false;

QString dbPath()
{
    QString base = AppPaths::dataDir();
    if (base.isEmpty()) {
        // Fallback to current working dir if data/ isn't located -
        // the app still works, just stores the log next to the binary.
        base = QStringLiteral(".");
    }
    return QDir(base).absoluteFilePath(QStringLiteral("tune_log.sqlite"));
}

bool exec(QSqlQuery &q, const QString &sql)
{
    if (!q.exec(sql)) {
        qWarning("TuneLogger: SQL exec failed: %s | %s",
                 qPrintable(sql), qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

} // namespace

bool TuneLogger::ensureOpen()
{
    if (g_opened) return true;

    QSqlDatabase db = QSqlDatabase::contains(QString::fromLatin1(kConnName))
        ? QSqlDatabase::database(QString::fromLatin1(kConnName))
        : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                    QString::fromLatin1(kConnName));
    db.setDatabaseName(dbPath());
    if (!db.open()) {
        qWarning("TuneLogger: cannot open %s: %s",
                 qPrintable(dbPath()),
                 qPrintable(db.lastError().text()));
        return false;
    }

    QSqlQuery q(db);
    // CREATE TABLE IF NOT EXISTS - idempotent first-run schema setup.
    // Using TEXT for applied_at so the file remains portable (no SQLite
    // datetime serialisation quirks across versions).
    if (!exec(q, QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS tune_log (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            applied_at    TEXT    NOT NULL,
            driver_schema TEXT,
            bin_path      TEXT,
            stage_name    TEXT    NOT NULL,
            cells_changed INTEGER,
            rating        INTEGER,
            notes         TEXT
        )
    )"))) {
        return false;
    }
    if (!exec(q, QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_tune_log_applied_at "
        "ON tune_log(applied_at DESC)"))) {
        return false;
    }
    g_opened = true;
    return true;
}

int TuneLogger::recordApply(const QString &driverSchema,
                            const QString &binPath,
                            const QString &stageName,
                            int cellsChanged)
{
    if (!ensureOpen()) return -1;
    QSqlDatabase db = QSqlDatabase::database(QString::fromLatin1(kConnName));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO tune_log "
        "(applied_at, driver_schema, bin_path, stage_name, cells_changed) "
        "VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.addBindValue(driverSchema);
    q.addBindValue(binPath);
    q.addBindValue(stageName);
    q.addBindValue(cellsChanged);
    if (!q.exec()) {
        qWarning("TuneLogger: insert failed: %s",
                 qPrintable(q.lastError().text()));
        return -1;
    }
    return q.lastInsertId().toInt();
}

bool TuneLogger::updateRatingAndNotes(int id, int rating, const QString &notes)
{
    if (!ensureOpen()) return false;
    QSqlDatabase db = QSqlDatabase::database(QString::fromLatin1(kConnName));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE tune_log SET rating = ?, notes = ? WHERE id = ?"));
    // rating == 0 -> NULL so "unrated" is distinguishable from "1 star"
    if (rating > 0) q.addBindValue(rating);
    else            q.addBindValue(QVariant(QMetaType(QMetaType::Int)));
    q.addBindValue(notes);
    q.addBindValue(id);
    if (!q.exec()) {
        qWarning("TuneLogger: update failed: %s",
                 qPrintable(q.lastError().text()));
        return false;
    }
    return q.numRowsAffected() > 0;
}

bool TuneLogger::removeEntry(int id)
{
    if (!ensureOpen()) return false;
    QSqlDatabase db = QSqlDatabase::database(QString::fromLatin1(kConnName));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM tune_log WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        qWarning("TuneLogger: delete failed: %s",
                 qPrintable(q.lastError().text()));
        return false;
    }
    return q.numRowsAffected() > 0;
}

QList<TuneLogEntry> TuneLogger::listAll(int limit)
{
    QList<TuneLogEntry> out;
    if (!ensureOpen()) return out;
    QSqlDatabase db = QSqlDatabase::database(QString::fromLatin1(kConnName));
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, applied_at, driver_schema, bin_path, stage_name, "
        "       cells_changed, rating, notes "
        "FROM tune_log ORDER BY applied_at DESC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec()) {
        qWarning("TuneLogger: list failed: %s",
                 qPrintable(q.lastError().text()));
        return out;
    }
    while (q.next()) {
        TuneLogEntry e;
        e.id            = q.value(0).toInt();
        e.appliedAt     = QDateTime::fromString(q.value(1).toString(), Qt::ISODate);
        e.appliedAt.setTimeSpec(Qt::UTC);
        e.driverSchema  = q.value(2).toString();
        e.binPath       = q.value(3).toString();
        e.stageName     = q.value(4).toString();
        e.cellsChanged  = q.value(5).toInt();
        e.rating        = q.value(6).isNull() ? 0 : q.value(6).toInt();
        e.notes         = q.value(7).toString();
        out.append(e);
    }
    return out;
}

void TuneLogger::close()
{
    if (!g_opened) return;
    {
        QSqlDatabase db = QSqlDatabase::database(QString::fromLatin1(kConnName));
        if (db.isOpen()) db.close();
    }
    QSqlDatabase::removeDatabase(QString::fromLatin1(kConnName));
    g_opened = false;
}

} // namespace EcuParser
