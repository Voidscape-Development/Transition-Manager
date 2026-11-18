#include "transition-manager-window.hpp"

#include "manager-hooks.hpp"
#include "media-probe.hpp"
#include "properties-panel.hpp"
#include "timeline-slider.hpp"
#include "transition-dialogs.hpp"
#include "transition-list.hpp"
#include "transition-preview.hpp"
#include "transition-store.hpp"

#include <obs-frontend-api.h>

#include <QAction>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QEnterEvent>
#include <QEvent>
#include <QPainter>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>

using namespace TransitionList;

static constexpr int kTickMs = 16;
static constexpr int kLoopGapMs = 600;

static obs_source_t *itemSource(const QListWidgetItem *item)
{
	return item ? reinterpret_cast<obs_source_t *>(item->data(SourceRole).value<quintptr>()) : nullptr;
}

static bool isStinger(obs_source_t *source)
{
	return source && strcmp(obs_source_get_id(source), "obs_stinger_transition") == 0;
}

static QLabel *roleLabel(const QString &text, const char *role)
{
	auto *label = new QLabel(text);
	label->setProperty("tmRole", role);
	return label;
}

static QPushButton *roleButton(const QString &text, const char *role)
{
	auto *button = new QPushButton(text);
	button->setProperty("tmRole", role);
	button->setCursor(Qt::PointingHandCursor);
	return button;
}

// Round accent "play" button, painted directly so OBS themes can't reshape it.
class RoundButton : public QAbstractButton {
public:
	explicit RoundButton(QWidget *parent = nullptr) : QAbstractButton(parent)
	{
		setFixedSize(36, 36);
		setCursor(Qt::PointingHandCursor);
		setFocusPolicy(Qt::StrongFocus);
	}
	void setColors(const QColor &bg, const QColor &hover)
	{
		bg_ = bg;
		hover_ = hover;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(!isEnabled() ? palette().color(QPalette::Mid) : underMouse() ? hover_ : bg_);
		p.drawEllipse(rect().adjusted(1, 1, -1, -1));
		if (hasFocus()) {
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(palette().color(QPalette::Highlight).lighter(150), 2));
			p.drawEllipse(rect().adjusted(1, 1, -1, -1));
		}
		const QSize s = iconSize();
		icon().paint(&p, QRect((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height()));
	}
	void enterEvent(QEnterEvent *) override { update(); }
	void leaveEvent(QEvent *) override { update(); }

private:
	QColor bg_, hover_;
};

static QFrame *card()
{
	auto *frame = new QFrame;
	frame->setObjectName(QStringLiteral("tmCard"));
	return frame;
}

// Two-option switch (e.g. "Test colors | Program → Preview").
static QFrame *segmented(QPushButton *a, QPushButton *b, QObject *owner)
{
	auto *frame = new QFrame;
	frame->setProperty("tmRole", "segGroup");
	auto *h = new QHBoxLayout(frame);
	h->setContentsMargins(3, 3, 3, 3);
	h->setSpacing(3);
	for (QPushButton *button : {a, b}) {
		button->setProperty("tmRole", "seg");
		button->setCheckable(true);
		button->setCursor(Qt::PointingHandCursor);
		h->addWidget(button);
	}
	auto *group = new QButtonGroup(owner);
	group->setExclusive(true);
	group->addButton(a);
	group->addButton(b);
	return frame;
}

// =============================================================================
// Construction
// =============================================================================

TransitionManagerWindow::TransitionManagerWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
	setWindowTitle(tr("Transition Manager"));
	setMinimumSize(780, 540);
	resize(1180, 760);

	auto *split = new QSplitter(Qt::Horizontal);
	split->setChildrenCollapsible(false);
	split->setHandleWidth(1);
	split->addWidget(buildSidebar());
	split->addWidget(buildDetail());
	split->setStretchFactor(0, 0);
	split->setStretchFactor(1, 1);
	split->setSizes({290, 890});

	auto *root = new QHBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->addWidget(split);

	probe_ = new MediaProbe(this);
	connect(probe_, &MediaProbe::finished, this, [this](const QString &path, qint64 durationMs, qint64 frames) {
		if (path != stingerPath_ || !isStinger(shown_))
			return;
		OBSDataAutoRelease settings = obs_source_get_settings(shown_);
		const bool byFrame = obs_data_get_int(settings, "tp_type") == 1;
		const qint64 point = obs_data_get_int(settings, "transition_point");

		// Same arithmetic as the Stinger itself.
		stingerTotalMs_ = durationMs + 250;
		stingerFrames_ = frames;
		if (byFrame && frames > 0) {
			stingerCut_ = (double)point / (double)frames;
			stingerCutText_ = tr("cuts at frame %1").arg(point);
		} else {
			stingerCut_ = (double)point / (double)stingerTotalMs_;
			stingerCutText_ = tr("cuts at %1 ms").arg(point);
		}
		stingerCut_ = std::clamp(stingerCut_, 0.001, 0.999);
		updateStingerInfo();
		updateDurationCard();
	});
	connect(probe_, &MediaProbe::failed, this, [this](const QString &path) {
		if (path != stingerPath_)
			return;
		stingerTotalMs_ = -1;
		stingerCut_ = -1.0;
		updateStingerInfo();
		updateDurationCard();
	});

	ticker_.setInterval(kTickMs);
	connect(&ticker_, &QTimer::timeout, this, &TransitionManagerWindow::onPlaybackTick);

	// --- store wiring
	auto &store = TransitionStore::instance();
	connect(&store, &TransitionStore::changed, this, &TransitionManagerWindow::refreshList);
	connect(&store, &TransitionStore::currentChanged, this, &TransitionManagerWindow::refreshList);
	connect(&store, &TransitionStore::durationChanged, this, [this] {
		if (isVisible())
			updateDurationCard();
	});
	connect(&store, &TransitionStore::scenesChanged, this, [this] {
		if (preview_->sourceMode() == TransitionPreview::SourceMode::Scenes && isVisible()) {
			stopPlayback(false);
			preview_->refreshSources();
			updateEndpoints();
		}
	});
	connect(&store, &TransitionStore::collectionUnloading, this, [this] {
		// Let go of everything that belongs to the outgoing collection.
		stopPlayback(false);
		probe_->cancel();
		preview_->releaseScenes();
		preview_->setTransition(nullptr);
		props_->setSource(nullptr);
		shown_ = nullptr;
		shownName_.clear();
		QSignalBlocker block(list_);
		list_->clear();
		detailStack_->setCurrentIndex(0);
	});

	applyStyle();
	refreshList();
}

