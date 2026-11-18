#include "manager-module.hpp"

#include "transition-manager-window.hpp"
#include "transition-store.hpp"
#include "transitions-dock.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QMainWindow>
#include <QPointer>

namespace ManagerModule {

static QPointer<TransitionManagerWindow> g_window;

bool load()
{
	TransitionStore::instance().init();

	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	// The full editor is a separate window, owned by the main window and
	// hidden until opened from the dock or the Tools menu.
	auto *window = new TransitionManagerWindow(main);
	auto *dock = new TransitionsDock(window, main);
	g_window = window;

	// OBS 30+: OBS takes ownership of the widget.
	if (!obs_frontend_add_dock_by_id("transition_manager_dock", "Transition Manager", dock)) {
		obs_log(LOG_WARNING, "failed to add dock");
		delete dock;
	}

	obs_frontend_add_tools_menu_item(
		"Transition Manager", [](void *data) { static_cast<TransitionManagerWindow *>(data)->open(); }, window);

	return true;
}

void unload()
{
	TransitionStore::instance().shutdown();
}

TransitionManagerWindow *window()
{
	return g_window.data();
}

} // namespace ManagerModule
