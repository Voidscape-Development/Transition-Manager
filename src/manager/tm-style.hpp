#pragma once

#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QString>

// Colors, icons and the window style sheet for the Transition Manager
// window. Everything is derived from the current OBS theme's palette, so
// the window follows dark and light themes alike.
namespace TMStyle {

struct Colors {
	bool dark;
	QColor text, muted, accent, accentText, link;
	QColor sidebar, card, border, input, segmentOn, selection, selectionBorder;
	QColor chipBg, chipText, activeBg, activeText, activeDot, marker;
};

Colors colors(const QPalette &palette);
QColor blend(const QColor &a, const QColor &b, double t); // t = weight of b

// Stroke icons by name: plus, search, pencil, dots, play, stop, loop, lock,
// clock, duplicate, trash, info.
QIcon icon(const QString &name, const QColor &color, int size = 16);

// Icon for a transition type id (falls back to a generic glyph).
QIcon typeIcon(const char *typeId, const QColor &color, int size = 16);

// Short description for known transition types, empty for others.
QString typeDescription(const char *typeId);

QString styleSheet(const Colors &c);

} // namespace TMStyle
