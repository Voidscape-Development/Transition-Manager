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
	treeHooks.createTransition = [](QWidget *parent) -> std::string {
		TransitionManagerWindow *window = ManagerModule::window();
		if (!window)
			return std::string();
		obs_source_t *source = window->createFromMenu(parent, QCursor::pos());
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
