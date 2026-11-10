#pragma once

#include <QWidget>
#include <obs.hpp>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPushButton;
class QSlider;
class QSpinBox;
class QToolButton;
class TransitionPreview;
class PropertiesPanel;

// The full Transition Manager window: live preview + controls on top,
// searchable/reorderable transition list in the middle, generated settings
// panel at the bottom. Opened from the Transitions dock or the Tools menu.
class TransitionManagerWindow : public QWidget {
	Q_OBJECT

public:
	explicit TransitionManagerWindow(QWidget *parent = nullptr);

	// Show, raise and focus the window, selecting `source` if given.
	void open(obs_source_t *source = nullptr);

protected:
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;

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
	void onOwnDurationToggled(bool on);
	void onDurationChanged(int ms);
	void onGlobalDurationChanged();

private:
	obs_source_t *selected() const;
	void selectSource(obs_source_t *source);
	void populateNewMenu(QMenu *menu);
	void updateButtons();
	void updateDurationControls();
	void showContextMenu(const QPoint &pos);

	TransitionPreview *preview_ = nullptr;
	QComboBox *previewMode_ = nullptr;
	QPushButton *playBtn_ = nullptr;
	QSlider *scrub_ = nullptr;
	QSpinBox *duration_ = nullptr;
	QCheckBox *ownDuration_ = nullptr;

	QLineEdit *filter_ = nullptr;
	QListWidget *list_ = nullptr;
	QToolButton *newBtn_ = nullptr;
	QPushButton *dupBtn_ = nullptr;
	QPushButton *renameBtn_ = nullptr;
	QPushButton *removeBtn_ = nullptr;
	QPushButton *activeBtn_ = nullptr;
	QLabel *readOnlyNote_ = nullptr;

	PropertiesPanel *props_ = nullptr;
	obs_source_t *shown_ = nullptr; // compared only; props_ holds the ref
	QString shownName_;
	bool refreshing_ = false;
	bool reordering_ = false;
};
