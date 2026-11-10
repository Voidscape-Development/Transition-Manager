#include "transition-manager-window.hpp"

#include "properties-panel.hpp"
#include "transition-dialogs.hpp"
#include "transition-preview.hpp"
#include "transition-store.hpp"

#include <obs-frontend-api.h>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QShowEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

static constexpr int kSourceRole = Qt::UserRole + 1;

static obs_source_t *itemSource(const QListWidgetItem *item)
{
	return item ? reinterpret_cast<obs_source_t *>(item->data(kSourceRole).value<quintptr>()) : nullptr;
}

TransitionManagerWindow::TransitionManagerWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
	setWindowTitle(tr("Transition Manager"));
	resize(560, 860);

	// --- preview + playback controls
	preview_ = new TransitionPreview(this);

	playBtn_ = new QPushButton(tr("Play"));
	scrub_ = new QSlider(Qt::Horizontal);
	scrub_->setRange(0, 1000);
	scrub_->setToolTip(tr("Drag to scrub through the transition"));

	auto *playRow = new QHBoxLayout;
	playRow->addWidget(playBtn_);
	playRow->addWidget(scrub_, 1);

	previewMode_ = new QComboBox;
	previewMode_->addItem(tr("Test colors"));
	previewMode_->addItem(tr("Program → Preview scene"));
	previewMode_->setToolTip(tr("What the preview transitions between. Outside studio mode, the scene after the "
				    "program scene is used as the target."));

	duration_ = new QSpinBox;
	duration_->setRange(50, 20000);
	duration_->setSingleStep(50);
	duration_->setSuffix(QStringLiteral(" ms"));
	duration_->setValue(obs_frontend_get_transition_duration());

	ownDuration_ = new QCheckBox(tr("Own duration"));
	ownDuration_->setToolTip(tr("Remember this duration for the selected transition and apply it to OBS "
				    "whenever the transition becomes active."));

	auto *optionsRow = new QHBoxLayout;
	optionsRow->addWidget(new QLabel(tr("Preview:")));
	optionsRow->addWidget(previewMode_, 1);
	optionsRow->addWidget(duration_);
	optionsRow->addWidget(ownDuration_);

	// --- transition list + actions
	filter_ = new QLineEdit;
	filter_->setPlaceholderText(tr("Filter transitions"));
	filter_->setClearButtonEnabled(true);

	list_ = new QListWidget;
	list_->setSelectionMode(QAbstractItemView::SingleSelection);
	list_->setDragDropMode(QAbstractItemView::InternalMove);
	list_->setDefaultDropAction(Qt::MoveAction);
	list_->setContextMenuPolicy(Qt::CustomContextMenu);

	newBtn_ = new QToolButton;
	newBtn_->setText(tr("New"));
	newBtn_->setPopupMode(QToolButton::InstantPopup);
	auto *newMenu = new QMenu(newBtn_);
	connect(newMenu, &QMenu::aboutToShow, this, [this, newMenu] { populateNewMenu(newMenu); });
	newBtn_->setMenu(newMenu);

	dupBtn_ = new QPushButton(tr("Duplicate"));
	renameBtn_ = new QPushButton(tr("Rename"));
	removeBtn_ = new QPushButton(tr("Remove"));
	activeBtn_ = new QPushButton(tr("Set active"));

	auto *actions = new QHBoxLayout;
	actions->addWidget(newBtn_);
	actions->addWidget(dupBtn_);
	actions->addWidget(renameBtn_);
	actions->addWidget(removeBtn_);
	actions->addStretch();
	actions->addWidget(activeBtn_);

	readOnlyNote_ = new QLabel(tr("OBS's transition list couldn't be accessed, so adding, removing, renaming and "
				      "reordering are disabled. Settings, preview and switching still work."));
	readOnlyNote_->setWordWrap(true);
	readOnlyNote_->setVisible(false);

	auto *listPane = new QWidget;
	auto *listLayout = new QVBoxLayout(listPane);
	listLayout->setContentsMargins(0, 0, 0, 0);
	listLayout->addWidget(filter_);
	listLayout->addWidget(list_, 1);
	listLayout->addWidget(readOnlyNote_);
	listLayout->addLayout(actions);

	// --- settings
	props_ = new PropertiesPanel;

	auto *split = new QSplitter(Qt::Vertical);
	split->addWidget(listPane);
	split->addWidget(props_);
	split->setStretchFactor(0, 1);
	split->setStretchFactor(1, 2);

	auto *root = new QVBoxLayout(this);
	root->addWidget(preview_);
	root->addLayout(playRow);
	root->addLayout(optionsRow);
	root->addWidget(split, 1);

	// --- wiring
	auto &store = TransitionStore::instance();
	connect(&store, &TransitionStore::changed, this, &TransitionManagerWindow::refreshList);
	connect(&store, &TransitionStore::currentChanged, this, &TransitionManagerWindow::refreshList);
	connect(&store, &TransitionStore::durationChanged, this, &TransitionManagerWindow::onGlobalDurationChanged);
	connect(&store, &TransitionStore::scenesChanged, this, [this] {
		if (preview_->sourceMode() == TransitionPreview::SourceMode::Scenes)
			preview_->refreshSources();
	});
	connect(&store, &TransitionStore::collectionUnloading, this, [this] {
		// Let go of everything that belongs to the outgoing collection.
		preview_->releaseScenes();
		preview_->setTransition(nullptr);
		props_->setSource(nullptr);
		shown_ = nullptr;
		shownName_.clear();
		QSignalBlocker block(list_);
		list_->clear();
		updateButtons();
	});

	connect(filter_, &QLineEdit::textChanged, this, &TransitionManagerWindow::applyFilter);
	connect(list_, &QListWidget::itemSelectionChanged, this, &TransitionManagerWindow::onSelectionChanged);
	connect(list_, &QListWidget::itemDoubleClicked, this, &TransitionManagerWindow::onSetActive);
	connect(list_, &QListWidget::customContextMenuRequested, this, &TransitionManagerWindow::showContextMenu);
	connect(list_->model(), &QAbstractItemModel::rowsMoved, this, &TransitionManagerWindow::onRowsMoved);
	connect(list_->model(), &QAbstractItemModel::rowsInserted, this, &TransitionManagerWindow::onRowsMoved);

	connect(dupBtn_, &QPushButton::clicked, this, &TransitionManagerWindow::onDuplicate);
	connect(renameBtn_, &QPushButton::clicked, this, &TransitionManagerWindow::onRename);
	connect(removeBtn_, &QPushButton::clicked, this, &TransitionManagerWindow::onRemove);
	connect(activeBtn_, &QPushButton::clicked, this, &TransitionManagerWindow::onSetActive);

	connect(props_, &PropertiesPanel::settingsChanged, this, [this] { preview_->syncSettings(props_->source()); });

	connect(previewMode_, &QComboBox::currentIndexChanged, this, [this](int i) {
		preview_->setSourceMode(i == 1 ? TransitionPreview::SourceMode::Scenes
					       : TransitionPreview::SourceMode::Colors);
		scrub_->setValue(0);
	});
	connect(duration_, &QSpinBox::valueChanged, this, &TransitionManagerWindow::onDurationChanged);
	connect(ownDuration_, &QCheckBox::toggled, this, &TransitionManagerWindow::onOwnDurationToggled);

	connect(playBtn_, &QPushButton::clicked, this, [this] {
		scrub_->setValue(0);
		preview_->play(duration_->value());
	});
	connect(scrub_, &QSlider::sliderPressed, this, [this] {
		preview_->beginScrub(duration_->value());
		preview_->scrub(scrub_->value() / 1000.0f);
	});
	connect(scrub_, &QSlider::valueChanged, this, [this](int v) {
		if (scrub_->isSliderDown())
			preview_->scrub(v / 1000.0f);
	});

	refreshList();
}

