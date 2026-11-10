#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <vector>

#include <obs.hpp>
#include <obs-frontend-api.h>

class QComboBox;

// Single source of truth for the plugin's UI.
//
// Every transition lives in OBS's own Scene Transitions list (the main window's
// "transitions" combo box). Transitions created here are added to that list
// the same way OBS's "+" button does, so OBS saves them with the scene
// collection and they work everywhere OBS uses transitions: scene switches,
// studio mode, quick transitions and per-scene overrides.
//
// The plugin only persists its own extras (per-transition default durations)
// under the "transition_manager" key of the scene collection.
class TransitionStore : public QObject {
	Q_OBJECT

public:
	static TransitionStore &instance();

	void init();
	void shutdown();

	// False if OBS's transition list widget couldn't be found (e.g. a future
	// OBS changed its layout). Viewing, previewing, editing settings and
	// switching still work; adding, removing, renaming and reordering don't.
	bool canEditList() const;

	std::vector<OBSSource> transitions() const;
	bool contains(obs_source_t *source) const;
	bool canRemove(obs_source_t *source) const;
	OBSSource current() const;

	// Returned pointers are borrowed; OBS's list keeps the reference.
	obs_source_t *createTransition(const char *typeId, const QString &name, obs_data_t *settings = nullptr);
	obs_source_t *duplicateTransition(obs_source_t *source, const QString &name);
	bool renameTransition(obs_source_t *source, const QString &name);
	bool removeTransition(obs_source_t *source);
	bool moveTransition(int from, int to);
	void setCurrent(obs_source_t *source);

	bool nameTaken(const QString &name, obs_source_t *ignore = nullptr) const;
	QString uniqueName(const QString &base, obs_source_t *ignore = nullptr) const;

	// Per-transition default duration in ms, applied to OBS's duration box
	// whenever that transition becomes active. -1 means "not set".
	int defaultDuration(obs_source_t *source) const;
	void setDefaultDuration(obs_source_t *source, int ms);
	void clearDefaultDuration(obs_source_t *source);

signals:
	void changed();             // list contents, names or order changed
	void currentChanged();      // OBS switched its active transition
	void durationChanged();     // OBS's global transition duration changed
	void scenesChanged();       // program/preview scene or scene list changed
	void collectionUnloading(); // drop any references into the scene collection

private:
	TransitionStore() = default;

	QComboBox *combo() const;
	int comboIndexOf(obs_source_t *source) const;
	void hookTransition(obs_source_t *source);
	void unhookAll();
	void applyDefaultDuration();

	static void onFrontendEvent(enum obs_frontend_event event, void *data);
	static void onSave(obs_data_t *saveData, bool saving, void *data);
	static void onTransitionStop(void *data, calldata_t *cd);
	static void onTransitionFullStop(void *data, calldata_t *cd);

	mutable QPointer<QComboBox> combo_;
	mutable bool comboSearched_ = false;

	QHash<QString, int> durations_; // keyed by transition name
	std::vector<OBSWeakSource> hooked_;
	bool initialized_ = false;
	bool frontendExited_ = false; // OBS drops frontend callbacks itself after EXIT
};
