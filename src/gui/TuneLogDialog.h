#ifndef TUNELOGDIALOG_H
#define TUNELOGDIALOG_H

#include <QDialog>

class QTableWidget;

namespace EcuParser {

// Read-only viewer over the SQLite tune_log table, plus inline rating
// and notes editing. Layout:
//
//   | when | schema | bin | stage | cells | rating | notes... |
//
// Right-click row -> Edit rating / Edit notes / Delete.
class TuneLogDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TuneLogDialog(QWidget *parent = nullptr);

private slots:
    void refresh();
    void onContextMenu(const QPoint &pos);

private:
    QTableWidget *m_table = nullptr;
};

} // namespace EcuParser

#endif // TUNELOGDIALOG_H