// ---- window lifecycle ------------------------------------------------------------

void TransitionManagerWindow::open(obs_source_t *source)
{
	if (isMinimized())
		showNormal();
	else
		show();
	raise();
	activateWindow();

	if (source)
		selectSource(source);
}

void TransitionManagerWindow::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	refreshList(); // recreates the preview copy for the selection
}

void TransitionManagerWindow::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	if (event->spontaneous())
		return; // minimized: keep state

	// Closed: stop rendering the preview copy and drop its scene references.
	preview_->setTransition(nullptr);
	shown_ = nullptr;
	shownName_.clear();
}

// ---- list ----------------------------------------------------------------------

obs_source_t *TransitionManagerWindow::selected() const
{
	QListWidgetItem *item = list_->currentItem();
	if (!item || !item->isSelected())
		return nullptr;
	obs_source_t *source = itemSource(item);
	// Validate: the item may be stale if the list changed underneath us.
	return TransitionStore::instance().contains(source) ? source : nullptr;
}

void TransitionManagerWindow::refreshList()
{
	if (reordering_)
		return;

	auto &store = TransitionStore::instance();
	obs_source_t *keep = selected();
	OBSSource current = store.current();

	{
		refreshing_ = true;
		QSignalBlocker block(list_);
		list_->clear();

		for (const OBSSource &source : store.transitions()) {
			const QString name = QString::fromUtf8(obs_source_get_name(source));
			auto *item = new QListWidgetItem(name, list_);
			item->setData(kSourceRole,
				      QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(source.Get())));

			QString tip = QString::fromUtf8(obs_source_get_display_name(obs_source_get_id(source)));
			const int ms = store.defaultDuration(source);
			if (ms > 0)
				tip += tr(" • own duration %1 ms").arg(ms);
			item->setToolTip(tip);

			if (source.Get() == current.Get()) {
				QFont f = item->font();
				f.setBold(true);
				item->setFont(f);
				item->setText(name + tr("  (active)"));
			}
			if (source.Get() == keep)
				list_->setCurrentItem(item);
		}
		refreshing_ = false;
	}

	const bool editable = store.canEditList();
	readOnlyNote_->setVisible(!editable);
	applyFilter();

	// A rename changes the settings panel's header.
	if (shown_ && shown_ == selected() && shownName_ != QString::fromUtf8(obs_source_get_name(shown_))) {
		shownName_ = QString::fromUtf8(obs_source_get_name(shown_));
		props_->refresh();
	}

	onSelectionChanged();
}

