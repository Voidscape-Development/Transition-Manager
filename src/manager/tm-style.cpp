#include "tm-style.hpp"

#include <QHash>
#include <QPixmap>
#include <cstring>

namespace TMStyle {

QColor blend(const QColor &a, const QColor &b, double t)
{
	return QColor::fromRgbF((float)(a.redF() * (1 - t) + b.redF() * t),
				(float)(a.greenF() * (1 - t) + b.greenF() * t),
				(float)(a.blueF() * (1 - t) + b.blueF() * t));
}

Colors colors(const QPalette &palette)
{
	Colors c;
	const QColor window = palette.color(QPalette::Window);
	c.dark = window.lightness() < 128;
	c.text = palette.color(QPalette::WindowText);
	c.muted = blend(c.text, window, 0.35);
	c.accent = palette.color(QPalette::Highlight);
	c.accentText = palette.color(QPalette::HighlightedText);
	c.link = c.dark ? blend(c.accent, QColor(Qt::white), 0.45) : c.accent.darker(115);

	c.sidebar = c.dark ? window.lighter(112) : window.darker(103);
	c.card = c.dark ? window.lighter(120) : palette.color(QPalette::Base);
	c.border = blend(window, c.text, 0.16);
	c.input = c.dark ? window.darker(115) : palette.color(QPalette::Base);
	c.segmentOn = blend(c.card, c.accent, 0.45);
	c.selection = blend(c.sidebar, c.accent, 0.28);
	c.selectionBorder = blend(c.sidebar, c.accent, 0.6);
	c.chipBg = blend(c.sidebar, c.text, 0.12);
	c.chipText = blend(c.text, c.sidebar, 0.15);

	// Status green, readable on both themes.
	c.activeDot = c.dark ? QColor(0x3f, 0xcf, 0x86) : QColor(0x1f, 0x8a, 0x5b);
	c.activeBg = blend(c.sidebar, c.activeDot, c.dark ? 0.22 : 0.16);
	c.activeText = c.dark ? QColor(0x8b, 0xe3, 0xb2) : QColor(0x16, 0x6b, 0x46);
	c.marker = c.dark ? QColor(0xf0, 0xb4, 0x3c) : QColor(0xb0, 0x70, 0x00);
	return c;
}

// ---- icons -------------------------------------------------------------------

static const char *iconBody(const QString &name)
{
	static const QHash<QString, const char *> bodies = {
		{"plus", "<path d='M12 5v14M5 12h14'/>"},
		{"search", "<circle cx='11' cy='11' r='7'/><path d='M20 20l-3.5-3.5'/>"},
		{"pencil", "<path d='M4 20h4L19 9l-4-4L4 16z'/>"},
		{"dots", "<circle cx='12' cy='5' r='1.3' fill='C'/><circle cx='12' cy='12' r='1.3' fill='C'/>"
			 "<circle cx='12' cy='19' r='1.3' fill='C'/>"},
		{"play", "<path d='M7 4l13 8-13 8z' fill='C'/>"},
		{"stop", "<rect x='6' y='6' width='12' height='12' rx='1.5' fill='C'/>"},
		{"loop", "<path d='M17 2l3 3-3 3'/><path d='M4 11V9a4 4 0 0 1 4-4h12'/><path d='M7 22l-3-3 3-3'/>"
			 "<path d='M20 13v2a4 4 0 0 1-4 4H4'/>"},
		{"lock", "<rect x='5' y='11' width='14' height='10' rx='2'/><path d='M8 11V7a4 4 0 0 1 8 0v4'/>"},
		{"clock", "<circle cx='12' cy='12' r='9'/><path d='M12 7v5l3 2'/>"},
		{"duplicate", "<rect x='8' y='8' width='12' height='12' rx='2'/>"
			      "<path d='M16 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h2'/>"},
		{"trash", "<path d='M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13'/>"},
		{"info", "<circle cx='12' cy='12' r='9'/><path d='M12 11v6M12 7.5v.5'/>"},
		// transition types
		{"t-cut", "<circle cx='6' cy='6' r='3'/><circle cx='6' cy='18' r='3'/>"
			  "<path d='M20 4L8.1 15.9M14.5 14.5L20 20M8.1 8.1L12 12'/>"},
		{"t-fade", "<circle cx='12' cy='12' r='8'/><path d='M12 4a8 8 0 0 1 0 16z' fill='C'/>"},
		{"t-swipe", "<path d='M4 12h14M13 6l6 6-6 6'/>"},
		{"t-slide", "<path d='M12 19V5M6 11l6-6 6 6'/>"},
		{"t-luma", "<circle cx='12' cy='12' r='8'/><path d='M12 8v4l3 2'/>"},
		{"t-stinger", "<rect x='3' y='5' width='18' height='14' rx='2'/><path d='M7 5v14M17 5v14'/>"},
		{"t-color",
		 "<rect x='4' y='4' width='16' height='16' rx='3'/><path d='M4 14h16' stroke-dasharray='2 2'/>"},
		{"t-generic",
		 "<rect x='3' y='6' width='11' height='12' rx='2'/><rect x='10' y='6' width='11' height='12' "
		 "rx='2'/>"},
	};
	return bodies.value(name, bodies.value("t-generic"));
}

QIcon icon(const QString &name, const QColor &color, int size)
{
	const QString hex = color.name(QColor::HexRgb);
	QString body = QString::fromLatin1(iconBody(name));
	body.replace(QStringLiteral("fill='C'"), QStringLiteral("fill='%1' stroke='none'").arg(hex));

	// Rendered at 2x for high-DPI screens. Uses Qt's SVG image plugin, which
	// OBS ships for its own icons.
	const int px = size * 2;
	const QString svg = QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='%1' height='%1' "
					   "viewBox='0 0 24 24' fill='none' stroke='%2' stroke-width='2' "
					   "stroke-linecap='round' stroke-linejoin='round'>%3</svg>")
				    .arg(px)
				    .arg(hex, body);
	QPixmap pixmap;
	if (!pixmap.loadFromData(svg.toUtf8(), "SVG"))
		return QIcon();
	pixmap.setDevicePixelRatio(2.0);
	return QIcon(pixmap);
}

