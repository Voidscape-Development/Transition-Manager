#pragma once

#include <QString>
#include <obs.hpp>

class QWidget;

// Small dialogs shared by the Transitions dock and the Transition Manager
// window, so both behave the same.
namespace TransitionDialogs {

// Asks for a transition name, re-asking while it's empty or already taken.
// Returns an empty string if cancelled. `ignore` is excluded from the
// uniqueness check (the transition being renamed).
QString askName(QWidget *parent, const QString &title, const QString &initial, obs_source_t *ignore = nullptr);

// Prompts for a new name and renames. Returns true if renamed.
bool rename(QWidget *parent, obs_source_t *source);

// Confirms and removes. allowActive: see TransitionStore::removeTransition.
bool remove(QWidget *parent, obs_source_t *source, bool allowActive);

} // namespace TransitionDialogs