QWidget *TransitionManagerWindow::buildSidebar()
{
	auto *sidebar = new QWidget;
	sidebar->setObjectName(QStringLiteral("tmSidebar"));
	sidebar->setAttribute(Qt::WA_StyledBackground);
	sidebar->setMinimumWidth(240);
	sidebar->setMaximumWidth(420);

	auto *title = roleLabel(tr("Transitions"), "sidebarTitle");
	countLabel_ = roleLabel(QString(), "muted");

	newBtn_ = roleButton(tr("New"), "primary");
	newBtn_->setToolTip(tr("Add a transition to OBS"));
	auto *newMenu = new QMenu(newBtn_);
	newMenu->setToolTipsVisible(true);
	connect(newMenu, &QMenu::aboutToShow, this, [this, newMenu] { populateNewMenu(newMenu); });
	newBtn_->setMenu(newMenu);

	auto *headerRow = new QHBoxLayout;
	headerRow->addWidget(title);
	headerRow->addWidget(countLabel_);
	headerRow->addStretch();
	headerRow->addWidget(newBtn_);

	filter_ = new QLineEdit;
	filter_->setObjectName(QStringLiteral("tmSearch"));
	filter_->setPlaceholderText(tr("Filter by name or type"));
	filter_->setClearButtonEnabled(true);
	filter_->setAccessibleName(tr("Filter transitions"));

	list_ = new QListWidget;
	list_->setSelectionMode(QAbstractItemView::SingleSelection);
	list_->setDragDropMode(QAbstractItemView::InternalMove);
	list_->setDefaultDropAction(Qt::MoveAction);
	list_->setContextMenuPolicy(Qt::CustomContextMenu);
	list_->setMouseTracking(true);
	list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	list_->setAccessibleName(tr("Transitions"));
	delegate_ = new Delegate(list_);
	list_->setItemDelegate(delegate_);

	sidebarNote_ = roleLabel(QString(), "muted");
	sidebarNote_->setWordWrap(true);

	auto *divider = new QFrame;
	divider->setObjectName(QStringLiteral("tmDivider"));
	divider->setFixedHeight(1);

	auto *v = new QVBoxLayout(sidebar);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);

	auto *top = new QVBoxLayout;
	top->setContentsMargins(16, 16, 16, 10);
	top->setSpacing(12);
	top->addLayout(headerRow);
	top->addWidget(filter_);
	v->addLayout(top);
	v->addWidget(list_, 1);
	v->addWidget(divider);

	auto *bottom = new QVBoxLayout;
	bottom->setContentsMargins(16, 10, 16, 14);
	bottom->addWidget(sidebarNote_);
	v->addLayout(bottom);

	connect(filter_, &QLineEdit::textChanged, this, &TransitionManagerWindow::applyFilter);
	connect(list_, &QListWidget::itemSelectionChanged, this, &TransitionManagerWindow::onSelectionChanged);
	connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
		if (item->data(KindRole).toInt() >= BuiltIn)
			onSetActive();
	});
	connect(list_, &QListWidget::customContextMenuRequested, this, &TransitionManagerWindow::showContextMenu);
	connect(list_->model(), &QAbstractItemModel::rowsMoved, this, &TransitionManagerWindow::onRowsMoved);
	connect(list_->model(), &QAbstractItemModel::rowsInserted, this, &TransitionManagerWindow::onRowsMoved);

	return sidebar;
}

