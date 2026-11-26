#include "transition-actions.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace TransitionActions {

namespace {

const char *kKindNames[] = {QT_TR_NOOP("Mute"),          QT_TR_NOOP("Unmute"),      QT_TR_NOOP("Toggle mute"),
			    QT_TR_NOOP("Set volume"),    QT_TR_NOOP("Show source"), QT_TR_NOOP("Hide source"),
			    QT_TR_NOOP("Restart media"), QT_TR_NOOP("Press hotkey")};

QString tr(const char *text)
{
	return QObject::tr(text);
}

QStringList inputNames(uint32_t requiredFlags)
{
	struct Ctx {
		uint32_t flags;
		QStringList names;
	} ctx{requiredFlags, {}};
	obs_enum_sources(
		[](void *data, obs_source_t *s) {
			auto *c = static_cast<Ctx *>(data);
			if ((obs_source_get_output_flags(s) & c->flags) == c->flags)
				c->names << QString::fromUtf8(obs_source_get_name(s));
			return true;
		},
		&ctx);
	ctx.names.sort(Qt::CaseInsensitive);
	return ctx.names;
}

QStringList sceneNames()
{
	QStringList names;
	char **raw = obs_frontend_get_scene_names();
	for (char **n = raw; n && *n; ++n)
		names << QString::fromUtf8(*n);
	bfree(raw);
	return names;
}

// Combo whose items carry a name; shows a stored name even if it's gone.
void fill(QComboBox *combo, const QStringList &names, const std::string &current)
{
	combo->clear();
	for (const QString &n : names)
		combo->addItem(n, n);
	const QString cur = QString::fromStdString(current);
	if (!cur.isEmpty() && combo->findData(cur) < 0)
		combo->addItem(cur + tr(" (missing)"), cur);
	combo->setCurrentIndex(std::max(0, combo->findData(cur)));
}

struct Row {
	QWidget *widget;
	QComboBox *when, *kind, *scene, *source, *hotkey;
	QDoubleSpinBox *volume;
	QSpinBox *delay;
};

void updateRow(Row &r, const Action &a)
{
	const Kind kind = (Kind)r.kind->currentIndex();
	const bool showHide = kind == Kind::Show || kind == Kind::Hide;
	r.scene->setVisible(showHide);
	r.source->setVisible(kind != Kind::Hotkey);
	r.hotkey->setVisible(kind == Kind::Hotkey);
	r.volume->setVisible(kind == Kind::SetVolume);

	if (showHide) {
		r.scene->clear();
		r.scene->addItem(tr("Scene switched to"), QString());
		for (const QString &n : sceneNames())
			r.scene->addItem(tr("In %1").arg(n), n);
		r.scene->setCurrentIndex(std::max(0, r.scene->findData(QString::fromStdString(a.scene))));
		fill(r.source, inputNames(0) + sceneNames(), a.source);
	} else if (kind == Kind::RestartMedia) {
		fill(r.source, inputNames(OBS_SOURCE_CONTROLLABLE_MEDIA), a.source);
	} else if (kind != Kind::Hotkey) {
		fill(r.source, inputNames(OBS_SOURCE_AUDIO), a.source);
	}
}

void fillHotkeys(QComboBox *combo, const Action &a)
{
	struct Ctx {
		QComboBox *combo;
	} ctx{combo};
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id, obs_hotkey_t *key) {
			auto *combo = static_cast<Ctx *>(data)->combo;
			QString owner;
			const auto type = obs_hotkey_get_registerer_type(key);
			if (type == OBS_HOTKEY_REGISTERER_SOURCE) {
				OBSSourceAutoRelease s = obs_weak_source_get_source(
					static_cast<obs_weak_source_t *>(obs_hotkey_get_registerer(key)));
				if (!s)
					return true;
				owner = QString::fromUtf8(obs_source_get_name(s));
			} else if (type != OBS_HOTKEY_REGISTERER_FRONTEND) {
				return true;
			}
			const QString desc = QString::fromUtf8(obs_hotkey_get_description(key));
			combo->addItem(owner.isEmpty() ? desc : owner + QStringLiteral(": ") + desc,
				       QStringList{owner, QString::fromUtf8(obs_hotkey_get_name(key))});
			return true;
		},
		&ctx);
	const QStringList cur{QString::fromStdString(a.source), QString::fromStdString(a.hotkey)};
	if (!a.hotkey.empty() && combo->findData(cur) < 0)
		combo->addItem(QString::fromStdString(a.hotkey) + tr(" (missing)"), cur);
	combo->setCurrentIndex(std::max(0, combo->findData(cur)));
}