QIcon typeIcon(const char *typeId, const QColor &color, int size)
{
	static const struct {
		const char *id;
		const char *icon;
	} map[] = {
		{"cut_transition", "t-cut"},
		{"fade_transition", "t-fade"},
		{"swipe_transition", "t-swipe"},
		{"slide_transition", "t-slide"},
		{"wipe_transition", "t-luma"},
		{"obs_stinger_transition", "t-stinger"},
		{"fade_to_color_transition", "t-color"},
	};
	for (const auto &m : map) {
		if (typeId && strcmp(typeId, m.id) == 0)
			return icon(QString::fromLatin1(m.icon), color, size);
	}
	return icon(QStringLiteral("t-generic"), color, size);
}

QString typeDescription(const char *typeId)
{
	if (!typeId)
		return QString();
	if (strcmp(typeId, "fade_to_color_transition") == 0)
		return QStringLiteral("Fade out to a solid color, then into the new scene");
	if (strcmp(typeId, "wipe_transition") == 0)
		return QStringLiteral("Wipe using an image as the mask");
	if (strcmp(typeId, "slide_transition") == 0)
		return QStringLiteral("Slide the new scene in, pushing the old one out");
	if (strcmp(typeId, "obs_stinger_transition") == 0)
		return QStringLiteral("Play a video over the switch");
	if (strcmp(typeId, "swipe_transition") == 0)
		return QStringLiteral("Swipe the new scene over the old one");
	return QString();
}

// ---- style sheet -------------------------------------------------------------