QWidget *TransitionManagerWindow::buildDetail()
{
	detailStack_ = new QStackedWidget;

	// --- page 0: nothing selected
	auto *empty = new QWidget;
	auto *emptyLayout = new QVBoxLayout(empty);
	auto *emptyText = roleLabel(tr("Select a transition on the left, or create one with New."), "muted");
	emptyText->setAlignment(Qt::AlignCenter);
	emptyLayout->addWidget(emptyText);
	detailStack_->addWidget(empty);

	// --- page 1: the selected transition
	auto *page = new QWidget;
	auto *v = new QVBoxLayout(page);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);

	auto *header = new QFrame;
	header->setObjectName(QStringLiteral("tmHeader"));
	auto *hh = new QHBoxLayout(header);
	hh->setContentsMargins(24, 16, 24, 16);
	hh->setSpacing(8);

	nameLabel_ = roleLabel(QString(), "title");
	nameLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	renameBtn_ = roleButton(QString(), "icon");
	renameBtn_->setToolTip(tr("Rename"));
	renameBtn_->setAccessibleName(tr("Rename"));
	subtitle_ = roleLabel(QString(), "muted");

	auto *nameRow = new QHBoxLayout;
	nameRow->setSpacing(4);
	nameRow->addWidget(nameLabel_);
	nameRow->addWidget(renameBtn_);
	nameRow->addStretch();
	auto *titles = new QVBoxLayout;
	titles->setSpacing(2);
	titles->addLayout(nameRow);
	titles->addWidget(subtitle_);

	duplicateBtn_ = roleButton(tr("Duplicate"), "secondary");
	activateBtn_ = roleButton(tr("Make active"), "primary");
	activePill_ = roleLabel(tr("●  Active in OBS"), "activePill");
	moreBtn_ = roleButton(QString(), "iconBox");
	moreBtn_->setToolTip(tr("More actions"));
	moreBtn_->setAccessibleName(tr("More actions"));

	hh->addLayout(titles, 1);
	hh->addWidget(duplicateBtn_);
	hh->addWidget(activateBtn_);
	hh->addWidget(activePill_);
	hh->addWidget(moreBtn_);
	v->addWidget(header);

	auto *body = new QHBoxLayout;
	body->setContentsMargins(24, 20, 24, 24);
	body->setSpacing(20);

	auto *left = new QVBoxLayout;
	left->setSpacing(16);
	left->addWidget(buildPreviewCard());
	left->addWidget(buildDurationCard());
	left->addStretch();

	body->addLayout(left, 3);
	body->addWidget(buildSettingsCard(), 2);
	v->addLayout(body, 1);

	detailStack_->addWidget(page);

	connect(renameBtn_, &QPushButton::clicked, this, &TransitionManagerWindow::onRename);
	connect(duplicateBtn_, &QPushButton::clicked, this, &TransitionManagerWindow::onDuplicate);
	connect(activateBtn_, &QPushButton::clicked, this, &TransitionManagerWindow::onSetActive);
	connect(moreBtn_, &QPushButton::clicked, this, [this] {
		QMenu *menu = buildActionsMenu(this);
		menu->exec(moreBtn_->mapToGlobal(QPoint(0, moreBtn_->height())));
		menu->deleteLater();
	});

	return detailStack_;
}

QWidget *TransitionManagerWindow::buildPreviewCard()
{
	auto *frame = card();
	auto *v = new QVBoxLayout(frame);
	v->setContentsMargins(12, 12, 12, 12);
	v->setSpacing(10);

	preview_ = new TransitionPreview;
	endpoints_ = roleLabel(QString(), "muted");

	playBtn_ = new RoundButton;
	playBtn_->setIconSize(QSize(14, 14));
	playBtn_->setToolTip(tr("Play preview"));
	playBtn_->setAccessibleName(tr("Play preview"));

	scrub_ = new TimelineSlider;
	scrub_->setRange(0, 1000);
	scrub_->setAccessibleName(tr("Scrub"));
	scrub_->setToolTip(tr("Drag to scrub through the transition"));

	timeLabel_ = roleLabel(QStringLiteral("0 ms"), "muted");
	cutLabel_ = roleLabel(QString(), "marker");
	totalLabel_ = roleLabel(QString(), "muted");
	auto *timeRow = new QHBoxLayout;
	timeRow->addWidget(timeLabel_);
	timeRow->addStretch();
	timeRow->addWidget(cutLabel_);
	timeRow->addStretch();
	timeRow->addWidget(totalLabel_);

	auto *scrubColumn = new QVBoxLayout;
	scrubColumn->setSpacing(2);
	scrubColumn->addWidget(scrub_);
	scrubColumn->addLayout(timeRow);

	loopBtn_ = roleButton(QString(), "iconBox");
	loopBtn_->setCheckable(true);
	loopBtn_->setToolTip(tr("Loop preview"));
	loopBtn_->setAccessibleName(tr("Loop preview"));

	auto *transport = new QHBoxLayout;
	transport->setSpacing(12);
	transport->addWidget(playBtn_);
	transport->addLayout(scrubColumn, 1);
	transport->addWidget(loopBtn_);

	colorsBtn_ = new QPushButton(tr("Test colors"));
	scenesBtn_ = new QPushButton(tr("Program → Preview"));
	colorsBtn_->setChecked(true);
	scenesBtn_->setToolTip(tr("Outside studio mode, the scene after the program scene is used as the target."));

	auto *sourcesRow = new QHBoxLayout;
	sourcesRow->addWidget(roleLabel(tr("Preview between"), "muted"));
	sourcesRow->addStretch();
	sourcesRow->addWidget(segmented(colorsBtn_, scenesBtn_, this));
	colorsBtn_->setChecked(true);

	auto *divider = new QFrame;
	divider->setObjectName(QStringLiteral("tmDivider"));
	divider->setFixedHeight(1);

	v->addWidget(preview_);
	v->addWidget(endpoints_);
	v->addLayout(transport);
	v->addWidget(divider);
	v->addLayout(sourcesRow);

	connect(playBtn_, &QPushButton::clicked, this, [this] {
		if (playing_)
			stopPlayback(true);
		else
			startPlayback();
	});
	connect(scrub_, &QSlider::sliderPressed, this, [this] {
		stopPlayback(false);
		preview_->beginScrub(previewLengthMs());
		preview_->scrub(scrub_->value() / 1000.0f);
		updateTimeLabel(scrub_->value() / 1000.0);
	});
	connect(scrub_, &QSlider::valueChanged, this, [this](int value) {
		if (!scrub_->isSliderDown())
			return;
		preview_->scrub(value / 1000.0f);
		updateTimeLabel(value / 1000.0);
	});
	connect(colorsBtn_, &QPushButton::toggled, this, [this](bool on) {
		if (!on)
			return;
		stopPlayback(false);
		preview_->setSourceMode(TransitionPreview::SourceMode::Colors);
		updateEndpoints();
	});
	connect(scenesBtn_, &QPushButton::toggled, this, [this](bool on) {
		if (!on)
			return;
		stopPlayback(false);
		preview_->setSourceMode(TransitionPreview::SourceMode::Scenes);
		updateEndpoints();
	});

	return frame;
}

