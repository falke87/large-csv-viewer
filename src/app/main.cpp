// Large CSV Viewer – entry point.   DEMO PROJECT
#include "MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("LargeCsvViewer"));
    QApplication::setApplicationName(QStringLiteral("Large CSV Viewer"));
    QApplication::setApplicationVersion(QStringLiteral("1.0"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.png")));

    // Same look on every system (and readable in Windows dark mode).
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    p.setColor(QPalette::Window, QColor(246, 247, 249));
    p.setColor(QPalette::WindowText, QColor(30, 34, 40));
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, QColor(244, 247, 251));
    p.setColor(QPalette::Text, QColor(30, 34, 40));
    p.setColor(QPalette::Button, QColor(240, 242, 245));
    p.setColor(QPalette::ButtonText, QColor(30, 34, 40));
    p.setColor(QPalette::Highlight, QColor(31, 58, 95));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, QColor(255, 255, 225));
    p.setColor(QPalette::ToolTipText, Qt::black);
    p.setColor(QPalette::PlaceholderText, QColor(130, 138, 150));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(160, 165, 172));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(160, 165, 172));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(160, 165, 172));
    QApplication::setPalette(p);

    MainWindow w;
    w.show();
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) w.openFile(args.at(1));   // "Open with…" / drag onto the .exe
    return app.exec();
}
