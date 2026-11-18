#include "transition-store.hpp"
#include "obs-qt-types.hpp"

#include <plugin-support.h>

#include <QComboBox>
#include <QDialog>
#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>
#include <QSignalBlocker>
#include <algorithm>
#include <climits>
#include <cstring>
#include <utility>

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

QObject *TransitionStore::mainWindow() const
{
	return static_cast<QObject *>(obs_frontend_get_main_window());
}

bool TransitionStore::hasMainSlot(const char *signature) const
{
	QObject *main = mainWindow();
	if (!main)
		return false;
	const QByteArray normalized = QMetaObject::normalizedSignature(signature);
	return main->metaObject()->indexOfMethod(normalized.constData()) >= 0;
}

static bool holdsSource(const QVariant &v)
{
	return v.metaType().id() == qMetaTypeId<OBSSource>();
}

static bool holdsUuid(const QVariant &v)
{
	return v.metaType().id() == QMetaType::QString;
}

static QString uuidOf(obs_source_t *source)
{
	return QString::fromUtf8(obs_source_get_uuid(source));
}

TransitionStore::ListMode TransitionStore::listMode() const
{
	QComboBox *box = combo();
	if (!box || box->count() == 0)
		return ListMode::Unavailable;

	// OBS 31 and older store the transition itself in each item; OBS 32
	// switched to storing its UUID and keeping the real list elsewhere.
	const QVariant first = box->itemData(0);
	if (holdsSource(first))
		return ListMode::Legacy;
	if (holdsUuid(first))
		return ListMode::Uuid;

	if (!warnedUnsupported_) {
		obs_log(LOG_WARNING,
			"OBS's transition list holds data of an unknown type (%s); adding, removing, "
			"renaming and reordering transitions is disabled",
			first.typeName() ? first.typeName() : "none");
		warnedUnsupported_ = true;
	}
	return ListMode::Unavailable;
}

bool TransitionStore::canEditList() const
{
	QComboBox *box = combo();
	if (!box)
		return false;

	const ListMode mode = listMode();
	const auto list = transitions();
	if ((size_t)box->count() != list.size())
		return false;

	if (mode == ListMode::Legacy) {
		// Make sure the widget really is the list the frontend API reports,
		// in the same order, before writing to it.
		for (int i = 0; i < box->count(); ++i) {
			if (box->itemData(i).value<OBSSource>().Get() != list[(size_t)i].Get())
				return false;
		}
		return true;
	}

	if (mode == ListMode::Uuid) {
		// Edits go through these main-window slots; a future OBS may rename them.
		if (!hasMainSlot("AddTransition(const char*)") || !hasMainSlot("RenameTransition(OBSSource)") ||
		    !hasMainSlot("on_transitionRemove_clicked()")) {
			if (!warnedUnsupported_) {
				obs_log(LOG_WARNING, "OBS's transition slots weren't found; adding, removing and "
						     "renaming transitions is disabled");
				warnedUnsupported_ = true;
			}
			return false;
		}
		// The dropdown must show exactly the transitions OBS reports.
		for (const OBSSource &s : list) {
			if (box->findData(uuidOf(s)) < 0)
				return false;
		}
		return true;
	}

	return false;
}

bool TransitionStore::canReorder() const
{
	// OBS 32+ saves transitions from an unordered map, so an order set here
	// wouldn't survive a restart.
	return listMode() == ListMode::Legacy && canEditList();
}

bool TransitionStore::obsPrompts() const
{
	return listMode() == ListMode::Uuid;
}