QWidget *TransitionManagerWindow::buildDurationCard()
{
	// Two cards, one shown at a time (a hidden widget takes no space, so
	// the column keeps the height of whichever is visible).
	auto *container = new QWidget;
	auto *outer = new QVBoxLayout(container);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	// --- adjustable duration
	auto *adjustable = card();
	durationAdjustable_ = adjustable;
	auto *v = new QVBoxLayout(adjustable);
	v->setContentsMargins(16, 14, 16, 16);
	v->setSpacing(12);

	obsDefaultBtn_ = new QPushButton;
	customBtn_ = new QPushButton(tr("Custom"));
	auto *titleRow = new QHBoxLayout;
	titleRow->addWidget(roleLabel(tr("Duration"), "cardTitle"));
	titleRow->addStretch();
	titleRow->addWidget(segmented(obsDefaultBtn_, customBtn_, this));
	obsDefaultBtn_->setChecked(true);

	durationSlider_ = new QSlider(Qt::Horizontal);
	durationSlider_->setRange(50, 5000);
	durationSlider_->setSingleStep(50);
	durationSlider_->setPageStep(250);
	durationSlider_->setAccessibleName(tr("Duration"));
	durationSpin_ = new QSpinBox;
	durationSpin_->setRange(50, 20000);
	durationSpin_->setSingleStep(50);
	durationSpin_->setSuffix(QStringLiteral(" ms"));
	durationSpin_->setAccessibleName(tr("Duration in milliseconds"));

	customRow_ = new QWidget;
	auto *row = new QHBoxLayout(customRow_);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(14);
	row->addWidget(durationSlider_, 1);
	row->addWidget(durationSpin_);

	durationHint_ = roleLabel(QString(), "muted");
	durationHint_->setWordWrap(true);

	v->addLayout(titleRow);
	v->addWidget(customRow_);
	v->addWidget(durationHint_);
	outer->addWidget(adjustable);

	// --- fixed length (Cut, Stinger, ...)
	auto *fixed = card();
	durationFixed_ = fixed;
	auto *fv = new QVBoxLayout(fixed);
	fv->setContentsMargins(16, 14, 16, 16);
	fv->setSpacing(6);
	fixedIcon_ = new QLabel;
	auto *fixedTitle = new QHBoxLayout;
	fixedTitle->setSpacing(8);
	fixedTitle->addWidget(fixedIcon_);
	fixedTitle->addWidget(roleLabel(tr("Duration · fixed length"), "cardTitle"));
	fixedTitle->addStretch();
	fixedText_ = roleLabel(QString(), "muted");
	fixedText_->setWordWrap(true);
	fv->addLayout(fixedTitle);
	fv->addWidget(fixedText_);
	outer->addWidget(fixed);
	fixed->hide();

	connect(customBtn_, &QPushButton::toggled, this, &TransitionManagerWindow::onDurationModeChanged);
	connect(durationSlider_, &QSlider::valueChanged, durationSpin_, &QSpinBox::setValue);
	connect(durationSpin_, &QSpinBox::valueChanged, this, [this](int ms) {
		QSignalBlocker block(durationSlider_);
		durationSlider_->setValue(ms);
		onCustomDurationChanged(ms);
	});
	return container;
}

void TransitionManagerWindow::showFixedDuration(bool fixed)
{
	durationAdjustable_->setVisible(!fixed);
	durationFixed_->setVisible(fixed);
}

QWidget *TransitionManagerWindow::buildSettingsCard()
{
	auto *frame = card();
	frame->setMinimumWidth(300);
	auto *v = new QVBoxLayout(frame);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);

	auto *head = new QHBoxLayout;
	head->setContentsMargins(16, 12, 10, 12);
	head->addWidget(roleLabel(tr("Settings"), "cardTitle"));
	head->addStretch();
	resetBtn_ = roleButton(tr("Reset to defaults"), "link");
	head->addWidget(resetBtn_);

	auto *topDivider = new QFrame;
	topDivider->setObjectName(QStringLiteral("tmDivider"));
	topDivider->setFixedHeight(1);
	auto *bottomDivider = new QFrame;
	bottomDivider->setObjectName(QStringLiteral("tmDivider"));
	bottomDivider->setFixedHeight(1);

	props_ = new PropertiesPanel;
	props_->setObjectName(QStringLiteral("tmSettings"));
	props_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	auto *note = roleLabel(tr("Changes apply live and save with the scene collection."), "muted");
	note->setWordWrap(true);
	auto *foot = new QHBoxLayout;
	foot->setContentsMargins(16, 10, 16, 12);
	foot->addWidget(note);

	v->addLayout(head);
	v->addWidget(topDivider);
	v->addWidget(props_, 1);
	v->addWidget(bottomDivider);
	v->addLayout(foot);

	connect(resetBtn_, &QPushButton::clicked, props_, &PropertiesPanel::resetToDefaults);
	connect(props_, &PropertiesPanel::settingsChanged, this, [this] {
		preview_->syncSettings(props_->source());
		if (isStinger(shown_))
			updateStingerInfo(); // path or transition point may have changed
	});
	return frame;
}

// =============================================================================
// Style
// =============================================================================

