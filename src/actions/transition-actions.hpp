#pragma once

#include <obs.h>

#include <QString>
#include <functional>
#include <string>
#include <vector>

class QWidget;

// Actions that run when a transition starts or ends: mute or set the volume
// of an audio source, show or hide a source, restart a media source, or
// press a hotkey.
//
// They're set at three levels, from broad to narrow:
//  - global: every transition
//  - per transition: whenever that transition plays
//  - per Transition Tree rule: whenever that rule drives the switch
// All matching levels run, broadest first. A narrower level can replace the
// broader ones instead (ActionSet::replaceBroader).
//
// Shared by the manager and Transition Tree; depends on neither.
namespace TransitionActions {

enum class When { Start = 0, End = 1 };

// Serialized; keep the values stable.
enum class Kind { Mute = 0, Unmute, ToggleMute, SetVolume, Show, Hide, RestartMedia, Hotkey };

struct Action {
	When when = When::Start;
	Kind kind = Kind::Mute;
	std::string source; // the target; for Hotkey, the source that owns it (empty: OBS's own)
	std::string scene;  // Show/Hide: the scene holding it (empty: the scene being switched to)
	std::string hotkey; // Hotkey: OBS's hotkey name
	double volumeDb = -20.0;
	int delayMs = 0;
};

struct ActionSet {
	bool replaceBroader = false;
	std::vector<Action> actions;

	bool empty() const { return actions.empty() && !replaceBroader; }
};

obs_data_t *save(const ActionSet &set); // caller releases
ActionSet load(obs_data_t *data);
QString summary(const ActionSet &set, bool showReplace = true);

// Stored with the scene collection.
const ActionSet &global();
void setGlobal(const ActionSet &set);
ActionSet forTransition(const std::string &name);
void setForTransition(const std::string &name, const ActionSet &set);

// Finds the Transition Tree rule that drove a switch to `toScene` with
// `transition`, if any. Called on the Qt main thread when it starts.
using MarkResolver = std::function<const ActionSet *(obs_source_t *transition, const std::string &toScene)>;
void setMarkResolver(MarkResolver resolver);

// Modal editor. `level` names what the actions belong to in the dialog;
// `canReplace` offers "replace broader actions". Returns true when edited.
bool edit(QWidget *parent, const QString &level, ActionSet &set, bool canReplace);

// Re-reads which transitions exist. Called on OBS's list events; call it
// too after adding a transition OBS doesn't announce.
void refreshHooks();

void init();
void shutdown();

} // namespace TransitionActions
