#include "transition-store.hpp"
#include "obs-qt-types.hpp"

#include <plugin-support.h>

#include <QComboBox>
#include <QMainWindow>
#include <QSignalBlocker>
#include <algorithm>

static const char *kSaveKey = "transition_manager";

TransitionStore &TransitionStore::instance()
{
	// Intentionally leaked: avoids destroying a QObject after QApplication.
	static TransitionStore *store = new TransitionStore();
	return *store;
}

void TransitionStore::init()
{
	if (initialized_)
		return;
	obs_frontend_add_event_callback(onFrontendEvent, this);
	obs_frontend_add_save_callback(onSave, this);
	initialized_ = true;
}

void TransitionStore::shutdown()
{
	if (!initialized_)
		return;
	if (!frontendExited_) {
		obs_frontend_remove_save_callback(onSave, this);
		obs_frontend_remove_event_callback(onFrontendEvent, this);
	}
	unhookAll();
	durations_.clear();
	initialized_ = false;
}

// ---- OBS's transition list ---------------------------------------------------

QComboBox *TransitionStore::combo() const
{
	if (combo_)
		return combo_;

	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (main)
		combo_ = main->findChild<QComboBox *>(QStringLiteral("transitions"));

	if (!combo_ && !comboSearched_)
		obs_log(LOG_WARNING, "OBS's transition list wasn't found; adding, removing, renaming and "
				     "reordering transitions is disabled");
	comboSearched_ = true;
	return combo_;
}

bool TransitionStore::canEditList() const
{
	QComboBox *box = combo();
	if (!box)
		return false;

	// Make sure the widget really is the list the frontend API reports, in
	// the same order, before writing to it.
	const auto list = transitions();
	if ((size_t)box->count() != list.size())
		return false;
	for (int i = 0; i < box->count(); ++i) {
		if (box->itemData(i).value<OBSSource>().Get() != list[(size_t)i].Get())
			return false;
	}
	return true;
}

int TransitionStore::comboIndexOf(obs_source_t *source) const
{
	QComboBox *box = combo();
	if (!box || !source)
		return -1;
	for (int i = 0; i < box->count(); ++i) {
		if (box->itemData(i).value<OBSSource>().Get() == source)
			return i;
	}
	return -1;
}

std::vector<OBSSource> TransitionStore::transitions() const
{
	std::vector<OBSSource> out;
	struct obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	out.reserve(list.sources.num);
	for (size_t i = 0; i < list.sources.num; ++i)
		out.emplace_back(list.sources.array[i]);
	obs_frontend_source_list_free(&list);
	return out;
}

bool TransitionStore::contains(obs_source_t *source) const
{
	if (!source)
		return false;
	for (const OBSSource &s : transitions())
		if (s.Get() == source)
			return true;
	return false;
}

bool TransitionStore::canRemove(obs_source_t *source) const
{
	// Same rules as OBS's own remove button (Cut, Fade and other
	// unconfigurable transitions are built in), plus: never pull the
	// active transition out from under the program output.
	return source && canEditList() && obs_source_configurable(source) && current().Get() != source;
}

OBSSource TransitionStore::current() const
{
	OBSSourceAutoRelease cur = obs_frontend_get_current_transition();
	return OBSSource(cur.Get());
}

bool TransitionStore::nameTaken(const QString &name, obs_source_t *ignore) const
{
	for (const OBSSource &s : transitions()) {
		if (s.Get() == ignore)
			continue;
		if (name == QString::fromUtf8(obs_source_get_name(s)))
			return true;
	}
	return false;
}

QString TransitionStore::uniqueName(const QString &base, obs_source_t *ignore) const
{
	if (!nameTaken(base, ignore))
		return base;
	for (int i = 2;; ++i) {
		QString candidate = QStringLiteral("%1 %2").arg(base).arg(i);
		if (!nameTaken(candidate, ignore))
			return candidate;
	}
}

// ---- edits -------------------------------------------------------------------