void TransitionManagerWindow::applyStyle()
{
	applyingStyle_ = true;
	colors_ = TMStyle::colors(palette());
	setStyleSheet(TMStyle::styleSheet(colors_));
	delegate_->setColors(colors_);

	newBtn_->setIcon(TMStyle::icon(QStringLiteral("plus"), colors_.accentText, 14));
	if (!searchIcon_)
		searchIcon_ = filter_->addAction(QIcon(), QLineEdit::LeadingPosition);
	searchIcon_->setIcon(TMStyle::icon(QStringLiteral("search"), colors_.muted, 15));
	renameBtn_->setIcon(TMStyle::icon(QStringLiteral("pencil"), colors_.muted, 15));
	moreBtn_->setIcon(TMStyle::icon(QStringLiteral("dots"), colors_.text, 16));
	loopBtn_->setIcon(TMStyle::icon(QStringLiteral("loop"), colors_.text, 16));
	playBtn_->setIcon(
		TMStyle::icon(playing_ ? QStringLiteral("stop") : QStringLiteral("play"), colors_.accentText, 14));
	static_cast<RoundButton *>(playBtn_)->setColors(colors_.accent, colors_.dark ? colors_.accent.lighter(115)
										     : colors_.accent.darker(110));
	fixedIcon_->setPixmap(TMStyle::icon(QStringLiteral("clock"), colors_.muted, 18).pixmap(18, 18));
	if (scrub_->marker() >= 0)
		scrub_->setMarker(scrub_->marker(), colors_.marker);
	list_->viewport()->update();
	applyingStyle_ = false;
}

void TransitionManagerWindow::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	// Follow OBS theme switches.
	if (event->type() == QEvent::PaletteChange && !applyingStyle_ && delegate_)
		applyStyle();
}

// =============================================================================
// Window lifecycle
// =============================================================================

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

	// Closed: stop rendering the preview copy and drop its references.
	stopPlayback(false);
	probe_->cancel();
	preview_->setTransition(nullptr);
	shown_ = nullptr;
	shownName_.clear();
}

// =============================================================================
// Sidebar list
// =============================================================================

obs_source_t *TransitionManagerWindow::selected() const
{
	QListWidgetItem *item = list_->currentItem();
	if (!item || !item->isSelected() || item->data(KindRole).toInt() < BuiltIn)
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
	const auto all = store.transitions();
	const bool editable = store.canEditList();
	const bool reorderable = store.canReorder();

	std::vector<OBSSource> builtIn, custom;
	for (const OBSSource &s : all)
		(obs_source_configurable(s) ? custom : builtIn).push_back(s);

	auto addHeader = [this](const QString &text, Kind kind) {
		auto *item = new QListWidgetItem(text, list_);
		item->setData(KindRole, kind);
		item->setFlags(Qt::ItemIsEnabled);
	};
	auto addRow = [&](const OBSSource &s, Kind kind) {
		auto *item = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(s)), list_);
		item->setData(SourceRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(s.Get())));
		item->setData(KindRole, kind);
		item->setData(TypeRole, QString::fromUtf8(obs_source_get_id(s)));
		item->setData(ActiveRole, s.Get() == current.Get());
		item->setData(DurationRole, store.defaultDuration(s));

		QString tip = QString::fromUtf8(obs_source_get_display_name(obs_source_get_id(s)));
		if (kind == BuiltIn)
			tip += tr(" · built in, can't be removed");
		item->setToolTip(tip);

		Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
		if (kind == Custom && reorderable)
			flags |= Qt::ItemIsDragEnabled;
		item->setFlags(flags);
		if (s.Get() == keep)
			list_->setCurrentItem(item);
	};

	{
		refreshing_ = true;
		QSignalBlocker block(list_);
		list_->clear();

		if (!builtIn.empty()) {
			addHeader(tr("BUILT IN"), Header);
			for (const OBSSource &s : builtIn)
				addRow(s, BuiltIn);
		}
		addHeader(tr("CUSTOM"), Header);
		for (const OBSSource &s : custom)
			addRow(s, Custom);
		if (custom.empty())
			addHeader(tr("None yet. Use New to add one."), Placeholder);
		refreshing_ = false;
	}

	countLabel_->setText(QString::number(all.size()));
	newBtn_->setEnabled(editable);
	if (reorderable)
		sidebarNote_->setText(tr("Drag to reorder. The order matches the Scene Transitions dropdown in OBS."));
	else if (editable)
		sidebarNote_->setText(tr("The order matches the Scene Transitions dropdown in OBS. This version of "
					 "OBS doesn't keep a custom order, so reordering is turned off."));
	else
		sidebarNote_->setText(tr("OBS's transition list couldn't be accessed, so adding, removing, renaming "
					 "and reordering are turned off. Settings, preview and switching still work."));
	applyFilter();

	// A rename changes the header.
	if (shown_ && shown_ == selected() && shownName_ != QString::fromUtf8(obs_source_get_name(shown_)))
		shownName_ = QString::fromUtf8(obs_source_get_name(shown_));

	onSelectionChanged();
}

void TransitionManagerWindow::applyFilter()
{
	const QString text = filter_->text().trimmed();

	// Hide rows that don't match, then any section left empty.
	QListWidgetItem *header = nullptr;
	bool sectionHasRows = false;
	auto closeSection = [&] {
		if (header)
			header->setHidden(!text.isEmpty() && !sectionHasRows);
	};
	for (int i = 0; i < list_->count(); ++i) {
		QListWidgetItem *item = list_->item(i);
		const int kind = item->data(KindRole).toInt();
		if (kind == Header) {
			closeSection();
			header = item;
			sectionHasRows = false;
			continue;
		}
		if (kind == Placeholder) {
			item->setHidden(!text.isEmpty());
			continue;
		}
		const bool match = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive) ||
				   item->toolTip().contains(text, Qt::CaseInsensitive);
		item->setHidden(!match);
		sectionHasRows |= match;
	}
	closeSection();

	// Reordering a filtered list would be ambiguous.
	const bool canDrag = text.isEmpty() && TransitionStore::instance().canReorder();
	list_->setDragEnabled(canDrag);
	list_->setDragDropMode(canDrag ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
}