Action readRow(const Row &r)
{
	Action a;
	a.when = (When)r.when->currentIndex();
	a.kind = (Kind)r.kind->currentIndex();
	a.delayMs = r.delay->value();
	a.volumeDb = r.volume->value();
	if (a.kind == Kind::Hotkey) {
		const QStringList key = r.hotkey->currentData().toStringList();
		if (key.size() == 2) {
			a.source = key[0].toStdString();
			a.hotkey = key[1].toStdString();
		}
	} else {
		a.source = r.source->currentData().toString().toStdString();
		if (a.kind == Kind::Show || a.kind == Kind::Hide)
			a.scene = r.scene->currentData().toString().toStdString();
	}
	return a;
}

} // namespace

bool edit(QWidget *parent, const QString &level, ActionSet &set, bool canReplace)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(tr("Actions · %1").arg(level));
	dialog.resize(760, 420);

	auto *intro = new QLabel(
		tr("Run when the transition starts or ends. Global actions run for every transition, then the "
		   "transition's own, then those of the Transition Tree rule that picked it."));
	intro->setWordWrap(true);

	auto *rowsHost = new QWidget;
	auto *rows = new QVBoxLayout(rowsHost);
	rows->addStretch();
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setWidget(rowsHost);

	auto list = std::make_shared<std::vector<std::unique_ptr<Row>>>();
	auto addRow = [&dialog, rows, list](const Action &a) {
		auto r = std::make_unique<Row>();
		r->widget = new QWidget;
		auto *h = new QHBoxLayout(r->widget);
		h->setContentsMargins(0, 0, 0, 0);
		r->when = new QComboBox;
		r->when->addItems({tr("At start"), tr("At end")});
		r->when->setCurrentIndex((int)a.when);
		r->kind = new QComboBox;
		for (const char *name : kKindNames)
			r->kind->addItem(tr(name));
		r->kind->setCurrentIndex((int)a.kind);
		r->scene = new QComboBox;
		r->source = new QComboBox;
		r->source->setMinimumWidth(150);
		r->hotkey = new QComboBox;
		r->hotkey->setMinimumWidth(220);
		fillHotkeys(r->hotkey, a);
		r->volume = new QDoubleSpinBox;
		r->volume->setRange(-100.0, 20.0);
		r->volume->setSuffix(QStringLiteral(" dB"));
		r->volume->setValue(a.volumeDb);
		r->delay = new QSpinBox;
		r->delay->setRange(0, 600000);
		r->delay->setSingleStep(100);
		r->delay->setPrefix(tr("after "));
		r->delay->setSuffix(QStringLiteral(" ms"));
		r->delay->setValue(a.delayMs);
		auto *remove = new QPushButton(QStringLiteral("✕"));
		remove->setToolTip(tr("Remove this action"));
		remove->setFixedWidth(28);
		for (QWidget *w :
		     std::initializer_list<QWidget *>{r->when, r->kind, r->scene, r->source, r->hotkey, r->volume})
			h->addWidget(w);
		h->addStretch();
		h->addWidget(r->delay);
		h->addWidget(remove);

		Row *raw = r.get();
		updateRow(*raw, a);
		QObject::connect(raw->kind, &QComboBox::currentIndexChanged, &dialog,
				 [raw] { updateRow(*raw, readRow(*raw)); });
		QObject::connect(remove, &QPushButton::clicked, &dialog, [list, raw] {
			raw->widget->deleteLater();
			list->erase(std::remove_if(list->begin(), list->end(),
						   [raw](const auto &p) { return p.get() == raw; }),
				    list->end());
		});
		rows->insertWidget(rows->count() - 1, raw->widget);
		list->push_back(std::move(r));
	};
	for (const Action &a : set.actions)
		addRow(a);

	auto *add = new QPushButton(tr("Add action"));
	QObject::connect(add, &QPushButton::clicked, &dialog, [addRow] { addRow(Action()); });
	auto *replace = new QCheckBox(tr("Replace broader actions"));
	replace->setToolTip(tr("Run only these, skipping the global actions (and, for a rule, the transition's)."));
	replace->setChecked(set.replaceBroader);
	replace->setVisible(canReplace);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	auto *v = new QVBoxLayout(&dialog);
	v->addWidget(intro);
	v->addWidget(scroll, 1);
	auto *bottom = new QHBoxLayout;
	bottom->addWidget(add);
	bottom->addWidget(replace, 1);
	bottom->addWidget(buttons);
	v->addLayout(bottom);

	if (dialog.exec() != QDialog::Accepted)
		return false;
	set.replaceBroader = canReplace && replace->isChecked();
	set.actions.clear();
	for (const auto &r : *list)
		set.actions.push_back(readRow(*r));
	return true;
}

} // namespace TransitionActions
