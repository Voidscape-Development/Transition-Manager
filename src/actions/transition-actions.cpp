#include "transition-actions.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <map>

namespace TransitionActions {

static const char *kSaveKey = "transition_actions";

static ActionSet g_global;
static std::map<std::string, ActionSet> g_transitions;
static MarkResolver g_resolver;
static std::vector<OBSWeakSource> g_hooked;
// End actions waiting for each running transition (compared only).
static std::map<obs_source_t *, std::pair<std::vector<Action>, std::string>> g_pending;
static bool g_initialized = false;

// ---- model -------------------------------------------------------------------

obs_data_t *save(const ActionSet &set)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_bool(data, "replace_broader", set.replaceBroader);
	OBSDataArrayAutoRelease list = obs_data_array_create();
	for (const Action &a : set.actions) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_int(item, "when", (int)a.when);
		obs_data_set_int(item, "kind", (int)a.kind);
		obs_data_set_string(item, "source", a.source.c_str());
		obs_data_set_string(item, "scene", a.scene.c_str());
		obs_data_set_string(item, "hotkey", a.hotkey.c_str());
		obs_data_set_double(item, "volume_db", a.volumeDb);
		obs_data_set_int(item, "delay_ms", a.delayMs);
		obs_data_array_push_back(list, item);
	}
	obs_data_set_array(data, "actions", list);
	return data;
}

ActionSet load(obs_data_t *data)
{
	ActionSet set;
	if (!data)
		return set;
	set.replaceBroader = obs_data_get_bool(data, "replace_broader");
	OBSDataArrayAutoRelease list = obs_data_get_array(data, "actions");
	for (size_t i = 0; i < obs_data_array_count(list); ++i) {
		OBSDataAutoRelease item = obs_data_array_item(list, i);
		const int kind = (int)obs_data_get_int(item, "kind");
		if (kind < 0 || kind > (int)Kind::Hotkey)
			continue;
		Action a;
		a.when = obs_data_get_int(item, "when") == 1 ? When::End : When::Start;
		a.kind = (Kind)kind;
		a.source = obs_data_get_string(item, "source");
		a.scene = obs_data_get_string(item, "scene");
		a.hotkey = obs_data_get_string(item, "hotkey");
		a.volumeDb = obs_data_get_double(item, "volume_db");
		a.delayMs = (int)obs_data_get_int(item, "delay_ms");
		set.actions.push_back(a);
	}
	return set;
}

QString summary(const ActionSet &set, bool showReplace)
{
	int start = 0, end = 0;
	for (const Action &a : set.actions)
		(a.when == When::Start ? start : end)++;
	QString text = set.actions.empty() ? QObject::tr("No actions")
					   : QObject::tr("%1 at start, %2 at end").arg(start).arg(end);
	if (showReplace && set.replaceBroader)
		text += QObject::tr(" · replaces broader actions");
	return text;
}

const ActionSet &global()
{
	return g_global;
}

void setGlobal(const ActionSet &set)
{
	g_global = set;
}

ActionSet forTransition(const std::string &name)
{
	auto it = g_transitions.find(name);
	return it == g_transitions.end() ? ActionSet() : it->second;
}

void setForTransition(const std::string &name, const ActionSet &set)
{
	if (set.empty())
		g_transitions.erase(name);
	else
		g_transitions[name] = set;
}

void setMarkResolver(MarkResolver resolver)
{
	g_resolver = std::move(resolver);
}

// ---- running actions -----------------------------------------------------------