void TransitionManagerWindow::onRowsMoved()
{
	if (refreshing_ || reordering_)
		return;

	// Let the drop finish, then apply the new order of the custom rows to
	// OBS's list. Built-in transitions keep their positions.
	QTimer::singleShot(0, this, [this] {
		if (refreshing_ || reordering_)
			return;

		std::vector<obs_source_t *> customOrder;
		for (int i = 0; i < list_->count(); ++i)
			if (list_->item(i)->data(KindRole).toInt() == Custom)
				customOrder.push_back(itemSource(list_->item(i)));

		auto &store = TransitionStore::instance();
		const auto current = store.transitions();
		std::vector<obs_source_t *> desired;
		size_t next = 0;
		for (const OBSSource &s : current) {
			if (!obs_source_configurable(s))
				desired.push_back(s);
			else if (next < customOrder.size())
				desired.push_back(customOrder[next++]);
		}
		if (desired.size() != current.size()) {
			refreshList();
			return;
		}

		reordering_ = true;
		for (size_t i = 0; i < desired.size(); ++i) {
			const auto now = store.transitions();
			for (size_t j = 0; j < now.size(); ++j) {
				if (now[j].Get() == desired[i] && j != i) {
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
		if (item->data(KindRole).toInt() >= BuiltIn && itemSource(item) == source) {
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
		stopPlayback(false);
		shown_ = source;
		shownName_ = source ? QString::fromUtf8(obs_source_get_name(source)) : QString();

		// Preview first: building the panel can write fallback values back
		// (see PropertiesPanel), which are mirrored into the preview copy.
		preview_->setTransition(source);
		props_->setSource(source);
		scrub_->setValue(0);

		stingerPath_.clear();
		stingerTotalMs_ = -1;
		stingerFrames_ = 0;
		stingerCut_ = -1.0;
		if (isStinger(source))
			updateStingerInfo();
		else
			probe_->cancel();
	}

	detailStack_->setCurrentIndex(source ? 1 : 0);
	if (!source)
		return;

	updateHeader();
	updateDurationCard();
	updateEndpoints();
	updateStingerInfo();
}

// =============================================================================
// Detail updates
// =============================================================================

void TransitionManagerWindow::updateHeader()
{
	auto &store = TransitionStore::instance();
	obs_source_t *source = shown_;
	const bool active = store.current().Get() == source;
	const bool editable = store.canEditList();

	nameLabel_->setText(QString::fromUtf8(obs_source_get_name(source)));
	const QString type = QString::fromUtf8(obs_source_get_display_name(obs_source_get_id(source)));
	QString subtitle = active ? tr("%1 · used for scene switches right now").arg(type)
				  : tr("%1 · not active").arg(type);
	const QString usage = ManagerHooks::usage(source);
	if (!usage.isEmpty())
		subtitle += QStringLiteral(" · ") + usage;
	subtitle_->setText(subtitle);

	renameBtn_->setVisible(editable);
	duplicateBtn_->setEnabled(editable);
	activateBtn_->setVisible(!active);
	activePill_->setVisible(active);
	resetBtn_->setEnabled(obs_source_configurable(source));
}

void TransitionManagerWindow::refreshUsage()
{
	if (shown_)
		updateHeader();
}

void TransitionManagerWindow::updateDurationCard()
{
	obs_source_t *source = shown_;
	if (!source)
		return;

	auto &store = TransitionStore::instance();
	const int global = obs_frontend_get_transition_duration();

	if (obs_transition_fixed(source)) {
		showFixedDuration(true);
		if (!isStinger(source))
			fixedText_->setText(tr("This transition always runs for the same length."));
		else if (stingerPath_.isEmpty())
			fixedText_->setText(tr("Stinger runs for the length of its video. Choose a video file under "
					       "Settings."));
		else if (stingerTotalMs_ > 0)
			fixedText_->setText(tr("Stinger runs for the length of its video (%1 ms). The scene %2, set "
					       "under Settings.")
						    .arg(stingerTotalMs_)
						    .arg(stingerCutText_));
		else
			fixedText_->setText(tr("Stinger runs for the length of its video."));
		return;
	}

	showFixedDuration(false);
	const int own = store.defaultDuration(source);

	QSignalBlocker b1(obsDefaultBtn_), b2(customBtn_), b3(durationSpin_), b4(durationSlider_);
	obsDefaultBtn_->setText(tr("OBS default · %1 ms").arg(global));
	customBtn_->setChecked(own > 0);
	obsDefaultBtn_->setChecked(own <= 0);
	const int value = own > 0 ? own : global;
	durationSpin_->setValue(value);
	durationSlider_->setValue(value);
	customRow_->setEnabled(own > 0);

	durationHint_->setText(own > 0 ? tr("OBS switches to %1 ms whenever this transition becomes active.").arg(own)
				       : tr("Uses the duration set in OBS's Scene Transitions dock."));
	totalLabel_->setText(tr("%1 ms").arg(previewLengthMs()));
}

void TransitionManagerWindow::updateEndpoints()
{
	endpoints_->setText(tr("A · %1   →   B · %2").arg(preview_->fromName(), preview_->toName()));
}

void TransitionManagerWindow::updateStingerInfo()
{
	if (!isStinger(shown_)) {
		scrub_->setMarker(-1.0, colors_.marker);
		cutLabel_->clear();
		totalLabel_->setText(tr("%1 ms").arg(previewLengthMs()));
		return;
	}

	OBSDataAutoRelease settings = obs_source_get_settings(shown_);
	const QString path = QString::fromUtf8(obs_data_get_string(settings, "path"));
	if (path != stingerPath_) {
		// New video: measure it the way the Stinger does.
		stingerPath_ = path;
		stingerTotalMs_ = -1;
		stingerFrames_ = 0;
		stingerCut_ = -1.0;
		if (!path.isEmpty())
			probe_->probe(path); // answers via finished/failed
	} else if (stingerTotalMs_ > 0) {
		// Same video, maybe a new transition point: redo the arithmetic.
		const bool byFrame = obs_data_get_int(settings, "tp_type") == 1;
		const qint64 point = obs_data_get_int(settings, "transition_point");
		if (byFrame && stingerFrames_ > 0) {
			stingerCut_ = (double)point / (double)stingerFrames_;
			stingerCutText_ = tr("cuts at frame %1").arg(point);
		} else {
			stingerCut_ = (double)point / (double)stingerTotalMs_;
			stingerCutText_ = tr("cuts at %1 ms").arg(point);
		}
		stingerCut_ = std::clamp(stingerCut_, 0.001, 0.999);
	}

	if (stingerCut_ >= 0.0) {
		scrub_->setMarker(stingerCut_, colors_.marker);
		cutLabel_->setText(QStringLiteral("▲ ") + stingerCutText_);
	} else {
		scrub_->setMarker(-1.0, colors_.marker);
		cutLabel_->clear();
	}
	totalLabel_->setText(stingerTotalMs_ > 0 ? tr("%1 ms").arg(stingerTotalMs_) : QString());
	updateDurationCard();
}

// =============================================================================
// Duration
// =============================================================================

void TransitionManagerWindow::onDurationModeChanged(bool custom)
{
	auto &store = TransitionStore::instance();
	if (!shown_)
		return;
	if (custom)
		store.setDefaultDuration(shown_, durationSpin_->value());
	else
		store.clearDefaultDuration(shown_);
	refreshList(); // updates the sidebar chip and this card
}

void TransitionManagerWindow::onCustomDurationChanged(int ms)
{
	if (!shown_ || !customBtn_->isChecked())
		return;
	TransitionStore::instance().setDefaultDuration(shown_, ms);
	durationHint_->setText(tr("OBS switches to %1 ms whenever this transition becomes active.").arg(ms));
	totalLabel_->setText(tr("%1 ms").arg(ms));

	// Update the sidebar chip in place; a full refresh would steal focus
	// from the spin box while typing.
	for (int i = 0; i < list_->count(); ++i) {
		if (itemSource(list_->item(i)) == shown_)
			list_->item(i)->setData(DurationRole, ms);
	}
}

// =============================================================================
// Preview playback
// =============================================================================

int TransitionManagerWindow::previewLengthMs() const
{
	if (!shown_)
		return 300;
	if (obs_transition_fixed(shown_))
		return stingerTotalMs_ > 0 ? (int)stingerTotalMs_ : 1000;
	const int own = TransitionStore::instance().defaultDuration(shown_);
	return own > 0 ? own : obs_frontend_get_transition_duration();
}

void TransitionManagerWindow::startPlayback()
{
	if (!shown_)
		return;
	preview_->play(previewLengthMs());
	elapsed_.restart();
	playing_ = true;
	ticker_.start();
	playBtn_->setIcon(TMStyle::icon(QStringLiteral("stop"), colors_.accentText, 14));
	playBtn_->setToolTip(tr("Stop preview"));
}

void TransitionManagerWindow::stopPlayback(bool resetPreview)
{
	ticker_.stop();
	playing_ = false;
	if (playBtn_) {
		playBtn_->setIcon(TMStyle::icon(QStringLiteral("play"), colors_.accentText, 14));
		playBtn_->setToolTip(tr("Play preview"));
	}
	if (resetPreview) {
		preview_->reset();
		QSignalBlocker block(scrub_);
		scrub_->setValue(0);
		updateTimeLabel(0.0);
	}
}

void TransitionManagerWindow::onPlaybackTick()
{
	const int length = std::max(1, previewLengthMs());
	const qint64 ms = elapsed_.elapsed();

	if (ms <= length) {
		const double t = (double)ms / length;
		QSignalBlocker block(scrub_);
		scrub_->setValue((int)(t * 1000));
		updateTimeLabel(t);
		return;
	}

	// Finished. Hold the end frame briefly, then loop or stop.
	{
		QSignalBlocker block(scrub_);
		scrub_->setValue(1000);
		updateTimeLabel(1.0);
	}
	if (!loopBtn_->isChecked()) {
		stopPlayback(false);
		return;
	}
	if (ms > length + kLoopGapMs) {
		preview_->play(length);
		elapsed_.restart();
	}
}

void TransitionManagerWindow::updateTimeLabel(double t)
{
	timeLabel_->setText(tr("%1 ms").arg((int)(t * previewLengthMs())));
}

// =============================================================================
// Actions
// =============================================================================

void TransitionManagerWindow::populateNewMenu(QMenu *menu)
{
	menu->clear();
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
		QAction *action = menu->addAction(TMStyle::typeIcon(id, colors_.text),
						  QString::fromUtf8(display ? display : id), this,
						  [this, typeId] { onCreate(typeId); });
		action->setToolTip(TMStyle::typeDescription(id));
	}
	if (menu->isEmpty())
		menu->addAction(tr("No configurable transition types available"))->setEnabled(false);

	if (obs_source_t *source = selected()) {
		menu->addSeparator();
		menu->addAction(TMStyle::icon(QStringLiteral("duplicate"), colors_.text),
				tr("Duplicate “%1”").arg(QString::fromUtf8(obs_source_get_name(source))), this,
				&TransitionManagerWindow::onDuplicate);
	}
}

QMenu *TransitionManagerWindow::buildActionsMenu(QWidget *parent)
{
	auto &store = TransitionStore::instance();
	obs_source_t *source = selected();
	const bool editable = store.canEditList();
	const bool active = source && store.current().Get() == source;

	auto *menu = new QMenu(parent);
	menu->addAction(tr("Make active"), this, &TransitionManagerWindow::onSetActive)->setEnabled(source && !active);
	menu->addSeparator();
	menu->addAction(TMStyle::icon(QStringLiteral("pencil"), colors_.text), tr("Rename"), this,
			&TransitionManagerWindow::onRename)
		->setEnabled(source && editable);
	menu->addAction(TMStyle::icon(QStringLiteral("duplicate"), colors_.text), tr("Duplicate"), this,
			&TransitionManagerWindow::onDuplicate)
		->setEnabled(source && editable);
	menu->addSeparator();
	if (ManagerHooks::hasRules()) {
		OBSSource keep(source);
		menu->addAction(ManagerHooks::get().openRulesLabel, this, [keep] { ManagerHooks::openRules(keep); });
		menu->addSeparator();
	}

	QAction *remove = menu->addAction(TMStyle::icon(QStringLiteral("trash"), colors_.text), tr("Remove"), this,
					  &TransitionManagerWindow::onRemove);
	remove->setEnabled(store.canRemove(source));
	if (source && !obs_source_configurable(source))
		remove->setText(tr("Remove (built in, can't be removed)"));
	else if (active)
		remove->setText(tr("Remove (switch to another transition first)"));
	return menu;
}

void TransitionManagerWindow::showContextMenu(const QPoint &pos)
{
	QListWidgetItem *item = list_->itemAt(pos);
	if (!item || item->data(KindRole).toInt() < BuiltIn)
		return;
	list_->setCurrentItem(item);

	QMenu *menu = buildActionsMenu(this);
	menu->exec(list_->viewport()->mapToGlobal(pos));
	menu->deleteLater();
}

void TransitionManagerWindow::onCreate(const QString &typeId)
{
	if (obs_source_t *source = createOfType(typeId, this))
		selectSource(source);
}

obs_source_t *TransitionManagerWindow::createOfType(const QString &typeId, QWidget *dialogParent)
{
	auto &store = TransitionStore::instance();
	const QByteArray id = typeId.toUtf8();
	const char *display = obs_source_get_display_name(id.constData());
	QString name;
	if (!store.obsPrompts()) { // otherwise OBS asks for the name itself
		const QString suggested = store.uniqueName(QString::fromUtf8(display ? display : id.constData()));
		name = TransitionDialogs::askName(dialogParent, tr("New transition"), suggested);
		if (name.isEmpty())
			return nullptr;
	}

	obs_source_t *source = store.createTransition(id.constData(), name);
	if (!source && !store.obsPrompts()) // with OBS's dialog, nullptr usually means cancelled
		QMessageBox::warning(dialogParent, tr("New transition"), tr("The transition couldn't be created."));
	return source;
}

obs_source_t *TransitionManagerWindow::createFromMenu(QWidget *parent, const QPoint &globalPos)
{
	if (!TransitionStore::instance().canEditList()) {
		QMessageBox::information(parent, tr("New transition"),
					 tr("OBS's transition list couldn't be accessed, so transitions can't be added "
					    "from here. Add it from OBS's Scene Transitions dock instead."));
		return nullptr;
	}

	QMenu menu(parent);
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_transition_types(i, &id); ++i) {
		const uint32_t flags = obs_get_source_output_flags(id);
		if (flags & (OBS_SOURCE_DEPRECATED | OBS_SOURCE_CAP_DISABLED))
			continue;
		if (!obs_is_source_configurable(id))
			continue;
		const char *display = obs_source_get_display_name(id);
		QAction *action =
			menu.addAction(TMStyle::typeIcon(id, colors_.text), QString::fromUtf8(display ? display : id));
		action->setData(QString::fromUtf8(id));
		action->setToolTip(TMStyle::typeDescription(id));
	}
	if (menu.isEmpty())
		menu.addAction(tr("No configurable transition types available"))->setEnabled(false);
	menu.setToolTipsVisible(true);

	QAction *chosen = menu.exec(globalPos);
	if (!chosen || chosen->data().toString().isEmpty())
		return nullptr;

	obs_source_t *source = createOfType(chosen->data().toString(), parent);
	if (source)
		open(source); // its settings live here
	return source;
}

void TransitionManagerWindow::onDuplicate()
{
	obs_source_t *source = selected();
	if (!source)
		return;
	auto &store = TransitionStore::instance();
	QString name;
	if (!store.obsPrompts()) { // otherwise OBS asks for the name itself
		name = TransitionDialogs::askName(
			this, tr("Duplicate transition"),
			store.uniqueName(QString::fromUtf8(obs_source_get_name(source)) + tr(" (copy)")));
		if (name.isEmpty())
			return;
	}
	if (obs_source_t *copy = store.duplicateTransition(source, name))
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
