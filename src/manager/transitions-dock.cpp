#include "transitions-dock.hpp"
#include "manager-hooks.hpp"

#include "transition-dialogs.hpp"
#include "transition-manager-window.hpp"
#include "transition-store.hpp"

#include <obs-frontend-api.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

// Flat icon button styled by the active OBS theme. OBS 30 themes match on
// `themeID`; OBS 31+ themes match on `class`. Setting both covers either.
static QPushButton *makeToolButton(const QString &tooltip, const char *themeId, const char *themeClass,
				   const char *icon)
{
	auto *button = new QPushButton;
	button->setToolTip(tooltip);
	button->setAccessibleName(tooltip);
	button->setFlat(true);
	button->setIcon(QIcon(QString::fromLatin1(icon)));
	button->setProperty("themeID", QString::fromLatin1(themeId));
	button->setProperty("class", QString::fromLatin1(themeClass));
	button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	return button;
}

TransitionsDock::TransitionsDock(TransitionManagerWindow *window, QWidget *parent) : QWidget(parent), window_(window)
{
	combo_ = new QComboBox;
	combo_->setAccessibleName(tr("Transition"));
	combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	durationLabel_ = new QLabel(tr("Duration"));
	duration_ = new QSpinBox;
	duration_->setAccessibleName(tr("Transition duration"));
	duration_->setRange(50, 20000);
	duration_->setSingleStep(50);
	duration_->setSuffix(QStringLiteral(" ms"));
	duration_->setValue(obs_frontend_get_transition_duration());

	addBtn_ = makeToolButton(tr("Add transition"), "addIconSmall", "btn-tool icon-plus", ":/res/images/plus.svg");
	removeBtn_ = makeToolButton(tr("Remove transition"), "removeIconSmall", "btn-tool icon-trash",
				    ":/res/images/minus.svg");
	propsBtn_ = makeToolButton(tr("Transition properties"), "menuIconSmall", "btn-tool icon-dots-vert",
				   ":/settings/images/settings/general.svg");

	managerBtn_ = new QPushButton(tr("Open Transition Manager"));
	managerBtn_->setToolTip(tr("Preview, edit, duplicate and organize transitions"));

	auto *durationRow = new QHBoxLayout;
	durationRow->addWidget(durationLabel_);
	durationRow->addWidget(duration_, 1);

	auto *buttonRow = new QHBoxLayout;
	buttonRow->setSpacing(2);
	buttonRow->addWidget(addBtn_);
	buttonRow->addWidget(removeBtn_);
	buttonRow->addWidget(propsBtn_);
	buttonRow->addStretch();

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->addWidget(combo_);
	root->addLayout(durationRow);
	root->addLayout(buttonRow);
	root->addWidget(managerBtn_);
	root->addStretch();

	// --- wiring
	auto &store = TransitionStore::instance();
	connect(&store, &TransitionStore::changed, this, &TransitionsDock::refresh);
	connect(&store, &TransitionStore::currentChanged, this, &TransitionsDock::refresh);
	connect(&store, &TransitionStore::durationChanged, this, &TransitionsDock::syncDuration);
	connect(&store, &TransitionStore::collectionUnloading, this, [this] {
		QSignalBlocker block(combo_);
		combo_->clear();
		updateControls();
	});

	connect(combo_, &QComboBox::currentIndexChanged, this, &TransitionsDock::onComboChanged);
	connect(duration_, &QSpinBox::valueChanged, this, &TransitionsDock::onDurationChanged);

	connect(addBtn_, &QPushButton::clicked, this, [this] {
		QMenu menu(this);
		populateAddMenu(&menu);
		menu.exec(addBtn_->mapToGlobal(QPoint(0, addBtn_->height())));
	});
	connect(removeBtn_, &QPushButton::clicked, this, &TransitionsDock::onRemove);
	connect(propsBtn_, &QPushButton::clicked, this, &TransitionsDock::showPropsMenu);
	connect(managerBtn_, &QPushButton::clicked, this, [this] { window_->open(active()); });

	refresh();
}

obs_source_t *TransitionsDock::active() const
{
	OBSSource cur = TransitionStore::instance().current();
	return TransitionStore::instance().contains(cur) ? cur.Get() : nullptr;
}

void TransitionsDock::refresh()
{
	auto &store = TransitionStore::instance();
	OBSSource current = store.current();

	{
		QSignalBlocker block(combo_);
		combo_->clear();
		int currentIndex = -1;
		for (const OBSSource &source : store.transitions()) {
			combo_->addItem(QString::fromUtf8(obs_source_get_name(source)),
					QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(source.Get())));
			if (source.Get() == current.Get())
				currentIndex = combo_->count() - 1;
		}
		combo_->setCurrentIndex(currentIndex);
	}

	syncDuration();
	updateControls();
}