static void runNow(const Action &a, const std::string &toScene)
{
	if (a.kind == Kind::Hotkey) {
		struct Find {
			const Action *a;
			obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
		} find{&a};
		obs_enum_hotkeys(
			[](void *data, obs_hotkey_id id, obs_hotkey_t *key) {
				auto *f = static_cast<Find *>(data);
				if (f->a->hotkey != obs_hotkey_get_name(key))
					return true;
				std::string owner;
				if (obs_hotkey_get_registerer_type(key) == OBS_HOTKEY_REGISTERER_SOURCE) {
					OBSSourceAutoRelease s = obs_weak_source_get_source(
						static_cast<obs_weak_source_t *>(obs_hotkey_get_registerer(key)));
					owner = s ? obs_source_get_name(s) : "";
				}
				if (owner != f->a->source)
					return true;
				f->id = id;
				return false;
			},
			&find);
		if (find.id != OBS_INVALID_HOTKEY_ID) {
			obs_hotkey_trigger_routed_callback(find.id, true);
			obs_hotkey_trigger_routed_callback(find.id, false);
		}
		return;
	}

	if (a.kind == Kind::Show || a.kind == Kind::Hide) {
		OBSSourceAutoRelease sceneSource =
			obs_get_source_by_name((a.scene.empty() ? toScene : a.scene).c_str());
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		if (obs_sceneitem_t *item = scene ? obs_scene_find_source_recursive(scene, a.source.c_str()) : nullptr)
			obs_sceneitem_set_visible(item, a.kind == Kind::Show);
		return;
	}

	OBSSourceAutoRelease source = obs_get_source_by_name(a.source.c_str());
	if (!source)
		return;
	switch (a.kind) {
	case Kind::Mute:
	case Kind::Unmute:
		obs_source_set_muted(source, a.kind == Kind::Mute);
		break;
	case Kind::ToggleMute:
		obs_source_set_muted(source, !obs_source_muted(source));
		break;
	case Kind::SetVolume:
		obs_source_set_volume(source, (float)std::pow(10.0, a.volumeDb / 20.0));
		break;
	case Kind::RestartMedia:
		obs_source_media_restart(source);
		break;
	default:
		break;
	}
}

static void run(const std::vector<Action> &actions, const std::string &toScene)
{
	for (const Action &a : actions) {
		if (a.delayMs > 0)
			QTimer::singleShot(a.delayMs, qApp, [a, toScene] { runNow(a, toScene); });
		else
			runNow(a, toScene);
	}
}

static void onMain(std::function<void()> task)
{
	if (QThread::currentThread() == qApp->thread())
		task();
	else
		QMetaObject::invokeMethod(qApp, std::move(task), Qt::QueuedConnection);
}

static void started(obs_source_t *transition, const std::string &toScene)
{
	// A restart before the stop: finish the previous run first.
	if (auto it = g_pending.find(transition); it != g_pending.end()) {
		auto previous = std::move(it->second);
		g_pending.erase(it);
		run(previous.first, previous.second);
	}

	const ActionSet *mark = g_resolver ? g_resolver(transition, toScene) : nullptr;
	const ActionSet own = forTransition(obs_source_get_name(transition));

	std::vector<Action> start, end;
	auto add = [&](const ActionSet &set) {
		for (const Action &a : set.actions)
			(a.when == When::Start ? start : end).push_back(a);
	};
	if (!mark || !mark->replaceBroader) {
		if (!own.replaceBroader)
			add(g_global);
		add(own);
	}
	if (mark)
		add(*mark);

	run(start, toScene);
	if (!end.empty())
		g_pending[transition] = {std::move(end), toScene};
}

static void onStart(void *, calldata_t *cd)
{
	OBSSource transition = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	OBSSourceAutoRelease dest = obs_transition_get_source(transition, OBS_TRANSITION_SOURCE_B);
	const std::string toScene = dest ? obs_source_get_name(dest) : "";
	// Normally already on the main thread, before Transition Tree re-arms.
	onMain([transition, toScene] { started(transition, toScene); });
}

static void onStop(void *, calldata_t *cd)
{
	OBSSource transition = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	onMain([transition] {
		auto it = g_pending.find(transition.Get());
		if (it == g_pending.end())
			return;
		auto pending = std::move(it->second);
		g_pending.erase(it);
		run(pending.first, pending.second);
	});
}

