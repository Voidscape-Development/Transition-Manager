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
// Every transition lives in OBS's own Scene Transitions list, so OBS saves
// them with the scene collection and they work everywhere OBS uses
// transitions: scene switches, studio mode, quick transitions and per-scene
// overrides. How that list is reached depends on the OBS version:
//
//  - OBS 31 and older (ListMode::Legacy): the main window's "transitions"
//    combo box *is* the list; each item holds the transition as an OBSSource.
//    Edits are made directly on the combo box, the same way OBS's own
//    buttons do.
//  - OBS 32 and newer (ListMode::Uuid): OBS keeps the list in a private map
//    and the combo box is only a view whose items hold transition UUIDs.
//    Writing to the combo box would not reach OBS, so adding, renaming and
//    removing go through the main window's own slots instead. OBS then shows
//    its own name/confirmation dialogs (see obsPrompts()). OBS 32+ saves
//    transitions in no particular order, so reordering isn't possible.
//
// The plugin only persists its own extras (per-transition default durations)
// under the "transition_manager" key of the scene collection.
class TransitionStore : public QObject {
	Q_OBJECT

public:
	static TransitionStore &instance();

	void init();
	void shutdown();

	enum class ListMode {
		Unavailable, // OBS's list couldn't be found or understood
		Legacy,      // OBS 31 and older: combo box items hold OBSSource
		Uuid,        // OBS 32 and newer: combo box items hold UUIDs
	};
	ListMode listMode() const;

	// False if OBS's transition list couldn't be reached (e.g. a future OBS
	// changed its internals). Viewing, previewing, editing settings and
	// switching still work; adding, removing and renaming don't.
	bool canEditList() const;
	// Reordering is only possible where OBS keeps the list's order.
	bool canReorder() const;
	// True when OBS asks for names and confirms removals itself (OBS 32+), so
	// the plugin must not ask first. Names passed to createTransition() and
	// renameTransition() are then ignored.
	bool obsPrompts() const;

	std::vector<OBSSource> transitions() const;
	bool contains(obs_source_t *source) const;
	// allowActive: the active transition may be removed too; OBS is switched
	// to another one first (Fade if present), like OBS's own remove button.
	bool canRemove(obs_source_t *source, bool allowActive = false) const;
	OBSSource current() const;

	// Returned pointers are borrowed; OBS's list keeps the reference.
	// The active transition doesn't change. Returns nullptr on failure, or if
	// the user cancelled OBS's own dialog (see obsPrompts()).
	obs_source_t *createTransition(const char *typeId, const QString &name, obs_data_t *settings = nullptr);
	obs_source_t *duplicateTransition(obs_source_t *source, const QString &name);
	bool renameTransition(obs_source_t *source, const QString &name);
	bool removeTransition(obs_source_t *source, bool allowActive = false);
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
	QObject *mainWindow() const;
	bool hasMainSlot(const char *signature) const;
	int comboIndexOf(obs_source_t *source) const;
	obs_source_t *createViaObs(const char *typeId, obs_data_t *settings);
	bool renameViaObs(obs_source_t *source);
	bool removeViaObs(obs_source_t *source, bool allowActive);
	void restoreCurrent(obs_source_t *previous, int previousDuration);
	OBSSource fallbackFor(obs_source_t *source) const;
	void hookTransition(obs_source_t *source);
	void unhookAll();
	void applyDefaultDuration();

	static void onFrontendEvent(enum obs_frontend_event event, void *data);
	static void onSave(obs_data_t *saveData, bool saving, void *data);
	static void onTransitionStop(void *data, calldata_t *cd);
	static void onTransitionFullStop(void *data, calldata_t *cd);

	mutable QPointer<QComboBox> combo_;
	mutable bool comboSearched_ = false;
	mutable bool warnedUnsupported_ = false;

	QHash<QString, int> durations_; // keyed by transition name
	std::vector<OBSWeakSource> hooked_;
	bool initialized_ = false;
	bool frontendExited_ = false; // OBS drops frontend callbacks itself after EXIT
};
