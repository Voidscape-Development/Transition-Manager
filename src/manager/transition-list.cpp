#include "transition-list.hpp"

#include <QPainter>
#include <QPainterPath>

namespace TransitionList {

static constexpr int kRowHeight = 40;
static constexpr int kHeaderHeight = 30;

QSize Delegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	const int kind = index.data(KindRole).toInt();
	const int h = kind == Header ? kHeaderHeight : kind == Placeholder ? 34 : kRowHeight;
	return QSize(option.rect.width(), h + 2);
}

// Rounded "pill" with optional leading dot; returns its width.
static int drawPill(QPainter *p, const QRect &row, int right, const QString &text, const QColor &bg, const QColor &fg,
		    const QColor &dot, QFont font)
{
	font.setPointSizeF(font.pointSizeF() * 0.85);
	font.setBold(true);
	const QFontMetrics fm(font);
	const int dotSpace = dot.isValid() ? 11 : 0;
	const int w = fm.horizontalAdvance(text) + 14 + dotSpace;
	const int h = fm.height() + 4;
	const QRect r(right - w, row.center().y() - h / 2, w, h);

	QPainterPath path;
	path.addRoundedRect(r, h / 2.0, h / 2.0);
	p->fillPath(path, bg);
	if (dot.isValid()) {
		p->setBrush(dot);
		p->setPen(Qt::NoPen);
		p->drawEllipse(QPointF(r.left() + 10, r.center().y() + 0.5), 3, 3);
	}
	p->setFont(font);
	p->setPen(fg);
	p->drawText(r.adjusted(7 + dotSpace, 0, -7, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
	return w;
}

void Delegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	const TMStyle::Colors &c = colors_;
	const int kind = index.data(KindRole).toInt();
	const QString text = index.data(Qt::DisplayRole).toString();
	const QRect rect = option.rect.adjusted(8, 1, -8, -1);

	p->save();
	p->setRenderHint(QPainter::Antialiasing);

	if (kind == Header || kind == Placeholder) {
		QFont f = option.font;
		if (kind == Header) {
			f.setPointSizeF(f.pointSizeF() * 0.82);
			f.setBold(true);
			f.setLetterSpacing(QFont::PercentageSpacing, 106);
		} else {
			f.setItalic(true);
		}
		p->setFont(f);
		p->setPen(c.muted);
		p->drawText(rect.adjusted(8, kind == Header ? 6 : 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
		p->restore();
		return;
	}

	const bool selected = option.state & QStyle::State_Selected;
	const bool hovered = option.state & QStyle::State_MouseOver;

	QPainterPath bg;
	bg.addRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
	if (selected) {
		p->fillPath(bg, c.selection);
		p->setPen(QPen(c.selectionBorder, 1));
		p->drawPath(bg);
	} else if (hovered) {
		p->fillPath(bg, TMStyle::blend(c.sidebar, c.text, 0.06));
	}

	// Type icon
	const QColor iconColor = selected ? c.text : c.muted;
	const QIcon icon = TMStyle::typeIcon(index.data(TypeRole).toString().toUtf8().constData(), iconColor);
	icon.paint(p, QRect(rect.left() + 10, rect.center().y() - 8, 16, 16));

	// Badges, right to left
	int right = rect.right() - 10;
	if (kind == BuiltIn) {
		TMStyle::icon(QStringLiteral("lock"), c.muted, 13)
			.paint(p, QRect(right - 13, rect.center().y() - 6, 13, 13));
		right -= 13 + 8;
	}
	if (index.data(ActiveRole).toBool())
		right -= drawPill(p, rect, right, tr("Active"), c.activeBg, c.activeText, c.activeDot, option.font) + 6;
	const int ms = index.data(DurationRole).toInt();
	if (ms > 0) {
		const QColor chipBg = selected ? TMStyle::blend(c.selection, c.text, 0.12) : c.chipBg;
		right -= drawPill(p, rect, right, tr("%1 ms").arg(ms), chipBg, c.chipText, QColor(), option.font) + 6;
	}

	// Name
	QFont f = option.font;
	f.setBold(selected);
	p->setFont(f);
	p->setPen(c.text);
	const QRect textRect(rect.left() + 36, rect.top(), right - rect.left() - 36, rect.height());
	p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
		    QFontMetrics(f).elidedText(text, Qt::ElideRight, textRect.width()));

	p->restore();
}

} // namespace TransitionList