void TransitionManagerWindow::applyFilter()
{
	const QString text = filter_->text().trimmed();
	for (int i = 0; i < list_->count(); ++i) {
		QListWidgetItem *item = list_->item(i);
		const bool match = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive) ||
				   item->toolTip().contains(text, Qt::CaseInsensitive);
		item->setHidden(!match);
	}

	// Reordering a filtered list would be ambiguous.
	const bool canDrag = text.isEmpty() && TransitionStore::instance().canEditList();
	list_->setDragEnabled(canDrag);
	list_->setDragDropMode(canDrag ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
}

void TransitionManagerWindow::onRowsMoved()
{
	if (refreshing_ || reordering_)
		return;

	// Let the drop finish before touching the list, then push the new order
	// into OBS's list one move at a time.
	QTimer::singleShot(0, this, [this] {
		if (refreshing_ || reordering_)
			return;

		std::vector<obs_source_t *> desired;
		for (int i = 0; i < list_->count(); ++i)
			desired.push_back(itemSource(list_->item(i)));

		auto &store = TransitionStore::instance();
		reordering_ = true;
		for (size_t i = 0; i < desired.size(); ++i) {
			const auto current = store.transitions();
			for (size_t j = 0; j < current.size(); ++j) {
				if (current[j].Get() == desired[i] && j != i) {
					store.moveTransition((int)j, (int)i);
					break;
				}
			}
		}
		reordering_ = false;
		refreshList();
	});
}

void TransitionManagerWindow::selectSource(obs_source_t *source)
{
	for (int i = 0; i < list_->count(); ++i) {
		QListWidgetItem *item = list_->item(i);
		if (itemSource(item) == source) {
			if (item->isHidden())
				filter_->clear();
			list_->setCurrentItem(item);
			list_->scrollToItem(item);
			return;
		}
	}
}

void TransitionManagerWindow::onSelectionChanged()
{
	// Nothing to render or edit while the window is closed; showEvent
	// catches up when it opens again.
	if (!isVisible())
		return;

	obs_source_t *source = selected();
	if (source != shown_) {
		shown_ = source;
		shownName_ = source ? QString::fromUtf8(obs_source_get_name(source)) : QString();
		// Preview first: building the panel can write fallback values back
		// (see PropertiesPanel), which are mirrored into the preview copy.
		preview_->setTransition(source);
		props_->setSource(source);
		scrub_->setValue(0);
	}
	updateDurationControls();
	updateButtons();
}

void TransitionManagerWindow::updateButtons()
{
	auto &store = TransitionStore::instance();
	obs_source_t *source = selected();
	const bool isCurrent = source && store.current().Get() == source;
	const bool editable = store.canEditList();

	newBtn_->setEnabled(editable);
	dupBtn_->setEnabled(source && editable);
	renameBtn_->setEnabled(source && editable);
	removeBtn_->setEnabled(store.canRemove(source));
	activeBtn_->setEnabled(source && !isCurrent);
	playBtn_->setEnabled(source);
	scrub_->setEnabled(source);

	if (source && !obs_source_configurable(source))
		removeBtn_->setToolTip(tr("Built-in transitions can't be removed"));
	else if (isCurrent)
		removeBtn_->setToolTip(tr("Switch to another transition before removing this one"));
	else
		removeBtn_->setToolTip(QString());
}

