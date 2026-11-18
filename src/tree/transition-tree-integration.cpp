/*
Transition Tree
Copyright (C) 2026 Voidscape Development <dev@voidscape.dev>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

/* Transition Tree's module lifecycle, its optional integration hooks, and the
 * bookkeeping that keeps rules pointing at transitions when they are renamed.
 * Nothing here depends on any other component. */

#include "transition-tree.hpp"
#include "transition-tree-dialog.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QMainWindow>
#include <QMetaObject>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace tt {

namespace {

Integration g_integration;

/* Transitions whose "rename" signal is connected. Weak, so a transition that
 * is removed simply expires here. */
std::vector<obs_weak_source_t *> g_hookedTransitions;

void OnTransitionRename(void *, calldata_t *callData)
{
	const char *rawPrevious = calldata_string(callData, "prev_name");
	const char *rawNext = calldata_string(callData, "new_name");
	if (!rawPrevious || !rawNext)
		return;

	const std::string previous = rawPrevious;
	const std::string next = rawNext;
	if (previous == next)
		return;

	auto task = [previous, next]() {
		if (!RenameTransitionReferences(previous, next))
			return;
		obs_log(LOG_INFO, "rules now use renamed transition '%s' (was '%s')", next.c_str(), previous.c_str());
	};

	auto *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	if (mainWindow)
		QMetaObject::invokeMethod(mainWindow, task, Qt::QueuedConnection);
	else
		task();
}

void UnhookTransitions()
{
	for (obs_weak_source_t *weak : g_hookedTransitions) {
		obs_source_t *source = obs_weak_source_get_source(weak);
		if (source) {
			signal_handler_disconnect(obs_source_get_signal_handler(source), "rename", OnTransitionRename,
						  nullptr);
			obs_source_release(source);
		}
		obs_weak_source_release(weak);
	}
	g_hookedTransitions.clear();
}

void ProcGetTransition(void *, calldata_t *callData)
{
	/* Mirrors Transition Table's proc so tooling can ask what the tree would
	 * do for a given switch without going through obs-websocket. */
	const char *value = nullptr;

	std::string canvas;
	if (calldata_get_string(callData, "canvas", &value) && value)
		canvas = value;
	else
		canvas = MainCanvasName();

	value = nullptr;
	std::string fromScene;
	if (calldata_get_string(callData, "from_scene", &value) && value)
		fromScene = value;

	value = nullptr;
	std::string toScene;
	if (calldata_get_string(callData, "to_scene", &value) && value)
		toScene = value;

	std::string transition;
	int duration = 0;
	ResolveTransition(canvas, fromScene, toScene, transition, duration);

	calldata_set_string(callData, "transition", transition.c_str());
	calldata_set_int(callData, "duration", duration);
}

constexpr const char *kProcSignature =
	"void get_transition_tree_transition(string canvas, string from_scene, string to_scene, out string transition, out int duration)";

bool g_loaded = false;

} // namespace

/* ------------------------------------------------------------------------ */
/* Integration                                                               */
/* ------------------------------------------------------------------------ */

void SetIntegration(Integration integration)
{
	/* Set before the editor first opens; its buttons are built from it. */
	g_integration = std::move(integration);
}

const Integration &GetIntegration()
{
	return g_integration;
}

void NotifyIntegration()
{
	if (g_integration.dataChanged)
		g_integration.dataChanged();
}

/* ------------------------------------------------------------------------ */
/* Transition references                                                     */
/* ------------------------------------------------------------------------ */

bool RenameTransitionReferences(const std::string &prev, const std::string &next)
{
	if (prev.empty() || prev == next)
		return false;

	Store &store = GetStore();
	bool changed = false;
	bool globalChanged = false;

	for (auto &preset : store.presets) {
		bool presetChanged = false;
		if (preset.defaultTransition == prev) {
			preset.defaultTransition = next;
			presetChanged = true;
		}
		for (auto &mark : preset.marks) {
			for (auto &candidate : mark.candidates) {
				if (candidate.transition == prev) {
					candidate.transition = next;
					presetChanged = true;
				}
			}
		}
		if (presetChanged) {
			changed = true;
			globalChanged = globalChanged || preset.global;
		}
	}

	if (!changed)
		return false;

	store.Touch();
	if (globalChanged)
		SaveGlobalPresets();
	ApplyOverrides();
	TransitionTreeDialog::NotifyDataChanged();
	return true;
}

TransitionUsage FindTransitionUsage(const std::string &transition)
{
	TransitionUsage usage;
	if (transition.empty())
		return usage;

	for (const auto &preset : GetStore().presets) {
		bool used = false;
		if (preset.setDefaultTransition && preset.defaultTransition == transition) {
			usage.defaults++;
			used = true;
		}
		for (const auto &mark : preset.marks) {
			const bool inMark = std::any_of(mark.candidates.begin(), mark.candidates.end(),
							[&](const Candidate &c) { return c.transition == transition; });
			if (inMark) {
				usage.marks++;
				used = true;
			}
		}
		if (used)
			usage.presets.push_back(preset.name);
	}
	return usage;
}

void RefreshTransitionHooks()
{
	/* Drop the ones that are gone, then add any not yet hooked. */
	g_hookedTransitions.erase(std::remove_if(g_hookedTransitions.begin(), g_hookedTransitions.end(),
						 [](obs_weak_source_t *weak) {
							 if (!obs_weak_source_expired(weak))
								 return false;
							 obs_weak_source_release(weak);
							 return true;
						 }),
				  g_hookedTransitions.end());

	struct obs_frontend_source_list transitions = {};
	obs_frontend_get_transitions(&transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		obs_source_t *source = transitions.sources.array[i];
		const bool hooked = std::any_of(g_hookedTransitions.begin(), g_hookedTransitions.end(),
						[source](obs_weak_source_t *weak) {
							return obs_weak_source_references_source(weak, source);
						});
		if (hooked)
			continue;
		signal_handler_connect(obs_source_get_signal_handler(source), "rename", OnTransitionRename, nullptr);
		g_hookedTransitions.push_back(obs_source_get_weak_source(source));
	}
	obs_frontend_source_list_free(&transitions);
}

void ReleaseTransitionHooks()
{
	UnhookTransitions();
}

/* ------------------------------------------------------------------------ */
/* Module lifecycle                                                          */
/* ------------------------------------------------------------------------ */

void ModuleLoad()
{
	if (g_loaded)
		return;
	g_loaded = true;

	obs_log(LOG_INFO, "loading Transition Tree");

	auto *action = (QAction *)obs_frontend_add_tools_menu_qaction(obs_module_text("TransitionTree"));
	if (action)
		QAction::connect(action, &QAction::triggered, []() { TransitionTreeDialog::ShowDialog(); });

	EngineInit();
	proc_handler_add(obs_get_proc_handler(), kProcSignature, ProcGetTransition, nullptr);
}

void ModulePostLoad()
{
	if (!g_loaded)
		return;
	WebsocketInit();
}

void ModuleUnload()
{
	if (!g_loaded)
		return;
	g_loaded = false;

	UnhookTransitions();
	g_integration = Integration();
	WebsocketShutdown();
	EngineShutdown();
	obs_log(LOG_INFO, "Transition Tree unloaded");
}

} // namespace tt