QString styleSheet(const Colors &c)
{
	auto rgb = [](const QColor &col) {
		return col.name(QColor::HexRgb);
	};

	QString css = QStringLiteral(R"(
QWidget#tmSidebar { background: %SIDEBAR%; }
QScrollArea#tmSettings { background: transparent; border: none; }
QScrollArea#tmSettings > QWidget > QWidget { background: transparent; }
QWidget#tmSidebar QListWidget { background: transparent; border: none; outline: none; }
QFrame#tmDivider { background: %BORDER%; border: none; }
QFrame#tmCard { background: %CARD%; border: 1px solid %BORDER%; border-radius: 8px; }
QFrame#tmHeader { border: none; border-bottom: 1px solid %BORDER%; }
QLabel[tmRole="muted"] { color: %MUTED%; }
QLabel[tmRole="title"] { font-size: 18px; font-weight: 700; }
QLabel[tmRole="sidebarTitle"] { font-size: 15px; font-weight: 700; }
QLabel[tmRole="cardTitle"] { font-weight: 700; }
QLabel[tmRole="marker"] { color: %MARKER%; }
QLabel[tmRole="activePill"] { background: %ACTIVEBG%; color: %ACTIVETEXT%; border-radius: 5px;
	padding: 6px 12px; font-weight: 600; }
QLineEdit#tmSearch { background: %INPUT%; border: 1px solid %BORDER%; border-radius: 6px; padding: 6px 8px; }
QPushButton[tmRole="primary"] { background: %ACCENT%; color: %ACCENTTEXT%; border: none; border-radius: 5px;
	padding: 7px 14px; font-weight: 600; }
QPushButton[tmRole="primary"]:hover { background: %ACCENTHOVER%; }
QPushButton[tmRole="primary"]:disabled { background: %BORDER%; color: %MUTED%; }
QPushButton[tmRole="secondary"] { background: %CARD%; border: 1px solid %BORDER%; border-radius: 5px;
	padding: 7px 14px; font-weight: 600; }
QPushButton[tmRole="secondary"]:hover { border-color: %MUTED%; }
QPushButton[tmRole="icon"] { background: transparent; border: none; border-radius: 5px; padding: 6px; }
QPushButton[tmRole="icon"]:hover { background: %CHIPBG%; }
QPushButton[tmRole="iconBox"] { background: %CARD%; border: 1px solid %BORDER%; border-radius: 5px; padding: 8px; }
QPushButton[tmRole="iconBox"]:checked { background: %SEGON%; border-color: %SELBORDER%; }
QPushButton[tmRole="link"] { background: transparent; border: none; color: %LINK%; font-weight: 600;
	padding: 4px 6px; }
QPushButton[tmRole="link"]:hover { text-decoration: underline; }
QFrame[tmRole="segGroup"] { background: %INPUT%; border: 1px solid %BORDER%; border-radius: 6px; }
QPushButton[tmRole="seg"] { background: transparent; border: none; border-radius: 4px; padding: 5px 12px;
	color: %MUTED%; font-weight: 600; }
QPushButton[tmRole="seg"]:checked { background: %SEGON%; color: %TEXT%; }
QToolButton[tmRole="thumb"] { background: transparent; border: 1px solid %BORDER%; border-radius: 4px; padding: 1px; }
QToolButton[tmRole="thumb"]:checked { border: 2px solid %ACCENT%; padding: 0px; }
QToolButton[tmRole="thumb"]:hover { border-color: %MUTED%; }
)");

	const QColor accentHover = c.dark ? c.accent.lighter(115) : c.accent.darker(110);
	const std::pair<const char *, QColor> vars[] = {
		{"%SIDEBAR%", c.sidebar},
		{"%BORDER%", c.border},
		{"%CARD%", c.card},
		{"%MUTED%", c.muted},
		{"%MARKER%", c.marker},
		{"%ACTIVEBG%", c.activeBg},
		{"%ACTIVETEXT%", c.activeText},
		{"%INPUT%", c.input},
		{"%ACCENT%", c.accent},
		{"%ACCENTTEXT%", c.accentText},
		{"%ACCENTHOVER%", accentHover},
		{"%CHIPBG%", c.chipBg},
		{"%SEGON%", c.segmentOn},
		{"%SELBORDER%", c.selectionBorder},
		{"%LINK%", c.link},
		{"%TEXT%", c.text},
	};
	for (const auto &v : vars)
		css.replace(QLatin1String(v.first), rgb(v.second));
	return css;
}

} // namespace TMStyle