void TransitionsDock::syncDuration()
{
	QSignalBlocker block(duration_);
	duration_->setValue(obs_frontend_get_transition_duration());
}

void TransitionsDock::updateControls()
{
	auto &store = TransitionStore::instance();
	obs_source_t *source = active();
	const bool editable = store.canEditList();

	// Like OBS: fixed-length transitions (Cut, Stinger, ...) hide the duration.
	const bool fixed = source && obs_transition_fixed(source);
	durationLabel_->setVisible(!fixed);
	duration_->setVisible(!fixed);

	addBtn_->setEnabled(editable);
	removeBtn_->setEnabled(store.canRemove(source, true));
	propsBtn_->setEnabled(source != nullptr);
}

void TransitionsDock::onComboChanged(int index)
{
	if (index < 0)
		return;
	auto *source = reinterpret_cast<obs_source_t *>(combo_->itemData(index).value<quintptr>());
	TransitionStore::instance().setCurrent(source);
	// OBS reports the switch through TRANSITION_CHANGED, which refreshes us.
}

void TransitionsDock::onDurationChanged(int ms)
{
	auto &store = TransitionStore::instance();
	obs_frontend_set_transition_duration(ms);

	// Keep a transition's "own duration" in step if it has one, otherwise
	// the old value would come back the next time it's activated.
	obs_source_t *source = active();
	if (source && store.defaultDuration(source) > 0)
		store.setDefaultDuration(source, ms);
}

void TransitionsDock::populateAddMenu(QMenu *menu)
{
	// Same list as OBS's own "+" button: unconfigurable types (Cut, Fade)
	// already exist once and a second copy would be identical.
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_transition_types(i, &id); ++i) {
		const uint32_t flags = obs_get_source_output_flags(id);
		if (flags & (OBS_SOURCE_DEPRECATED | OBS_SOURCE_CAP_DISABLED))
			continue;
		if (!obs_is_source_configurable(id))
			continue;
		const char *display = obs_source_get_display_name(id);
		const QString typeId = QString::fromUtf8(id);
		menu->addAction(QString::fromUtf8(display ? display : id), this,
				[this, typeId] { addTransition(typeId); });
	}
	if (menu->isEmpty())
		menu->addAction(tr("No configurable transition types available"))->setEnabled(false);
}

void TransitionsDock::addTransition(const QString &typeId)
{
	auto &store = TransitionStore::instance();
	const QByteArray id = typeId.toUtf8();
	const char *display = obs_source_get_display_name(id.constData());

	// Like OBS: ask for a name, make the new transition active, then open
	// its settings (here, in the Transition Manager window).
	QString name;
	if (!store.obsPrompts()) { // otherwise OBS asks for the name itself
		const QString suggested = store.uniqueName(QString::fromUtf8(display ? display : id.constData()));
		name = TransitionDialogs::askName(this, tr("Add transition"), suggested);
		if (name.isEmpty())
			return;
	}

	obs_source_t *source = store.createTransition(id.constData(), name);
	if (!source) {
		if (!store.obsPrompts()) // with OBS's dialog, nullptr usually means cancelled
			QMessageBox::warning(this, tr("Add transition"), tr("The transition couldn't be created."));
		return;
	}
	store.setCurrent(source);
	window_->open(source);
}

void TransitionsDock::onRemove()
{
	TransitionDialogs::remove(this, active(), true);
}

void TransitionsDock::showPropsMenu()
{
	obs_source_t *source = active();
	if (!source)
		return;

	auto &store = TransitionStore::instance();
	const bool editable = store.canEditList();
	OBSSource keep(source);

	QMenu menu(this);
	menu.addAction(tr("Rename"), this, [this, keep] {
		    TransitionDialogs::rename(this, keep);
	    })->setEnabled(editable);
	menu.addAction(tr("Duplicate"), this, [this, keep] {
		    auto &store = TransitionStore::instance();
		    if (store.obsPrompts()) { // OBS asks for the name itself
			    store.duplicateTransition(keep, QString());
			    return;
		    }
		    const QString name = TransitionDialogs::askName(
			    this, tr("Duplicate transition"),
			    store.uniqueName(QString::fromUtf8(obs_source_get_name(keep)) + tr(" (copy)")));
		    if (!name.isEmpty())
			    store.duplicateTransition(keep, name);
	    })->setEnabled(editable);
	menu.addAction(tr("Properties"), this, [this, keep] {
		    window_->open(keep);
	    })->setEnabled(obs_source_configurable(source));
	menu.addSeparator();
	menu.addAction(tr("Open Transition Manager"), this, [this, keep] { window_->open(keep); });
	if (ManagerHooks::hasRules())
		menu.addAction(ManagerHooks::get().openRulesLabel, this, [keep] { ManagerHooks::openRules(keep); });

	menu.exec(propsBtn_->mapToGlobal(QPoint(0, propsBtn_->height())));
}
