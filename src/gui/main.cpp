#include "MainWindow.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QFile>

namespace {

// Build a dark palette over Qt's Fusion style. We pick the Fusion style
// because it's the only built-in style that obeys QPalette uniformly across
// platforms - on Windows the default "windowsvista" style ignores most
// palette colours, on macOS the native style is similarly opaque.
QPalette makeDarkPalette()
{
    QPalette p;
    const QColor base       (28,  31,  36);
    const QColor baseAlt    (38,  42,  48);
    const QColor surface    (44,  48,  54);
    const QColor surfaceMid (54,  60,  68);
    const QColor border     (74,  80,  88);
    const QColor text       (225, 230, 235);
    const QColor textDim    (170, 175, 182);
    const QColor accent     (95,  155, 230);
    const QColor accentText (235, 240, 248);

    p.setColor(QPalette::Window,           surface);
    p.setColor(QPalette::WindowText,       text);
    p.setColor(QPalette::Base,             base);
    p.setColor(QPalette::AlternateBase,    baseAlt);
    p.setColor(QPalette::ToolTipBase,      surfaceMid);
    p.setColor(QPalette::ToolTipText,      text);
    p.setColor(QPalette::Text,             text);
    p.setColor(QPalette::Button,           surfaceMid);
    p.setColor(QPalette::ButtonText,       text);
    p.setColor(QPalette::BrightText,       QColor(255, 90, 90));
    p.setColor(QPalette::Highlight,        accent);
    p.setColor(QPalette::HighlightedText,  accentText);
    p.setColor(QPalette::Link,             accent);
    p.setColor(QPalette::PlaceholderText,  textDim);

    // Disabled variants for grayed-out controls.
    p.setColor(QPalette::Disabled, QPalette::Text,        textDim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,  textDim);
    p.setColor(QPalette::Disabled, QPalette::WindowText,  textDim);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, textDim);
    p.setColor(QPalette::Disabled, QPalette::Highlight,   border);

    Q_UNUSED(border);
    return p;
}

// A small stylesheet to polish the bits the palette can't quite reach
// (separator borders, toolbar height, summary banner, headers).
const char kDarkStylesheet[] = R"(
QToolBar {
    background: #2C3036;
    border: 0;
    padding: 4px 6px;
    spacing: 4px;
}
QToolBar QLabel { color: #B6BCC4; }
QPushButton {
    background: #3A4049;
    color: #E2E6EA;
    border: 1px solid #4A5058;
    border-radius: 3px;
    padding: 4px 10px;
}
QPushButton:hover  { background: #475060; }
QPushButton:pressed{ background: #2F3540; }
QPushButton:disabled {
    background: #2A2E33;
    color: #6A707A;
    border-color: #353A40;
}
QComboBox {
    background: #1F2329;
    color: #E2E6EA;
    border: 1px solid #4A5058;
    border-radius: 3px;
    padding: 3px 6px;
}
QComboBox QAbstractItemView {
    background: #2A2F36;
    selection-background-color: #5F9BE6;
    selection-color: #ffffff;
    border: 1px solid #4A5058;
}
QTableWidget QHeaderView::section {
    background: #E8ECF2;
    color: #1F3A8A;
    padding: 4px 6px;
    border: 0;
    border-right: 1px solid #C8CFD8;
    border-bottom: 1px solid #C8CFD8;
    font-weight: bold;
}
QTreeWidget QHeaderView::section {
    background: #3A4049;
    color: #C8CDD3;
    padding: 4px 6px;
    border: 0;
    border-bottom: 1px solid #2A2E33;
    font-weight: bold;
}
QTableWidget {
    background: #F5F6F8;
    alternate-background-color: #ECEEF2;
    gridline-color: #C8CFD8;
    color: #1A202C;
    selection-background-color: #B6CFF7;
    selection-color: #1A202C;
}
QTableCornerButton::section {
    background: #E8ECF2;
    border: 0;
    border-right: 1px solid #C8CFD8;
    border-bottom: 1px solid #C8CFD8;
}
QTreeWidget {
    background: #1F2329;
    color: #E2E6EA;
    border: 0;
    selection-background-color: #5F9BE6;
    selection-color: #ffffff;
}
QTreeWidget::item:hover { background: #2A2F36; }
QSplitter::handle { background: #2A2E33; }
QStatusBar { background: #2C3036; color: #B6BCC4; }
QStatusBar::item { border: 0; }
QTabWidget::pane { border: 1px solid #2A2E33; background: #1F2329; }
QTabBar::tab {
    background: #2C3036;
    color: #B6BCC4;
    padding: 6px 14px;
    border: 1px solid #2A2E33;
    border-bottom: 0;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
}
QTabBar::tab:selected {
    background: #1F2329;
    color: #E2E6EA;
}
QLabel#summaryLabel {
    background: #232830;
    color: #C8CDD3;
    border-bottom: 1px solid #2A2E33;
}
)";

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("EcuParser"));
    QApplication::setApplicationVersion(QStringLiteral("0.3"));
    QApplication::setOrganizationName(QStringLiteral("EcuParser"));

    // Force Fusion + dark palette so the look is consistent across
    // Windows / macOS / Linux.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(makeDarkPalette());
    qApp->setStyleSheet(QString::fromLatin1(kDarkStylesheet));

    Titanium::MainWindow w;
    w.show();
    return app.exec();
}