static void onRename(void *, calldata_t *cd)
{
	const std::string prev = calldata_string(cd, "prev_name");
	const std::string next = calldata_string(cd, "new_name");
	onMain([prev, next] {
		auto node = g_transitions.extract(prev);
		if (!node.empty()) {
			node.key() = next;
			g_transitions.insert(std::move(node));
		}
	});
}

static void releaseHooks()
{
	for (const OBSWeakSource &weak : g_hooked) {
		OBSSourceAutoRelease s = obs_weak_source_get_source(weak);
		if (!s)
			continue;
		signal_handler_t *sh = obs_source_get_signal_handler(s);
		signal_handler_disconnect(sh, "transition_start", onStart, nullptr);
		signal_handler_disconnect(sh, "transition_stop", onStop, nullptr);
		signal_handler_disconnect(sh, "rename", onRename, nullptr);
	}
	g_hooked.clear();
	g_pending.clear();
}

void refreshHooks()
{
	releaseHooks();
	obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	for (size_t i = 0; i < list.sources.num; ++i) {
		obs_source_t *s = list.sources.array[i];
		signal_handler_t *sh = obs_source_get_signal_handler(s);
		signal_handler_connect(sh, "transition_start", onStart, nullptr);
		signal_handler_connect(sh, "transition_stop", onStop, nullptr);
		signal_handler_connect(sh, "rename", onRename, nullptr);
		OBSWeakSourceAutoRelease weak = obs_source_get_weak_source(s);
		g_hooked.emplace_back(weak.Get());
	}
	obs_frontend_source_list_free(&list);
}

// ---- lifecycle -----------------------------------------------------------------

static void onEvent(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED:
		refreshHooks();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		releaseHooks();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		releaseHooks();
		g_initialized = false; // OBS drops frontend callbacks itself after EXIT
		break;
	default:
		break;
	}
}

static void onSave(obs_data_t *saveData, bool saving, void *)
{
	if (saving) {
		OBSDataAutoRelease root = obs_data_create();
		OBSDataAutoRelease globalData = save(g_global);
		obs_data_set_obj(root, "global", globalData);
		std::vector<std::string> names; // drop entries of removed transitions
		obs_frontend_source_list list = {};
		obs_frontend_get_transitions(&list);
		for (size_t i = 0; i < list.sources.num; ++i)
			names.emplace_back(obs_source_get_name(list.sources.array[i]));
		obs_frontend_source_list_free(&list);

		OBSDataAutoRelease perTransition = obs_data_create();
		for (const auto &[name, set] : g_transitions) {
			if (std::find(names.begin(), names.end(), name) == names.end())
				continue;
			OBSDataAutoRelease item = save(set);
			obs_data_set_obj(perTransition, name.c_str(), item);
		}
		obs_data_set_obj(root, "transitions", perTransition);
		obs_data_set_obj(saveData, kSaveKey, root);
		return;
	}

	g_transitions.clear();
	OBSDataAutoRelease root = obs_data_get_obj(saveData, kSaveKey);
	OBSDataAutoRelease globalData = obs_data_get_obj(root, "global");
	g_global = load(globalData);
	OBSDataAutoRelease perTransition = obs_data_get_obj(root, "transitions");
	for (obs_data_item_t *item = perTransition ? obs_data_first(perTransition) : nullptr; item;
	     obs_data_item_next(&item)) {
		OBSDataAutoRelease obj = obs_data_item_get_obj(item);
		setForTransition(obs_data_item_get_name(item), load(obj));
	}
}

void init()
{
	obs_frontend_add_event_callback(onEvent, nullptr);
	obs_frontend_add_save_callback(onSave, nullptr);
	g_initialized = true;
}

void shutdown()
{
	if (g_initialized) {
		obs_frontend_remove_event_callback(onEvent, nullptr);
		obs_frontend_remove_save_callback(onSave, nullptr);
		g_initialized = false;
	}
	releaseHooks(); // the callbacks live in this module
	g_resolver = nullptr;
}

} // namespace TransitionActions
