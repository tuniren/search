#include "theme.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <Windows.h>

namespace searchapp
{

QIcon glyphIcon(Glyph glyph, const QColor& color)
{
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 24.0, size / 24.0);
        painter.setPen(QPen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        switch (glyph) {
        case Glyph::Search:
            painter.drawEllipse(QRectF(4, 4, 11, 11));
            painter.drawLine(QPointF(14, 14), QPointF(20, 20));
            break;
        case Glyph::Settings:
            painter.drawEllipse(QRectF(8, 8, 8, 8));
            for (int angle = 0; angle < 360; angle += 45) {
                painter.save();
                painter.translate(12, 12);
                painter.rotate(angle);
                painter.drawLine(QPointF(0, -7), QPointF(0, -9));
                painter.restore();
            }
            break;
        case Glyph::Application:
            for (int x : {4, 13})
                for (int y : {4, 13})
                    painter.drawRoundedRect(QRectF(x, y, 6, 6), 1.5, 1.5);
            break;
        }
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

bool isDarkTheme()
{
    return qApp->palette().color(QPalette::Window).lightness() < 128;
}

void applyTheme(QApplication& application, Theme theme)
{
    HIGHCONTRASTW highContrast{sizeof(HIGHCONTRASTW)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0);
    if (highContrast.dwFlags & HCF_HIGHCONTRASTON) {
        application.setStyle(QStyleFactory::create(QStringLiteral("windows")));
        application.setPalette(application.style()->standardPalette());
        application.setStyleSheet({});
        return;
    }
    bool dark = theme == Theme::Dark;
    if (theme == Theme::System) {
        QSettings registry(
            QStringLiteral(
                "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
            QSettings::NativeFormat);
        dark = registry.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
    }
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    const QString background = dark ? QStringLiteral("#181b22") : QStringLiteral("#f5f6f9");
    const QString surface = dark ? QStringLiteral("#222630") : QStringLiteral("#ffffff");
    const QString foreground = dark ? QStringLiteral("#edf0f7") : QStringLiteral("#222938");
    const QString secondary = dark ? QStringLiteral("#9aa5b8") : QStringLiteral("#737e91");
    const QString border = dark ? QStringLiteral("#343b49") : QStringLiteral("#e4e8ef");
    const QString hover = dark ? QStringLiteral("#2d3545") : QStringLiteral("#edf2ff");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(background));
    palette.setColor(QPalette::WindowText, QColor(foreground));
    palette.setColor(QPalette::Base, QColor(surface));
    palette.setColor(QPalette::AlternateBase, QColor(background));
    palette.setColor(QPalette::Text, QColor(foreground));
    palette.setColor(QPalette::Button, QColor(surface));
    palette.setColor(QPalette::ButtonText, QColor(foreground));
    palette.setColor(QPalette::Highlight, QColor("#4770eb"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor(secondary));
    palette.setColor(QPalette::ToolTipBase, QColor(surface));
    palette.setColor(QPalette::ToolTipText, QColor(foreground));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(secondary));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(secondary));
    application.setPalette(palette);
    application.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    application.setStyleSheet(QStringLiteral(R"(
        QWidget { color: %3; }
        QWidget#settingsRoot { background: %1; }
        QFrame#launcherSurface { background: %2; border: 1px solid %5; border-radius: 16px; }
        QFrame#separator { background: %5; border: none; max-height: 1px; }
        QLabel#muted { color: %4; }
        QLabel#sectionTitle { font-size: 21px; font-weight: 600; }
        QLabel#pageTitle { font-size: 23px; font-weight: 600; }
        QLabel#keyCap { color: %4; border: 1px solid %5; border-radius: 5px; padding: 3px 7px; font-size: 10px; }
        QLineEdit, QKeySequenceEdit, QComboBox, QListWidget, QPlainTextEdit {
            background: %2; border: 1px solid %5; border-radius: 7px; padding: 8px;
        }
        QLineEdit:focus, QKeySequenceEdit:focus, QComboBox:focus { border: 1px solid #4770eb; }
        QLineEdit#searchInput { border: none; background: transparent; font-size: 20px; padding: 8px 0; }
        QListView#results { border: none; background: transparent; outline: none; }
        QListWidget#navigation { border: none; background: transparent; padding: 0; outline: none; }
        QListWidget#navigation::item { padding: 12px 16px; border-radius: 7px; margin: 2px 0; }
        QListWidget#navigation::item:selected { background: %6; color: #4770eb; }
        QPushButton, QToolButton { background: %2; border: 1px solid %5; border-radius: 7px; padding: 7px 14px; }
        QPushButton:hover, QToolButton:hover { background: %6; }
        QPushButton#primary { background: #4770eb; color: white; border-color: #4770eb; }
        QPushButton#primary:hover { background: #365fd9; }
        QToolButton#settingsButton { border: none; background: transparent; padding: 6px; }
        QGroupBox { border: 1px solid %5; border-radius: 10px; margin-top: 14px; padding: 18px; }
        QGroupBox::title { subcontrol-origin: margin; left: 16px; padding: 0 5px; }
        QCheckBox { spacing: 10px; }
        QCheckBox::indicator { width: 17px; height: 17px; }
        QMenu { background: %2; border: 1px solid %5; padding: 5px; }
        QMenu::item { padding: 8px 24px; border-radius: 4px; }
        QMenu::item:selected { background: %6; }
        QScrollBar:vertical { width: 7px; background: transparent; }
        QScrollBar::handle:vertical { background: %5; border-radius: 3px; min-height: 24px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    )")
                                  .arg(background, surface, foreground, secondary, border, hover));
}

} // namespace searchapp
