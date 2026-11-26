#pragma once

#include <QString>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <obs.h>

// Optional extension points for the Transition Manager.
//
// The manager never depends on any other component. A host (the bundle's
// bridge) can fill these in to add features that need another component,
// such as Transition Tree's rules. Every hook may be left empty, in which case
// the related UI is hidden and the manager behaves exactly as it does alone.
//
// All hooks are called on the Qt main thread.
namespace ManagerHooks {

struct Hooks {
	// Label and action for a "rules" entry offered in the dock's ⋮ menu and
	// the window's actions menu. The source is the transition the menu was
	// opened for, or nullptr.
	QString openRulesLabel;
	std::function<void(obs_source_t *source)> openRules;

	// A short, translated description of what else uses this transition
	// (e.g. "Used by 3 rules in Transition Tree"), shown in the window's
	// header and when confirming a removal. Empty when nothing does.
	std::function<QString(obs_source_t *source)> usage;

	// Transition packs: the Transition Tree presets that use any of
	// `transitions`, as JSON (empty when none), and adding presets from that
	// JSON with transitions renamed per `renames`. Returns how many were added.
	std::function<std::string(const std::vector<std::string> &transitions)> exportRules;
	std::function<int(const std::string &json, const std::map<std::string, std::string> &renames)> importRules;
};

// Replaces the current hooks.
void set(Hooks hooks);
const Hooks &get();

// Convenience accessors, safe when the hook is unset.
bool hasRules();
void openRules(obs_source_t *source);
QString usage(obs_source_t *source);

} // namespace ManagerHooks
