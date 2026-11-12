#pragma once

#include "storage/settings.h"

#include <QIcon>

class QApplication;

namespace searchapp
{

enum class Glyph { Search, Settings, Application };
QIcon glyphIcon(Glyph glyph, const QColor& color = QColor(234, 116, 28));
void applyTheme(QApplication& application, Theme theme);
bool isDarkTheme();

} // namespace searchapp