void TransitionManagerWindow::showContextMenu(const QPoint &pos)
{
	QListWidgetItem *item = list_->itemAt(pos);
	if (!item)
		return;
	list_->setCurrentItem(item);

	QMenu menu(this);
	menu.addAction(tr("Set active"), this, &TransitionManagerWindow::onSetActive)
		->setEnabled(activeBtn_->isEnabled());
	menu.addSeparator();
	menu.addAction(tr("Duplicate"), this, &TransitionManagerWindow::onDuplicate)->setEnabled(dupBtn_->isEnabled());
	menu.addAction(tr("Rename"), this, &TransitionManagerWindow::onRename)->setEnabled(renameBtn_->isEnabled());
	menu.addAction(tr("Remove"), this, &TransitionManagerWindow::onRemove)->setEnabled(removeBtn_->isEnabled());
	menu.exec(list_->viewport()->mapToGlobal(pos));
}

// ---- durations -------------------------------------------------------------------

void TransitionManagerWindow::updateDurationControls()
{
	auto &store = TransitionStore::instance();
	obs_source_t *source = selected();
	const bool fixed = source && obs_transition_fixed(source);
	const int own = store.defaultDuration(source);

	QSignalBlocker blockSpin(duration_);
	QSignalBlocker blockCheck(ownDuration_);
	ownDuration_->setChecked(own > 0);
	duration_->setValue(own > 0 ? own : obs_frontend_get_transition_duration());

	duration_->setEnabled(!fixed);
	ownDuration_->setEnabled(source && !fixed);
	duration_->setToolTip(fixed ? tr("This transition has a fixed length") : QString());
}

void TransitionManagerWindow::onOwnDurationToggled(bool on)
{
	auto &store = TransitionStore::instance();
	obs_source_t *source = selected();
	if (!source)
		return;

	if (on) {
		store.setDefaultDuration(source, duration_->value());
	} else {
		store.clearDefaultDuration(source);
		QSignalBlocker block(duration_);
		duration_->setValue(obs_frontend_get_transition_duration());
	}
	refreshList(); // update tooltips
}

void TransitionManagerWindow::onDurationChanged(int ms)
{
	// Without "Own duration", the box only sets the preview length.
	if (ownDuration_->isChecked())
		TransitionStore::instance().setDefaultDuration(selected(), ms);
}

void TransitionManagerWindow::onGlobalDurationChanged()
{
	if (ownDuration_->isChecked())
		return;
	QSignalBlocker block(duration_);
	duration_->setValue(obs_frontend_get_transition_duration());
}

// ---- actions -------------------------------------------------------------------

void TransitionManagerWindow::populateNewMenu(QMenu *menu)
{
	menu->clear();
	const char *id = nullptr;
	// Same list as OBS's own "+" button: unconfigurable types (Cut, Fade)
	// already exist once and a second copy would be identical.
	for (size_t i = 0; obs_enum_transition_types(i, &id); ++i) {
		const uint32_t flags = obs_get_source_output_flags(id);
		if (flags & (OBS_SOURCE_DEPRECATED | OBS_SOURCE_CAP_DISABLED))
			continue;
		if (!obs_is_source_configurable(id))
			continue;
		const char *display = obs_source_get_display_name(id);
		const QString typeId = QString::fromUtf8(id);
		menu->addAction(QString::fromUtf8(display ? display : id), this, [this, typeId] { onCreate(typeId); });
	}
	if (menu->isEmpty())
		menu->addAction(tr("No configurable transition types available"))->setEnabled(false);
}

void TransitionManagerWindow::onCreate(const QString &typeId)
{
	const QByteArray id = typeId.toUtf8();
	const char *display = obs_source_get_display_name(id.constData());
	obs_source_t *source = TransitionStore::instance().createTransition(
		id.constData(), QString::fromUtf8(display ? display : id.constData()));
	if (source)
		selectSource(source);
	else
		QMessageBox::warning(this, tr("New transition"), tr("The transition couldn't be created."));
}

void TransitionManagerWindow::onDuplicate()
{
	obs_source_t *source = selected();
	if (!source)
		return;
	const QString base = QString::fromUtf8(obs_source_get_name(source)) + tr(" (copy)");
	obs_source_t *copy = TransitionStore::instance().duplicateTransition(source, base);
	if (copy)
		selectSource(copy);
}

void TransitionManagerWindow::onRename()
{
	TransitionDialogs::rename(this, selected());
}

void TransitionManagerWindow::onRemove()
{
	TransitionDialogs::remove(this, selected(), false);
}

void TransitionManagerWindow::onSetActive()
{
	if (obs_source_t *source = selected())
		TransitionStore::instance().setCurrent(source);
}
