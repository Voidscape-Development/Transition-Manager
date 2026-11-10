#pragma once

#include <QColor>
#include <QPainter>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>

// Horizontal scrub slider that can draw a marker at a fraction of its range,
// used to show where a Stinger cuts to the new scene.
class TimelineSlider : public QSlider {
public:
	explicit TimelineSlider(QWidget *parent = nullptr) : QSlider(Qt::Horizontal, parent) {}

	// fraction in [0, 1]; negative hides the marker.
	void setMarker(double fraction, const QColor &color)
	{
		marker_ = fraction;
		markerColor_ = color;
		update();
	}
	double marker() const { return marker_; }

protected:
	void paintEvent(QPaintEvent *event) override
	{
		QSlider::paintEvent(event);
		if (marker_ < 0.0)
			return;

		QStyleOptionSlider opt;
		initStyleOption(&opt);
		const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
		const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

		const int span = groove.width() - handle.width();
		const int x = groove.x() + handle.width() / 2 + (int)(marker_ * span);
		const int cy = groove.center().y();

		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(markerColor_, 2));
		p.drawLine(x, cy - 9, x, cy + 9);
	}

private:
	double marker_ = -1.0;
	QColor markerColor_;
};