int TransitionStore::comboIndexOf(obs_source_t *source) const
{
	QComboBox *box = combo();
	if (!box || !source)
		return -1;
	if (listMode() == ListMode::Uuid)
		return box->findData(uuidOf(source));
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

	// OBS 32+ reports transitions in hash-map order. Use the order of OBS's
	// dropdown instead, so both lists match.
	if (listMode() == ListMode::Uuid) {
		QComboBox *box = combo();
		std::vector<std::pair<int, OBSSource>> ranked;
		ranked.reserve(out.size());
		for (OBSSource &s : out) {
			const int idx = box->findData(uuidOf(s));
			ranked.emplace_back(idx < 0 ? INT_MAX : idx, std::move(s));
		}
		std::stable_sort(ranked.begin(), ranked.end(),
				 [](const auto &a, const auto &b) { return a.first < b.first; });
		out.clear();
		for (auto &entry : ranked)
			out.push_back(std::move(entry.second));
	}
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

bool TransitionStore::canRemove(obs_source_t *source, bool allowActive) const
{
	// Same rules as OBS's own remove button: Cut, Fade and other
	// unconfigurable transitions are built in. Unless allowActive, never pull
	// the active transition out from under the program output.
	if (!source || !canEditList() || !obs_source_configurable(source))
		return false;
	if (current().Get() != source)
		return true;
	return allowActive && transitions().size() > 1;
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
	if (listMode() == ListMode::Uuid)
		return createViaObs(typeId, settings);

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
	if (!source || !canEditList())
		return false;
	if (listMode() == ListMode::Uuid)
		return renameViaObs(source);

	const QString trimmed = name.trimmed();
	if (trimmed.isEmpty() || nameTaken(trimmed, source))
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

bool TransitionStore::removeTransition(obs_source_t *source, bool allowActive)
{
	if (!canRemove(source, allowActive))
		return false;
	if (listMode() == ListMode::Uuid)
		return removeViaObs(source, allowActive);

	if (current().Get() == source) {
		// Switch away first so the output never loses its transition.
		setCurrent(fallbackFor(source));
		if (current().Get() == source)
			return false;
	}

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
	if (!canReorder())
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

OBSSource TransitionStore::fallbackFor(obs_source_t *source) const
{
	// Fade if there is one, otherwise the first other transition.
	OBSSource fallback;
	for (const OBSSource &s : transitions()) {
		if (s.Get() == source)
			continue;
		if (strcmp(obs_source_get_id(s), "fade_transition") == 0)
			return s;
		if (!fallback)
			fallback = s;
	}
	return fallback;
}

void TransitionStore::restoreCurrent(obs_source_t *previous, int previousDuration)
{
	if (previous && current().Get() != previous)
		setCurrent(previous);
	// Activating a transition with its own default duration changes OBS's
	// duration box; put back what the user had.
	if (previousDuration > 0 && obs_frontend_get_transition_duration() != previousDuration)
		obs_frontend_set_transition_duration(previousDuration);
}

// ---- edits through OBS's own slots (OBS 32+) -------------------------------------

// OBS's properties windows that are open right now.
static std::vector<QPointer<QDialog>> propertiesWindows(QObject *main)
{
	std::vector<QPointer<QDialog>> out;
	for (QDialog *dialog : main->findChildren<QDialog *>()) {
		if (dialog->inherits("OBSBasicProperties"))
			out.emplace_back(dialog);
	}
	return out;
}

obs_source_t *TransitionStore::createViaObs(const char *typeId, obs_data_t *settings)
{
	QObject *main = mainWindow();
	if (!main || !typeId)
		return nullptr;

	const OBSSource previous = current();
	const int previousDuration = obs_frontend_get_transition_duration();
	const auto before = transitions();
	const auto windowsBefore = propertiesWindows(main);

	// The same as picking a type from OBS's "+" menu: OBS asks for a name,
	// adds the transition to its list, makes it active and opens its
	// properties window. A cancelled name dialog adds nothing.
	if (!QMetaObject::invokeMethod(main, "AddTransition", Qt::DirectConnection, Q_ARG(const char *, typeId))) {
		obs_log(LOG_WARNING, "couldn't ask OBS to add a transition of type '%s'", typeId);
		return nullptr;
	}

	OBSSource created;
	for (const OBSSource &s : transitions()) {
		const bool existed = std::any_of(before.begin(), before.end(),
						 [&s](const OBSSource &b) { return b.Get() == s.Get(); });
		if (!existed) {
			created = s;
			break;
		}
	}
	if (!created)
		return nullptr;

	// The plugin shows the new transition's settings itself. The window was
	// just opened and nothing in it changed, so closing it discards nothing.
	for (QDialog *dialog : propertiesWindows(main)) {
		const bool existed = std::any_of(windowsBefore.begin(), windowsBefore.end(),
						 [dialog](const QPointer<QDialog> &w) { return w == dialog; });
		if (!existed)
			dialog->close();
	}

	if (settings)
		obs_source_update(created, settings);

	restoreCurrent(previous, previousDuration);
	emit changed();
	return created.Get(); // OBS's list keeps the reference
}

bool TransitionStore::renameViaObs(obs_source_t *source)
{
	QObject *main = mainWindow();
	if (!main)
		return false;

	OBSSource keep(source);
	const QString oldName = QString::fromUtf8(obs_source_get_name(source));

	// OBS asks for the new name, checks it's unique and updates its lookups.
	if (!QMetaObject::invokeMethod(main, "RenameTransition", Qt::DirectConnection, Q_ARG(OBSSource, keep))) {
		obs_log(LOG_WARNING, "couldn't ask OBS to rename transition '%s'", obs_source_get_name(source));
		return false;
	}

	const QString newName = QString::fromUtf8(obs_source_get_name(source));
	if (newName == oldName)
		return false; // cancelled

	if (durations_.contains(oldName))
		durations_.insert(newName, durations_.take(oldName));

	emit changed();
	return true;
}

bool TransitionStore::removeViaObs(obs_source_t *source, bool allowActive)
{
	QObject *main = mainWindow();
	if (!main)
		return false;

	OBSSource keep(source); // OBS releases its reference when it removes it
	const QString name = QString::fromUtf8(obs_source_get_name(source));
	const OBSSource previous = current();
	const int previousDuration = obs_frontend_get_transition_duration();
	const bool wasActive = previous.Get() == source;
	if (wasActive && !allowActive)
		return false;

	// OBS's remove button removes the active transition, so select this one
	// first. That fails while a transition is running.
	if (!wasActive) {
		setCurrent(source);
		if (current().Get() != source) {
			restoreCurrent(previous, previousDuration);
			return false;
		}
	}

	// OBS confirms, removes it (with any quick transitions using it) and
	// switches to another transition.
	if (!QMetaObject::invokeMethod(main, "on_transitionRemove_clicked", Qt::DirectConnection))
		obs_log(LOG_WARNING, "couldn't ask OBS to remove transition '%s'", obs_source_get_name(source));

	const bool removed = !contains(source);
	if (removed)
		durations_.remove(name);

	if (!wasActive)
		restoreCurrent(previous, previousDuration);
	else if (removed)
		setCurrent(fallbackFor(source)); // prefer Fade, like the legacy path

	if (removed)
		emit changed();
	return removed;
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
