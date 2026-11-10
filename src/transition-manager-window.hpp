#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include <obs.hpp>

#include "tm-style.hpp"

class QAbstractButton;
class QAction;
class QButtonGroup;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class MediaProbe;
class PropertiesPanel;
class TimelineSlider;
class TransitionPreview;
namespace TransitionList {
class Delegate;
}

// The Transition Manager window: a sidebar listing every transition (built
// in and custom, in OBS's order) and a detail area for the selected one with
// its header actions, live preview, duration and settings.
class TransitionManagerWindow : public QWidget {
	Q_OBJECT

public:
	explicit TransitionManagerWindow(QWidget *parent = nullptr);

	// Show, raise and focus the window, selecting `source` if given.
	void open(obs_source_t *source = nullptr);

protected:
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void changeEvent(QEvent *event) override;

private slots:
	void refreshList();
	void applyFilter();
	void onSelectionChanged();
	void onRowsMoved();
	void onCreate(const QString &typeId);
	void onDuplicate();
	void onRename();
	void onRemove();
	void onSetActive();

private:
	QWidget *buildSidebar();
	QWidget *buildDetail();
	QWidget *buildPreviewCard();
	QWidget *buildDurationCard();
	QWidget *buildSettingsCard();

	void applyStyle();
	obs_source_t *selected() const;
	void selectSource(obs_source_t *source);
	void populateNewMenu(QMenu *menu);
	void showContextMenu(const QPoint &pos);
	QMenu *buildActionsMenu(QWidget *parent);

	void updateHeader();
	void updateDurationCard();
	void updateEndpoints();
	void updateStingerInfo();

	// Preview playback
	int previewLengthMs() const;
	void startPlayback();
	void stopPlayback(bool resetPreview);
	void onPlaybackTick();
	void updateTimeLabel(double t);

	// Duration
	void onDurationModeChanged(bool custom);
	void onCustomDurationChanged(int ms);

	TMStyle::Colors colors_{};
	bool applyingStyle_ = false;

	// Sidebar
	QLabel *countLabel_ = nullptr;
	QPushButton *newBtn_ = nullptr;
	QLineEdit *filter_ = nullptr;
	QAction *searchIcon_ = nullptr;
	QListWidget *list_ = nullptr;
	TransitionList::Delegate *delegate_ = nullptr;
	QLabel *sidebarNote_ = nullptr;

	// Detail
	QStackedWidget *detailStack_ = nullptr;
	QLabel *nameLabel_ = nullptr;
	QPushButton *renameBtn_ = nullptr;
	QLabel *subtitle_ = nullptr;
	QPushButton *duplicateBtn_ = nullptr;
	QPushButton *activateBtn_ = nullptr;
	QLabel *activePill_ = nullptr;
	QPushButton *moreBtn_ = nullptr;

	TransitionPreview *preview_ = nullptr;
	QLabel *endpoints_ = nullptr;
	QAbstractButton *playBtn_ = nullptr;
	TimelineSlider *scrub_ = nullptr;
	QLabel *timeLabel_ = nullptr;
	QLabel *cutLabel_ = nullptr;
	QLabel *totalLabel_ = nullptr;
	QPushButton *loopBtn_ = nullptr;
	QPushButton *colorsBtn_ = nullptr;
	QPushButton *scenesBtn_ = nullptr;

	QWidget *durationAdjustable_ = nullptr;
	QWidget *durationFixed_ = nullptr;
	QPushButton *obsDefaultBtn_ = nullptr;
	QPushButton *customBtn_ = nullptr;
	QSlider *durationSlider_ = nullptr;
	QSpinBox *durationSpin_ = nullptr;
	QWidget *customRow_ = nullptr;
	QLabel *durationHint_ = nullptr;
	QLabel *fixedText_ = nullptr;
	QLabel *fixedIcon_ = nullptr;
	void showFixedDuration(bool fixed);

	QPushButton *resetBtn_ = nullptr;
	PropertiesPanel *props_ = nullptr;

	// State
	obs_source_t *shown_ = nullptr; // compared only; props_ holds the ref
	QString shownName_;
	bool refreshing_ = false;
	bool reordering_ = false;

	QTimer ticker_;
	QElapsedTimer elapsed_;
	bool playing_ = false;

	MediaProbe *probe_ = nullptr;
	QString stingerPath_;
	qint64 stingerTotalMs_ = -1; // including Stinger's 250 ms tail
	qint64 stingerFrames_ = 0;
	double stingerCut_ = -1.0; // fraction of the length
	QString stingerCutText_;
};
