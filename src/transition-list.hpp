#pragma once

#include <QStyledItemDelegate>

#include "tm-style.hpp"

// Item roles and painting for the Transition Manager sidebar list.
namespace TransitionList {

enum Role {
	SourceRole = Qt::UserRole + 1, // quintptr obs_source_t*
	KindRole,                      // Kind
	ActiveRole,                    // bool
	DurationRole,                  // int ms, <= 0 = none
	TypeRole,                      // QString type id
};

enum Kind {
	Header = 0,  // "BUILT IN" / "CUSTOM" section label
	Placeholder, // muted hint row
	BuiltIn,     // unconfigurable (Cut, Fade): can't be removed or moved
	Custom,      // everything else
};

class Delegate : public QStyledItemDelegate {
	Q_OBJECT

public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void setColors(const TMStyle::Colors &colors) { colors_ = colors; }

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
	TMStyle::Colors colors_{};
};

} // namespace TransitionList
