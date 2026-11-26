#pragma once

#include <obs.h>

class QWidget;

// Transition packs: transitions, the media they use (Stinger videos, Luma
// Wipe images, ...), their durations and actions, and optionally the
// Transition Tree presets that use them, in one .tmpack file (a zip).
namespace TransitionPack {

// Asks which transitions to include and where to save. `preselect` starts
// checked.
void exportPack(QWidget *parent, obs_source_t *preselect);

// Asks for a pack, copies its media into the plugin's config folder and adds
// its transitions (and presets) to this scene collection.
void importPack(QWidget *parent);

} // namespace TransitionPack
