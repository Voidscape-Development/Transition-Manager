#pragma once

#include <QWidget>
#include <obs.hpp>

class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QSpinBox;
class TransitionManagerWindow;

// Compact dock that works like OBS's own Scene Transitions dock: pick the
// active transition, set the duration, add/remove, and a ⋮ menu. The full
// editor lives in TransitionManagerWindow, opened from here.
class TransitionsDock : public QWidget {
	Q_OBJECT

public:
	TransitionsDock(TransitionManagerWindow *window, QWidget *parent = nullptr);

private slots:
	void refresh();
	void syncDuration();
	void onComboChanged(int index);
	void onDurationChanged(int ms);
	void onRemove();
	void showPropsMenu();

private:
	obs_source_t *active() const;
	void populateAddMenu(QMenu *menu);
	void addTransition(const QString &typeId);
	void updateControls();

	TransitionManagerWindow *window_ = nullptr;

	QComboBox *combo_ = nullptr;
	QLabel *durationLabel_ = nullptr;
	QSpinBox *duration_ = nullptr;
	QPushButton *addBtn_ = nullptr;
	QPushButton *removeBtn_ = nullptr;
	QPushButton *propsBtn_ = nullptr;
	QPushButton *managerBtn_ = nullptr;
};