obs_source_t *TransitionStore::createTransition(const char *typeId, const QString &name, obs_data_t *settings)
{
	if (!canEditList())
		return nullptr;

	const QString finalName = uniqueName(name.trimmed().isEmpty() ? QStringLiteral("Transition") : name.trimmed());
	const QByteArray utf8 = finalName.toUtf8();

	// Transitions are private sources, the same way OBS creates its own.
	OBSSourceAutoRelease source = obs_source_create_private(typeId, utf8.constData(), settings);
	if (!source || obs_obj_invalid(source)) {
		obs_log(LOG_WARNING, "failed to create transition of type '%s'", typeId);
		return nullptr;
	}

	hookTransition(source);

	// Append without selecting it, so the active transition doesn't change.
	QComboBox *box = combo();
	{
		QSignalBlocker block(box);
		box->addItem(finalName, QVariant::fromValue(OBSSource(source.Get())));
	}

	emit changed();
	return source.Get(); // the combo box keeps the reference
}

obs_source_t *TransitionStore::duplicateTransition(obs_source_t *source, const QString &name)
{
	if (!source)
		return nullptr;

	// Deep copy via JSON so nested objects aren't shared with the original.
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	OBSDataAutoRelease copy = obs_data_create_from_json(obs_data_get_json(settings));
	obs_source_t *dup = createTransition(obs_source_get_id(source), name, copy);

	const int ms = defaultDuration(source);
	if (dup && ms > 0)
		setDefaultDuration(dup, ms);
	return dup;
}

bool TransitionStore::renameTransition(obs_source_t *source, const QString &name)
{
	const QString trimmed = name.trimmed();
	if (!source || trimmed.isEmpty() || nameTaken(trimmed, source) || !canEditList())
		return false;

	const int idx = comboIndexOf(source);
	if (idx < 0)
		return false;

	const QString oldName = QString::fromUtf8(obs_source_get_name(source));
	if (oldName == trimmed)
		return true;

	obs_source_set_name(source, trimmed.toUtf8().constData());
	combo()->setItemText(idx, trimmed);

	if (durations_.contains(oldName))
		durations_.insert(trimmed, durations_.take(oldName));

	emit changed();
	return true;
}

bool TransitionStore::removeTransition(obs_source_t *source)
{
	if (!canRemove(source))
		return false;

	const int idx = comboIndexOf(source);
	if (idx < 0)
		return false;

	OBSSource keepAlive(source); // the combo releases its reference below
	durations_.remove(QString::fromUtf8(obs_source_get_name(source)));

	{
		// The active transition isn't this one, so OBS doesn't need to hear
		// about the index shift.
		QSignalBlocker block(combo());
		combo()->removeItem(idx);
	}

	hooked_.erase(std::remove_if(
			      hooked_.begin(), hooked_.end(),
			      [source](const OBSWeakSource &w) {
				      OBSSourceAutoRelease s = obs_weak_source_get_source(w);
				      if (s.Get() != source)
					      return false;
				      signal_handler_t *sh = obs_source_get_signal_handler(source);
				      signal_handler_disconnect(sh, "transition_video_stop", onTransitionStop, nullptr);
				      signal_handler_disconnect(sh, "transition_stop", onTransitionFullStop, nullptr);
				      return true;
			      }),
		      hooked_.end());

	emit changed();
	return true;
}

bool TransitionStore::moveTransition(int from, int to)
{
	if (!canEditList())
		return false;

	QComboBox *box = combo();
	if (from < 0 || from >= box->count() || to < 0 || to >= box->count() || from == to)
		return false;

	const OBSSource active = box->currentData().value<OBSSource>();
	{
		QSignalBlocker block(box);
		const QString text = box->itemText(from);
		const QVariant data = box->itemData(from);
		box->removeItem(from);
		box->insertItem(to, text, data);

		// Same transition stays selected; only its position changed.
		for (int i = 0; i < box->count(); ++i) {
			if (box->itemData(i).value<OBSSource>().Get() == active.Get()) {
				box->setCurrentIndex(i);
				break;
			}
		}
	}

	emit changed();
	return true;
}

void TransitionStore::setCurrent(obs_source_t *source)
{
	// Safe because every transition shown here is in OBS's own list.
	if (source && contains(source))
		obs_frontend_set_current_transition(source);
}

// ---- default durations ---------------------------------------------------------

int TransitionStore::defaultDuration(obs_source_t *source) const
{
	if (!source)
		return -1;
	return durations_.value(QString::fromUtf8(obs_source_get_name(source)), -1);
}

void TransitionStore::setDefaultDuration(obs_source_t *source, int ms)
{
	if (!source || ms <= 0)
		return;
	durations_.insert(QString::fromUtf8(obs_source_get_name(source)), ms);
	if (current().Get() == source && obs_frontend_get_transition_duration() != ms)
		obs_frontend_set_transition_duration(ms);
}

