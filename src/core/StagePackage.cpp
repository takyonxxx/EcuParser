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

    // Helper: parse a single edit object into a StageEdit. Used both
    // for the top-level edits[] array and for option-scoped edits[].
    auto parseEdit = [](const QJsonObject &e, StageEdit *editOut) -> bool {
        editOut->mapName     = e.value(QStringLiteral("map")).toString();
        editOut->pctChange   = e.value(QStringLiteral("pct")).toDouble(0.0);
        editOut->rowMin      = e.value(QStringLiteral("row_min")).toInt(-1);
        editOut->rowMax      = e.value(QStringLiteral("row_max")).toInt(-1);
        editOut->colMin      = e.value(QStringLiteral("col_min")).toInt(-1);
        editOut->colMax      = e.value(QStringLiteral("col_max")).toInt(-1);
        editOut->maxValue    = e.value(QStringLiteral("max_value")).toInt(-1);
        editOut->setValue    = e.value(QStringLiteral("set_value")).toInt(-1);
        editOut->setToMapMax = e.value(QStringLiteral("set_to_map_max")).toBool(false);
        editOut->comment     = e.value(QStringLiteral("comment")).toString();
        return !editOut->mapName.isEmpty();
    };

    out->edits.clear();
    const QJsonArray edits = root.value(QStringLiteral("edits")).toArray();
    for (const QJsonValue &ev : edits) {
        if (!ev.isObject()) continue;
        StageEdit edit;
        if (parseEdit(ev.toObject(), &edit))
            out->edits.append(edit);
    }

    out->options.clear();
    const QJsonArray opts = root.value(QStringLiteral("options")).toArray();
    for (const QJsonValue &ov : opts) {
        if (!ov.isObject()) continue;
        const QJsonObject o = ov.toObject();
        StageOption opt;
        opt.id          = o.value(QStringLiteral("id")).toString();
        opt.label       = o.value(QStringLiteral("label")).toString();
        opt.description = o.value(QStringLiteral("description")).toString();
        opt.defaultOn   = o.value(QStringLiteral("default")).toBool(true);
        const QJsonArray oedits = o.value(QStringLiteral("edits")).toArray();
        for (const QJsonValue &oev : oedits) {
            if (!oev.isObject()) continue;
            StageEdit edit;
            if (parseEdit(oev.toObject(), &edit))
                opt.edits.append(edit);
        }
        if (opt.id.isEmpty())
            opt.id = opt.label; // fall back to label as id
        if (!opt.id.isEmpty())
            out->options.append(opt);
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
