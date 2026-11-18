/*
Transition Manager
Copyright (C) 2026 Voidscape Development

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

// One OBS module, two independent components:
//
//  - manager/  Transition Manager: the dock and window for creating, editing
//              and previewing transitions.
//  - tree/     Transition Tree: rules that pick the transition for each scene
//              switch (built when ENABLE_TRANSITION_TREE is on).
//  - bridge/   The glue that lets each use the other. Neither component
//              depends on it or on each other.

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <cstring>

#include "manager/manager-module.hpp"

#ifdef TM_WITH_TRANSITION_TREE
#include "bridge/bridge.hpp"
#include "tree/transition-tree.hpp"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Voidscape Development")
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("PluginName");
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("PluginDescription");
}

#ifdef TM_WITH_TRANSITION_TREE
static bool g_treeLoaded = false;

// The standalone Transition Tree plugin registers the same hotkeys, websocket
// vendor, proc handler and save data. If it's still installed, let it keep
// running on its own and leave the bundled copy off.
static bool standaloneTreeLoaded()
{
	bool found = false;
	obs_enum_modules(
		[](void *param, obs_module_t *module) {
			const char *file = obs_get_module_file_name(module);
			if (module != obs_current_module() && file && strstr(file, "transition-tree"))
				*static_cast<bool *>(param) = true;
		},
		&found);
	return found;
}
#endif

bool obs_module_load(void)
{
	if (!ManagerModule::load())
		return false;

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
#ifdef TM_WITH_TRANSITION_TREE
	// Started here rather than in obs_module_load() so every other module is
	// loaded and a standalone Transition Tree can be detected.
	if (standaloneTreeLoaded()) {
		obs_log(LOG_WARNING, "the standalone Transition Tree plugin is installed; its built-in copy stays off. "
				     "Remove the standalone plugin to use the one included here.");
		return;
	}

	tt::ModuleLoad();
	tt::ModulePostLoad();
	Bridge::connect();
	g_treeLoaded = true;
#endif
}

void obs_module_unload(void)
{
#ifdef TM_WITH_TRANSITION_TREE
	if (g_treeLoaded) {
		Bridge::disconnect();
		tt::ModuleUnload();
		g_treeLoaded = false;
	}
#endif
	ManagerModule::unload();
	obs_log(LOG_INFO, "plugin unloaded");
}
