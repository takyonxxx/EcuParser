#include "StagePackage.h"

#include "../gui/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace EcuParser {

bool StagePackage::loadFromJson(const QString &path,
                                StagePackage *out,
                                QString *errorOut)
{
    auto fail = [&](const QString &m) {
        if (errorOut) *errorOut = m;
        return false;
    };
    if (!out) return fail(QStringLiteral("internal: out == nullptr"));

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("Cannot open: %1").arg(f.errorString()));
    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (doc.isNull())
        return fail(QStringLiteral("JSON parse: %1").arg(pe.errorString()));
    if (!doc.isObject())
        return fail(QStringLiteral("JSON root is not an object"));

    const QJsonObject root = doc.object();
    out->name = root.value(QStringLiteral("name")).toString();
    out->description = root.value(QStringLiteral("description")).toString();

    const QJsonArray schemas = root.value(QStringLiteral("schemas")).toArray();
    out->schemas.clear();
    for (const QJsonValue &v : schemas)
        out->schemas.append(v.toString());

    out->edits.clear();
    const QJsonArray edits = root.value(QStringLiteral("edits")).toArray();
    for (const QJsonValue &ev : edits) {
        if (!ev.isObject()) continue;
        const QJsonObject e = ev.toObject();
        StageEdit edit;
        edit.mapName  = e.value(QStringLiteral("map")).toString();
        edit.pctChange = e.value(QStringLiteral("pct")).toDouble(0.0);
        edit.rowMin   = e.value(QStringLiteral("row_min")).toInt(-1);
        edit.rowMax   = e.value(QStringLiteral("row_max")).toInt(-1);
        edit.colMin   = e.value(QStringLiteral("col_min")).toInt(-1);
        edit.colMax   = e.value(QStringLiteral("col_max")).toInt(-1);
        edit.maxValue = e.value(QStringLiteral("max_value")).toInt(-1);
        edit.setValue = e.value(QStringLiteral("set_value")).toInt(-1);
        edit.setToMapMax = e.value(QStringLiteral("set_to_map_max")).toBool(false);
        edit.comment  = e.value(QStringLiteral("comment")).toString();
        if (edit.mapName.isEmpty())
            continue;
        out->edits.append(edit);
    }

    if (out->name.isEmpty())
        out->name = QFileInfo(path).baseName();
    return true;
}

QList<QPair<QString, QString>> StagePackage::listAvailable()
{
    QList<QPair<QString, QString>> out;
    const QString base = AppPaths::dataDir();
    if (base.isEmpty())
        return out;
    QDir d(base + QStringLiteral("/stages"));
    if (!d.exists())
        return out;
    const QStringList names = d.entryList(
        QStringList() << QStringLiteral("*.json"),
        QDir::Files | QDir::Readable, QDir::Name);
    for (const QString &n : names) {
        const QString full = d.absoluteFilePath(n);
        StagePackage tmp;
        if (StagePackage::loadFromJson(full, &tmp))
            out.append({tmp.name.isEmpty() ? n : tmp.name, full});
        else
            // Show the filename even if parse failed; the user gets a
            // clearer error later when they try to apply it.
            out.append({n, full});
    }
    std::sort(out.begin(), out.end(),
              [](const QPair<QString,QString> &a,
                 const QPair<QString,QString> &b) {
                  return a.first < b.first;
              });
    return out;
}

} // namespace EcuParser
