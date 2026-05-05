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

bool StagePackage::saveToJson(const QString &path, QString *errorOut) const
{
    auto fail = [&](const QString &m) {
        if (errorOut) *errorOut = m;
        return false;
    };

    // Build the JSON document mirroring the loadFromJson() schema. We
    // emit only the fields with non-default values so the resulting
    // file stays small and human-readable. Round-trip with loadFromJson
    // is exact for the fields we serialise.
    auto editToObject = [](const StageEdit &e) -> QJsonObject {
        QJsonObject o;
        o.insert(QStringLiteral("map"), e.mapName);
        if (e.pctChange != 0.0)
            o.insert(QStringLiteral("pct"), e.pctChange);
        if (e.rowMin >= 0) o.insert(QStringLiteral("row_min"), e.rowMin);
        if (e.rowMax >= 0) o.insert(QStringLiteral("row_max"), e.rowMax);
        if (e.colMin >= 0) o.insert(QStringLiteral("col_min"), e.colMin);
        if (e.colMax >= 0) o.insert(QStringLiteral("col_max"), e.colMax);
        if (e.maxValue >= 0) o.insert(QStringLiteral("max_value"), e.maxValue);
        if (e.setValue >= 0) o.insert(QStringLiteral("set_value"), e.setValue);
        if (e.setToMapMax)   o.insert(QStringLiteral("set_to_map_max"), true);
        if (!e.comment.isEmpty()) o.insert(QStringLiteral("comment"), e.comment);
        return o;
    };

    QJsonObject root;
    root.insert(QStringLiteral("name"), name);
    if (!description.isEmpty())
        root.insert(QStringLiteral("description"), description);
    if (!schemas.isEmpty()) {
        QJsonArray sa;
        for (const QString &s : schemas) sa.append(s);
        root.insert(QStringLiteral("schemas"), sa);
    }

    QJsonArray editsArr;
    for (const StageEdit &e : edits) {
        if (e.mapName.isEmpty()) continue;  // skip incomplete entries
        editsArr.append(editToObject(e));
    }
    root.insert(QStringLiteral("edits"), editsArr);

    if (!options.isEmpty()) {
        QJsonArray optsArr;
        for (const StageOption &opt : options) {
            QJsonObject oo;
            oo.insert(QStringLiteral("id"), opt.id);
            if (!opt.label.isEmpty())       oo.insert(QStringLiteral("label"), opt.label);
            if (!opt.description.isEmpty()) oo.insert(QStringLiteral("description"), opt.description);
            oo.insert(QStringLiteral("default"), opt.defaultOn);
            QJsonArray oedits;
            for (const StageEdit &e : opt.edits)
                if (!e.mapName.isEmpty()) oedits.append(editToObject(e));
            oo.insert(QStringLiteral("edits"), oedits);
            optsArr.append(oo);
        }
        root.insert(QStringLiteral("options"), optsArr);
    }

    const QJsonDocument doc(root);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return fail(QStringLiteral("Cannot open for write: %1").arg(f.errorString()));
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
    if (f.write(bytes) != bytes.size())
        return fail(QStringLiteral("Short write"));
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
    // Sort order: performance stages (Stage 1, Stage 2, ...) first,
    // economy variants after, anything else alphabetically at the end.
    // Within each group, alphabetical. The intent is that the most
    // common picks (performance stages) appear at the top of the
    // picker so the user doesn't have to scroll past Economy entries
    // to reach them.
    auto sortKey = [](const QString &name) -> int {
        const QString lower = name.toLower();
        if (lower.startsWith(QStringLiteral("stage")))   return 0;
        if (lower.startsWith(QStringLiteral("economy"))) return 1;
        return 2;
    };
    std::sort(out.begin(), out.end(),
              [&sortKey](const QPair<QString,QString> &a,
                         const QPair<QString,QString> &b) {
                  const int ka = sortKey(a.first);
                  const int kb = sortKey(b.first);
                  if (ka != kb) return ka < kb;
                  return a.first < b.first;
              });
    return out;
}

} // namespace EcuParser
