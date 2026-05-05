#include "TuneLogDialog.h"
#include "../core/TuneLogger.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace EcuParser {

TuneLogDialog::TuneLogDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Tune log"));
    auto *vlay = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    const QStringList cols {
        QStringLiteral("When (UTC)"),
        QStringLiteral("Schema"),
        QStringLiteral("Bin"),
        QStringLiteral("Stage"),
        QStringLiteral("Cells"),
        QStringLiteral("Rating"),
        QStringLiteral("Notes"),
    };
    m_table->setColumnCount(cols.size());
    m_table->setHorizontalHeaderLabels(cols);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSortingEnabled(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    vlay->addWidget(m_table, 1);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *refreshBtn = btns->addButton(QStringLiteral("Refresh"),
                                       QDialogButtonBox::ActionRole);
    connect(btns, &QDialogButtonBox::accepted,  this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected,  this, &QDialog::reject);
    connect(refreshBtn, &QPushButton::clicked,  this, &TuneLogDialog::refresh);
    vlay->addWidget(btns);

    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &TuneLogDialog::onContextMenu);

    setMinimumSize(900, 480);
    refresh();
}

void TuneLogDialog::refresh()
{
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    const auto entries = TuneLogger::listAll(500);
    for (const auto &e : entries) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto setCell = [&](int col, const QString &txt,
                           Qt::Alignment a = Qt::AlignCenter) {
            auto *it = new QTableWidgetItem(txt);
            it->setTextAlignment(a);
            it->setData(Qt::UserRole, e.id);
            m_table->setItem(row, col, it);
        };
        setCell(0, e.appliedAt.toLocalTime().toString(Qt::ISODate),
                Qt::AlignLeft | Qt::AlignVCenter);
        setCell(1, e.driverSchema);
        setCell(2, QFileInfo(e.binPath).fileName(),
                Qt::AlignLeft | Qt::AlignVCenter);
        setCell(3, e.stageName, Qt::AlignLeft | Qt::AlignVCenter);
        setCell(4, QString::number(e.cellsChanged));
        setCell(5, e.rating > 0 ? QString::number(e.rating) : QStringLiteral("-"));
        setCell(6, e.notes, Qt::AlignLeft | Qt::AlignVCenter);
    }
    m_table->resizeColumnsToContents();
    m_table->setSortingEnabled(true);
    m_table->sortItems(0, Qt::DescendingOrder);
}

void TuneLogDialog::onContextMenu(const QPoint &pos)
{
    const QTableWidgetItem *it = m_table->itemAt(pos);
    if (!it) return;
    const int id = it->data(Qt::UserRole).toInt();
    if (id <= 0) return;
    const int row = it->row();

    QMenu menu(this);
    QAction *rateAct  = menu.addAction(QStringLiteral("Edit rating (1-5)..."));
    QAction *notesAct = menu.addAction(QStringLiteral("Edit notes..."));
    menu.addSeparator();
    QAction *delAct   = menu.addAction(QStringLiteral("Delete entry..."));
    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == rateAct) {
        bool ok = false;
        const int seed = m_table->item(row, 5)->text() == QStringLiteral("-")
            ? 3 : m_table->item(row, 5)->text().toInt();
        const int v = QInputDialog::getInt(
            this, QStringLiteral("Rating"),
            QStringLiteral("Rating (1-5, 0 to clear):"),
            seed, 0, 5, 1, &ok);
        if (!ok) return;
        TuneLogger::updateRatingAndNotes(id, v,
            m_table->item(row, 6) ? m_table->item(row, 6)->text() : QString());
        refresh();
    } else if (chosen == notesAct) {
        bool ok = false;
        const QString seed = m_table->item(row, 6)
            ? m_table->item(row, 6)->text() : QString();
        const QString v = QInputDialog::getMultiLineText(
            this, QStringLiteral("Notes"),
            QStringLiteral("Notes for this tune:"), seed, &ok);
        if (!ok) return;
        const int rating = m_table->item(row, 5)->text() == QStringLiteral("-")
            ? 0 : m_table->item(row, 5)->text().toInt();
        TuneLogger::updateRatingAndNotes(id, rating, v);
        refresh();
    } else if (chosen == delAct) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Delete entry"),
            QStringLiteral("Delete tune log entry #%1?").arg(id),
            QMessageBox::Yes | QMessageBox::No);
        if (answer == QMessageBox::Yes) {
            TuneLogger::removeEntry(id);
            refresh();
        }
    }
}

} // namespace EcuParser
