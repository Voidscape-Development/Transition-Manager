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

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QMainWindow>

#include "transition-manager-window.hpp"
#include "transition-store.hpp"
#include "transitions-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Transition Manager";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Dock for creating, editing, previewing, duplicating and organizing scene transitions.";
}

bool obs_module_load(void)
{
	TransitionStore::instance().init();

	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	// The full editor is a separate window, owned by the main window and
	// hidden until opened from the dock or the Tools menu.
	auto *window = new TransitionManagerWindow(main);
	auto *dock = new TransitionsDock(window, main);

	// OBS 30+: OBS takes ownership of the widget.
	if (!obs_frontend_add_dock_by_id("transition_manager_dock", "Transition Manager", dock)) {
		obs_log(LOG_WARNING, "failed to add dock");
		delete dock;
	}

	obs_frontend_add_tools_menu_item(
		"Transition Manager", [](void *data) { static_cast<TransitionManagerWindow *>(data)->open(); }, window);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	TransitionStore::instance().shutdown();
	obs_log(LOG_INFO, "plugin unloaded");
}