void TransitionStore::clearDefaultDuration(obs_source_t *source)
{
	if (source)
		durations_.remove(QString::fromUtf8(obs_source_get_name(source)));
}

void TransitionStore::applyDefaultDuration()
{
	OBSSource cur = current();
	if (!cur || obs_transition_fixed(cur))
		return;
	const int ms = defaultDuration(cur);
	if (ms > 0 && obs_frontend_get_transition_duration() != ms)
		obs_frontend_set_transition_duration(ms);
}

// ---- transition stop signals ---------------------------------------------------

// OBS connects these two signals on every transition it creates so the main
// window can finish scene switches; do the same for ones created here.
void TransitionStore::onTransitionStop(void *, calldata_t *)
{
	if (auto *main = static_cast<QObject *>(obs_frontend_get_main_window()))
		QMetaObject::invokeMethod(main, "TransitionStopped", Qt::QueuedConnection);
}

void TransitionStore::onTransitionFullStop(void *, calldata_t *)
{
	if (auto *main = static_cast<QObject *>(obs_frontend_get_main_window()))
		QMetaObject::invokeMethod(main, "TransitionFullyStopped", Qt::QueuedConnection);
}

void TransitionStore::hookTransition(obs_source_t *source)
{
	signal_handler_t *sh = obs_source_get_signal_handler(source);
	signal_handler_connect(sh, "transition_video_stop", onTransitionStop, nullptr);
	signal_handler_connect(sh, "transition_stop", onTransitionFullStop, nullptr);

	// Prune entries for transitions that no longer exist.
	hooked_.erase(std::remove_if(hooked_.begin(), hooked_.end(),
				     [](const OBSWeakSource &w) { return obs_weak_source_expired(w); }),
		      hooked_.end());
	OBSWeakSourceAutoRelease weak = obs_source_get_weak_source(source);
	hooked_.emplace_back(weak.Get());
}

void TransitionStore::unhookAll()
{
	// The callbacks live in this module, so they must be gone before it unloads.
	for (const OBSWeakSource &w : hooked_) {
		OBSSourceAutoRelease s = obs_weak_source_get_source(w);
		if (!s)
			continue;
		signal_handler_t *sh = obs_source_get_signal_handler(s);
		signal_handler_disconnect(sh, "transition_video_stop", onTransitionStop, nullptr);
		signal_handler_disconnect(sh, "transition_stop", onTransitionFullStop, nullptr);
	}
	hooked_.clear();
}

// ---- frontend callbacks --------------------------------------------------------

void TransitionStore::onFrontendEvent(enum obs_frontend_event event, void *data)
{
	auto *self = static_cast<TransitionStore *>(data);

	switch (event) {
	case OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		emit self->changed();
		emit self->scenesChanged();
		break;
	case OBS_FRONTEND_EVENT_TRANSITION_CHANGED:
		self->applyDefaultDuration();
		emit self->currentChanged();
		break;
	case OBS_FRONTEND_EVENT_TRANSITION_DURATION_CHANGED:
		emit self->durationChanged();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		emit self->scenesChanged();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		emit self->collectionUnloading();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		self->frontendExited_ = true;
		emit self->collectionUnloading();
		self->unhookAll();
		break;
	default:
		break;
	}
}

void TransitionStore::onSave(obs_data_t *saveData, bool saving, void *data)
{
	auto *self = static_cast<TransitionStore *>(data);

	if (saving) {
		OBSDataAutoRelease durations = obs_data_create();
		for (auto it = self->durations_.cbegin(); it != self->durations_.cend(); ++it)
			obs_data_set_int(durations, it.key().toUtf8().constData(), it.value());

		OBSDataAutoRelease root = obs_data_create();
		obs_data_set_obj(root, "durations", durations);
		obs_data_set_obj(saveData, kSaveKey, root);
		return;
	}

	self->durations_.clear();
	OBSDataAutoRelease root = obs_data_get_obj(saveData, kSaveKey);
	if (root) {
		OBSDataAutoRelease durations = obs_data_get_obj(root, "durations");
		obs_data_item_t *first = durations ? obs_data_first(durations) : nullptr;
		for (obs_data_item_t *item = first; item; obs_data_item_next(&item)) {
			const int ms = (int)obs_data_item_get_int(item);
			if (ms > 0)
				self->durations_.insert(QString::fromUtf8(obs_data_item_get_name(item)), ms);
		}
	}

	emit self->changed();
}
