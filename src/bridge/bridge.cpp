#include "bridge.hpp"

#include "../manager/manager-hooks.hpp"
#include "../manager/manager-module.hpp"
#include "../manager/transition-manager-window.hpp"
#include "../manager/transition-store.hpp"
#include "../tree/transition-tree-dialog.hpp"
#include "../tree/transition-tree.hpp"

#include <obs-module.h>

#include <QCursor>
#include <QMetaObject>
#include <QString>

namespace Bridge {

static QMetaObject::Connection g_storeChanged;

static QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

static obs_source_t *findTransition(const std::string &name)
{
	for (const OBSSource &source : TransitionStore::instance().transitions()) {
		const char *sourceName = obs_source_get_name(source);
		if (sourceName && name == sourceName)
			return source.Get(); // OBS's list keeps the reference
	}
	return nullptr;
}

// A scene on the named canvas (the main one when empty or unknown).
static obs_source_t *findScene(const std::string &canvasName, const std::string &scene)
{
	if (scene.empty())
		return nullptr;
	obs_canvas_t *canvas = canvasName.empty() ? nullptr : obs_get_canvas_by_name(canvasName.c_str());
	if (!canvas)
		canvas = obs_get_main_canvas();
	obs_source_t *source = canvas ? obs_canvas_get_source_by_name(canvas, scene.c_str()) : nullptr;
	obs_canvas_release(canvas);
	return source; // caller releases
}

static QString usageText(obs_source_t *source)
{
	const tt::TransitionUsage usage = tt::FindTransitionUsage(obs_source_get_name(source));
	if (usage.Empty())
		return QString();

	if (usage.defaults == 0)
		return text(usage.marks == 1 ? "Bridge.Usage.OneRule" : "Bridge.Usage.Rules").arg(usage.marks);
	if (usage.marks == 0)
		return text(usage.defaults == 1 ? "Bridge.Usage.OneDefault" : "Bridge.Usage.Defaults")
			.arg(usage.defaults);
	return text("Bridge.Usage.RulesAndDefaults").arg(usage.marks).arg(usage.defaults);
}

void connect()
{
	ManagerHooks::Hooks managerHooks;
	managerHooks.openRulesLabel = text("Bridge.ShowRules");
	managerHooks.openRules = [](obs_source_t *source) {
		const char *name = source ? obs_source_get_name(source) : nullptr;
		if (name && !tt::FindTransitionUsage(name).Empty())
			TransitionTreeDialog::ShowDialogFiltered(QString::fromUtf8(name));
		else
			TransitionTreeDialog::ShowDialog();
	};
	managerHooks.usage = usageText;
	ManagerHooks::set(managerHooks);

	tt::Integration treeHooks;
	treeHooks.hostName = obs_module_text("Bridge.ManagerName");
	treeHooks.createTransition = [](QWidget *parent, const std::string &suggestedName) -> std::string {
		TransitionManagerWindow *window = ManagerModule::window();
		if (!window)
			return std::string();
		obs_source_t *source =
			window->createFromMenu(parent, QCursor::pos(), QString::fromStdString(suggestedName));
		if (!source)
			return std::string();
		tt::RefreshTransitionHooks();
		const char *name = obs_source_get_name(source);
		return name ? name : "";
	};
	treeHooks.editTransition = [](const std::string &name) {
		TransitionManagerWindow *window = ManagerModule::window();
		if (window)
			window->open(findTransition(name));
	};
	treeHooks.defaultDuration = [](const std::string &name) {
		obs_source_t *source = findTransition(name);
		if (!source || obs_transition_fixed(source))
			return -1;
		return TransitionStore::instance().defaultDuration(source);
	};
	treeHooks.previewSwitch = [](const std::string &transition, int durationMs, const std::string &canvas,
				     const std::string &fromScene, const std::string &toScene) {
		TransitionManagerWindow *window = ManagerModule::window();
		obs_source_t *source = findTransition(transition);
		if (!window || !source)
			return;
		OBSSourceAutoRelease from = findScene(canvas, fromScene);
		OBSSourceAutoRelease to = findScene(canvas, toScene);
		window->previewSwitch(source, durationMs, from, to);
	};
	treeHooks.openHost = []() {
		if (TransitionManagerWindow *window = ManagerModule::window())
			window->open();
	};
	treeHooks.dataChanged = []() {
		if (TransitionManagerWindow *window = ManagerModule::window())
			window->refreshUsage();
	};
	tt::SetIntegration(treeHooks);

	// On OBS 31 and older the manager adds transitions straight to OBS's
	// list, which raises no list-changed event, so tell the tree directly.
	g_storeChanged = QObject::connect(&TransitionStore::instance(), &TransitionStore::changed,
					  []() { tt::RefreshTransitionHooks(); });
}

void disconnect()
{
	QObject::disconnect(g_storeChanged);
	ManagerHooks::set(ManagerHooks::Hooks());
	tt::SetIntegration(tt::Integration());
}

} // namespace Bridge
