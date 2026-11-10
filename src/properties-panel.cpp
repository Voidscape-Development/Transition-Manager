#include "properties-panel.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <media-io/frame-rate.h>

#include <climits>
#include <cmath>

// ---- helpers --------------------------------------------------------------

static QColor colorFromObs(uint32_t c, bool alpha)
{
	return QColor(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, alpha ? (c >> 24) & 0xFF : 255);
}

static uint32_t colorToObs(const QColor &c)
{
	return (uint32_t)c.red() | ((uint32_t)c.green() << 8) | ((uint32_t)c.blue() << 16) |
	       ((uint32_t)c.alpha() << 24);
}

static void styleColorButton(QPushButton *button, const QColor &color)
{
	button->setText(color.name(QColor::HexArgb).toUpper());
	button->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
				      .arg(color.name())
				      .arg(color.lightness() > 128 ? "black" : "white"));
}

static int clampInt(long long v)
{
	return (int)std::max<long long>(INT_MIN, std::min<long long>(INT_MAX, v));
}

static QFont fontFromObs(obs_data_t *font)
{
	QFont f;
	if (!font)
		return f;
	const QString face = QString::fromUtf8(obs_data_get_string(font, "face"));
	const QString style = QString::fromUtf8(obs_data_get_string(font, "style"));
	const int size = (int)obs_data_get_int(font, "size");
	const uint32_t flags = (uint32_t)obs_data_get_int(font, "flags");

	if (!face.isEmpty())
		f.setFamily(face);
	if (!style.isEmpty())
		f.setStyleName(style);
	if (size > 0)
		f.setPointSize(size);
	f.setBold(flags & OBS_FONT_BOLD);
	f.setItalic(flags & OBS_FONT_ITALIC);
	f.setUnderline(flags & OBS_FONT_UNDERLINE);
	f.setStrikeOut(flags & OBS_FONT_STRIKEOUT);
	return f;
}

static obs_data_t *fontToObs(const QFont &f)
{
	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", f.family().toUtf8().constData());
	obs_data_set_string(font, "style", f.styleName().toUtf8().constData());
	obs_data_set_int(font, "size", f.pointSize() > 0 ? f.pointSize() : 12);

	uint32_t flags = 0;
	if (f.bold())
		flags |= OBS_FONT_BOLD;
	if (f.italic())
		flags |= OBS_FONT_ITALIC;
	if (f.underline())
		flags |= OBS_FONT_UNDERLINE;
	if (f.strikeOut())
		flags |= OBS_FONT_STRIKEOUT;
	obs_data_set_int(font, "flags", flags);
	return font;
}

static QString describeFont(const QFont &f)
{
	QString text = f.family();
	if (!f.styleName().isEmpty())
		text += QStringLiteral(" ") + f.styleName();
	return text + QStringLiteral(", %1pt").arg(f.pointSize());
}

static double fpsValue(const media_frames_per_second &fps)
{
	return fps.denominator ? (double)fps.numerator / fps.denominator : 0.0;
}

static QString describeFps(const media_frames_per_second &fps)
{
	return fps.denominator == 1 ? QString::number(fps.numerator) : QString::number(fpsValue(fps), 'f', 2);
}

// ---- panel ------------------------------------------------------------------

PropertiesPanel::PropertiesPanel(QWidget *parent) : QScrollArea(parent)
{
	setWidgetResizable(true);
	setFrameShape(QFrame::NoFrame);
	rebuild();
}

PropertiesPanel::~PropertiesPanel()
{
	// Widgets hold raw obs_property_t pointers; delete them before props_.
	clearWidgets();
}

void PropertiesPanel::setSource(obs_source_t *source)
{
	if (source_.Get() == source)
		return;
	source_ = source;
	settings_ = source ? obs_source_get_settings(source) : nullptr;
	rebuild();
}

void PropertiesPanel::refresh()
{
	if (source_)
		scheduleRebuild();
}

void PropertiesPanel::clearWidgets()
{
	delete takeWidget();
}

void PropertiesPanel::scheduleRebuild()
{
	if (rebuildPending_)
		return;
	rebuildPending_ = true;
	// Deferred so the widget that triggered it finishes its signal first.
	QMetaObject::invokeMethod(
		this,
		[this] {
			rebuildPending_ = false;
			rebuild();
		},
		Qt::QueuedConnection);
}

void PropertiesPanel::rebuild()
{
	const int scroll = verticalScrollBar() ? verticalScrollBar()->value() : 0;

	clearWidgets();
	props_.reset();

	auto *container = new QWidget;
	auto *outer = new QVBoxLayout(container);

	if (!source_) {
		auto *empty = new QLabel(tr("Select a transition to edit its settings."));
		empty->setAlignment(Qt::AlignCenter);
		empty->setEnabled(false);
		outer->addWidget(empty);
		outer->addStretch();
		setWidget(container);
		return;
	}

	const char *typeId = obs_source_get_id(source_);
	auto *header = new QLabel(QStringLiteral("<b>%1</b> &nbsp;<span style='opacity:0.6'>%2</span>")
					  .arg(QString::fromUtf8(obs_source_get_name(source_)).toHtmlEscaped(),
					       QString::fromUtf8(obs_source_get_display_name(typeId)).toHtmlEscaped()));
	outer->addWidget(header);

	props_.reset(obs_source_properties(source_));
	auto *form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

	if (props_) {
		// Runs every modified callback once so visibility starts correct.
		obs_properties_apply_settings(props_.get(), settings_);
		for (obs_property_t *p = obs_properties_first(props_.get()); p; obs_property_next(&p))
			addProperty(p, form);
	}

	if (form->rowCount() == 0)
		form->addRow(new QLabel(tr("This transition has no settings.")));

	outer->addLayout(form);

	auto *resetBtn = new QPushButton(tr("Reset to defaults"));
	connect(resetBtn, &QPushButton::clicked, this, &PropertiesPanel::resetToDefaults);
	outer->addWidget(resetBtn, 0, Qt::AlignRight);
	outer->addStretch();

	setWidget(container);
	verticalScrollBar()->setValue(scroll);
}

void PropertiesPanel::commit(obs_property_t *prop)
{
	// settings_ is the source's live settings object, so a null update just
	// asks the source to re-read it.
	obs_source_update(source_, nullptr);
	emit settingsChanged();

	if (prop && obs_property_modified(prop, settings_))
		scheduleRebuild();
}

void PropertiesPanel::resetToDefaults()
{
	if (!source_)
		return;
	obs_data_clear(settings_);
	obs_source_update(source_, nullptr);
	emit settingsChanged();
	scheduleRebuild();
}

void PropertiesPanel::addProperty(obs_property_t *prop, QFormLayout *layout)
{
	if (!obs_property_visible(prop))
		return;

	const QByteArray key(obs_property_name(prop));
	const QString label = QString::fromUtf8(obs_property_description(prop));
	const QString tooltip = QString::fromUtf8(obs_property_long_description(prop));
	QWidget *field = nullptr;
	bool fullRow = false;

	switch (obs_property_get_type(prop)) {
	case OBS_PROPERTY_BOOL: {
		auto *check = new QCheckBox(label);
		check->setChecked(obs_data_get_bool(settings_, key));
		connect(check, &QCheckBox::toggled, this, [this, prop, key](bool on) {
			obs_data_set_bool(settings_, key, on);
			commit(prop);
		});
		field = check;
		fullRow = true;
		break;
	}

	case OBS_PROPERTY_INT: {
		auto *spin = new QSpinBox;
		const int min = obs_property_int_min(prop);
		const int max = obs_property_int_max(prop);
		const int step = std::max(1, obs_property_int_step(prop));
		spin->setRange(min, max);
		spin->setSingleStep(step);
		spin->setSuffix(QString::fromUtf8(obs_property_int_suffix(prop)));
		spin->setValue(clampInt(obs_data_get_int(settings_, key)));
		connect(spin, &QSpinBox::valueChanged, this, [this, prop, key](int v) {
			obs_data_set_int(settings_, key, v);
			commit(prop);
		});
		field = spin;

		if (obs_property_int_type(prop) == OBS_NUMBER_SLIDER) {
			auto *slider = new QSlider(Qt::Horizontal);
			slider->setRange(min, max);
			slider->setSingleStep(step);
			slider->setPageStep(step);
			slider->setValue(spin->value());
			connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
			connect(spin, &QSpinBox::valueChanged, slider, [slider](int v) {
				QSignalBlocker block(slider);
				slider->setValue(v);
			});

			auto *row = new QWidget;
			auto *h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			h->addWidget(slider, 1);
			h->addWidget(spin);
			field = row;
		}
		break;
	}

	case OBS_PROPERTY_FLOAT: {
		auto *spin = new QDoubleSpinBox;
		const double min = obs_property_float_min(prop);
		const double max = obs_property_float_max(prop);
		const double step = obs_property_float_step(prop) > 0 ? obs_property_float_step(prop) : 0.01;
		spin->setDecimals(step < 1 ? std::min(6, (int)std::ceil(-std::log10(step))) : 2);
		spin->setRange(min, max);
		spin->setSingleStep(step);
		spin->setSuffix(QString::fromUtf8(obs_property_float_suffix(prop)));
		spin->setValue(obs_data_get_double(settings_, key));
		connect(spin, &QDoubleSpinBox::valueChanged, this, [this, prop, key](double v) {
			obs_data_set_double(settings_, key, v);
			commit(prop);
		});
		field = spin;

		if (obs_property_float_type(prop) == OBS_NUMBER_SLIDER) {
			// QSlider is integer-only: map [min, max] onto whole steps.
			const int steps = (int)std::min(1e6, std::round((max - min) / step));
			auto *slider = new QSlider(Qt::Horizontal);
			slider->setRange(0, std::max(1, steps));
			auto toTick = [min, step](double v) {
				return (int)std::round((v - min) / step);
			};
			slider->setValue(toTick(spin->value()));
			connect(slider, &QSlider::valueChanged, spin,
				[spin, min, step](int tick) { spin->setValue(min + tick * step); });
			connect(spin, &QDoubleSpinBox::valueChanged, slider, [slider, toTick](double v) {
				QSignalBlocker block(slider);
				slider->setValue(toTick(v));
			});

			auto *row = new QWidget;
			auto *h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			h->addWidget(slider, 1);
			h->addWidget(spin);
			field = row;
		}
		break;
	}

	case OBS_PROPERTY_TEXT: {
		const QString value = QString::fromUtf8(obs_data_get_string(settings_, key));
		switch (obs_property_text_type(prop)) {
		case OBS_TEXT_MULTILINE: {
			auto *edit = new QPlainTextEdit(value);
			edit->setMaximumHeight(90);
			connect(edit, &QPlainTextEdit::textChanged, this, [this, prop, key, edit] {
				obs_data_set_string(settings_, key, edit->toPlainText().toUtf8());
				commit(prop);
			});
			field = edit;
			break;
		}
		case OBS_TEXT_INFO: {
			auto *info = new QLabel(label);
			info->setWordWrap(true);
			field = info;
			fullRow = true;
			break;
		}
		default: {
			auto *edit = new QLineEdit(value);
			if (obs_property_text_type(prop) == OBS_TEXT_PASSWORD)
				edit->setEchoMode(QLineEdit::Password);
			connect(edit, &QLineEdit::editingFinished, this, [this, prop, key, edit] {
				obs_data_set_string(settings_, key, edit->text().toUtf8());
				commit(prop);
			});
			field = edit;
			break;
		}
		}
		break;
	}

	case OBS_PROPERTY_PATH: {
		auto *row = new QWidget;
		auto *h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		auto *edit = new QLineEdit(QString::fromUtf8(obs_data_get_string(settings_, key)));
		auto *browse = new QPushButton(tr("Browse"));
		h->addWidget(edit, 1);
		h->addWidget(browse);

		const obs_path_type type = obs_property_path_type(prop);
		const QString filter = QString::fromUtf8(obs_property_path_filter(prop));
		const QString defaultPath = QString::fromUtf8(obs_property_path_default_path(prop));

		auto apply = [this, prop, key, edit] {
			obs_data_set_string(settings_, key, edit->text().toUtf8());
			commit(prop);
		};
		connect(edit, &QLineEdit::editingFinished, this, apply);
		connect(browse, &QPushButton::clicked, this, [this, edit, type, filter, defaultPath, label, apply] {
			const QString start = edit->text().isEmpty() ? defaultPath : edit->text();
			QString picked;
			if (type == OBS_PATH_DIRECTORY)
				picked = QFileDialog::getExistingDirectory(this, label, start);
			else if (type == OBS_PATH_FILE_SAVE)
				picked = QFileDialog::getSaveFileName(this, label, start, filter);
			else
				picked = QFileDialog::getOpenFileName(this, label, start, filter);
			if (!picked.isEmpty()) {
				edit->setText(picked);
				apply();
			}
		});
		field = row;
		break;
	}

	case OBS_PROPERTY_LIST: {
		auto *combo = new QComboBox;
		const obs_combo_format format = obs_property_list_format(prop);
		const bool editable = obs_property_list_type(prop) == OBS_COMBO_TYPE_EDITABLE;
		combo->setEditable(editable);

		const size_t count = obs_property_list_item_count(prop);
		for (size_t i = 0; i < count; ++i) {
			const QString name = QString::fromUtf8(obs_property_list_item_name(prop, i));
			QVariant value;
			if (format == OBS_COMBO_FORMAT_INT)
				value = QVariant::fromValue<qlonglong>(obs_property_list_item_int(prop, i));
			else if (format == OBS_COMBO_FORMAT_FLOAT)
				value = obs_property_list_item_float(prop, i);
			else if (format == OBS_COMBO_FORMAT_STRING)
				value = QString::fromUtf8(obs_property_list_item_string(prop, i));
			combo->addItem(name, value);

			if (obs_property_list_item_disabled(prop, i)) {
				if (auto *model = qobject_cast<QStandardItemModel *>(combo->model()))
					model->item((int)i)->setEnabled(false);
			}
		}

		int index = -1;
		if (format == OBS_COMBO_FORMAT_INT)
			index = combo->findData(QVariant::fromValue<qlonglong>(obs_data_get_int(settings_, key)));
		else if (format == OBS_COMBO_FORMAT_FLOAT)
			index = combo->findData(obs_data_get_double(settings_, key));
		else if (format == OBS_COMBO_FORMAT_STRING)
			index = combo->findData(QString::fromUtf8(obs_data_get_string(settings_, key)));
		combo->setCurrentIndex(index);
		if (editable && format == OBS_COMBO_FORMAT_STRING)
			combo->setCurrentText(QString::fromUtf8(obs_data_get_string(settings_, key)));

		if (editable && format == OBS_COMBO_FORMAT_STRING) {
			connect(combo, &QComboBox::currentTextChanged, this, [this, prop, key](const QString &text) {
				obs_data_set_string(settings_, key, text.toUtf8());
				commit(prop);
			});
		} else {
			connect(combo, &QComboBox::currentIndexChanged, this, [this, prop, key, combo, format](int i) {
				if (i < 0)
					return;
				const QVariant v = combo->itemData(i);
				if (format == OBS_COMBO_FORMAT_INT)
					obs_data_set_int(settings_, key, v.toLongLong());
				else if (format == OBS_COMBO_FORMAT_FLOAT)
					obs_data_set_double(settings_, key, v.toDouble());
				else if (format == OBS_COMBO_FORMAT_STRING)
					obs_data_set_string(settings_, key, v.toString().toUtf8());
				commit(prop);
			});

			// Like OBS's own properties view: if the stored value matches no
			// item (e.g. a type with no default, such as Swipe's direction),
			// fall back to the first item and write it back.
			if (index < 0 && count > 0)
				combo->setCurrentIndex(0);
		}
		field = combo;
		break;
	}

	case OBS_PROPERTY_COLOR:
	case OBS_PROPERTY_COLOR_ALPHA: {
		const bool alpha = obs_property_get_type(prop) == OBS_PROPERTY_COLOR_ALPHA;
		auto *button = new QPushButton;
		styleColorButton(button, colorFromObs((uint32_t)obs_data_get_int(settings_, key), alpha));
		connect(button, &QPushButton::clicked, this, [this, prop, key, button, alpha, label] {
			const QColor current = colorFromObs((uint32_t)obs_data_get_int(settings_, key), alpha);
			const QColor picked = QColorDialog::getColor(current, this, label,
								     alpha ? QColorDialog::ShowAlphaChannel
									   : QColorDialog::ColorDialogOptions());
			if (!picked.isValid())
				return;
			obs_data_set_int(settings_, key, colorToObs(picked));
			styleColorButton(button, picked);
			commit(prop);
		});
		field = button;
		break;
	}

	case OBS_PROPERTY_BUTTON: {
		auto *button = new QPushButton(label);
		connect(button, &QPushButton::clicked, this, [this, prop] {
			if (obs_property_button_clicked(prop, source_))
				scheduleRebuild();
		});
		field = button;
		fullRow = true;
		break;
	}

	case OBS_PROPERTY_GROUP: {
		auto *box = new QGroupBox(label);
		auto *inner = new QFormLayout(box);
		if (obs_property_group_type(prop) == OBS_GROUP_CHECKABLE) {
			box->setCheckable(true);
			box->setChecked(obs_data_get_bool(settings_, key));
			connect(box, &QGroupBox::toggled, this, [this, prop, key](bool on) {
				obs_data_set_bool(settings_, key, on);
				commit(prop);
			});
		}
		obs_properties_t *content = obs_property_group_content(prop);
		for (obs_property_t *p = obs_properties_first(content); p; obs_property_next(&p))
			addProperty(p, inner);
		field = box;
		fullRow = true;
		break;
	}

	case OBS_PROPERTY_FONT:
		field = makeFontField(prop, key, label);
		break;

	case OBS_PROPERTY_EDITABLE_LIST:
		field = makeEditableListField(prop, key, label);
		break;

	case OBS_PROPERTY_FRAME_RATE:
		field = makeFrameRateField(prop, key);
		break;

	default:
		field = new QLabel(tr("(not supported)"));
		field->setEnabled(false);
		break;
	}

	field->setEnabled(field->isEnabled() && obs_property_enabled(prop));
	if (!tooltip.isEmpty())
		field->setToolTip(tooltip);

	if (fullRow)
		layout->addRow(field);
	else
		layout->addRow(label, field);
}

// ---- font -------------------------------------------------------------------

QWidget *PropertiesPanel::makeFontField(obs_property_t *prop, const QByteArray &key, const QString &label)
{
	OBSDataAutoRelease font = obs_data_get_obj(settings_, key);
	const QFont current = fontFromObs(font);

	auto *button = new QPushButton(describeFont(current));
	QFont shown = current;
	shown.setPointSize(std::min(current.pointSize() > 0 ? current.pointSize() : 12, 14));
	button->setFont(shown);

	connect(button, &QPushButton::clicked, this, [this, prop, key, label, button] {
		OBSDataAutoRelease existing = obs_data_get_obj(settings_, key);
		bool ok = false;
		const QFont picked = QFontDialog::getFont(&ok, fontFromObs(existing), this, label);
		if (!ok)
			return;
		OBSDataAutoRelease updated = fontToObs(picked);
		obs_data_set_obj(settings_, key, updated);
		button->setText(describeFont(picked));
		commit(prop);
	});
	return button;
}

// ---- editable list ----------------------------------------------------------

void PropertiesPanel::commitEditableList(obs_property_t *prop, const QByteArray &key, QListWidget *list)
{
	OBSDataArrayAutoRelease array = obs_data_array_create();
	for (int i = 0; i < list->count(); ++i) {
		QListWidgetItem *item = list->item(i);
		OBSDataAutoRelease entry = obs_data_create();
		obs_data_set_string(entry, "value", item->text().toUtf8().constData());
		obs_data_set_bool(entry, "selected", item->isSelected());
		obs_data_set_bool(entry, "hidden", item->isHidden());
		obs_data_array_push_back(array, entry);
	}
	obs_data_set_array(settings_, key, array);
	commit(prop);
}

QWidget *PropertiesPanel::makeEditableListField(obs_property_t *prop, const QByteArray &key, const QString &label)
{
	const obs_editable_list_type type = obs_property_editable_list_type(prop);
	const QString filter = QString::fromUtf8(obs_property_editable_list_filter(prop));
	const QString defaultPath = QString::fromUtf8(obs_property_editable_list_default_path(prop));

	auto *container = new QWidget;
	auto *h = new QHBoxLayout(container);
	h->setContentsMargins(0, 0, 0, 0);

	auto *list = new QListWidget;
	list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	list->setMaximumHeight(120);

	OBSDataArrayAutoRelease array = obs_data_get_array(settings_, key);
	const size_t count = obs_data_array_count(array);
	for (size_t i = 0; i < count; ++i) {
		OBSDataAutoRelease entry = obs_data_array_item(array, i);
		auto *item = new QListWidgetItem(QString::fromUtf8(obs_data_get_string(entry, "value")), list);
		item->setSelected(obs_data_get_bool(entry, "selected"));
		item->setHidden(obs_data_get_bool(entry, "hidden"));
	}

	auto *buttons = new QVBoxLayout;
	auto *addBtn = new QPushButton(tr("Add"));
	auto *editBtn = new QPushButton(tr("Edit"));
	auto *removeBtn = new QPushButton(tr("Remove"));
	auto *upBtn = new QPushButton(tr("Up"));
	auto *downBtn = new QPushButton(tr("Down"));
	for (QPushButton *b : {addBtn, editBtn, removeBtn, upBtn, downBtn})
		buttons->addWidget(b);
	buttons->addStretch();

	h->addWidget(list, 1);
	h->addLayout(buttons);

	auto save = [this, prop, key, list] {
		commitEditableList(prop, key, list);
	};

	auto addText = [this, list, label, save] {
		bool ok = false;
		const QString text =
			QInputDialog::getText(this, label, tr("Value:"), QLineEdit::Normal, QString(), &ok);
		if (ok && !text.trimmed().isEmpty()) {
			list->addItem(text.trimmed());
			save();
		}
	};
	auto addFiles = [this, list, label, filter, defaultPath, save] {
		const QStringList files = QFileDialog::getOpenFileNames(this, label, defaultPath, filter);
		if (files.isEmpty())
			return;
		list->addItems(files);
		save();
	};

	if (type == OBS_EDITABLE_LIST_TYPE_FILES_AND_URLS) {
		auto *menu = new QMenu(addBtn);
		menu->addAction(tr("Add files"), this, addFiles);
		menu->addAction(tr("Add path or URL"), this, addText);
		addBtn->setMenu(menu);
	} else if (type == OBS_EDITABLE_LIST_TYPE_FILES) {
		connect(addBtn, &QPushButton::clicked, this, addFiles);
	} else {
		connect(addBtn, &QPushButton::clicked, this, addText);
	}

	auto editItem = [this, list, label, type, filter, save](QListWidgetItem *item) {
		if (!item)
			return;
		QString value;
		if (type == OBS_EDITABLE_LIST_TYPE_FILES) {
			value = QFileDialog::getOpenFileName(this, label, item->text(), filter);
		} else {
			bool ok = false;
			value = QInputDialog::getText(this, label, tr("Value:"), QLineEdit::Normal, item->text(), &ok);
			if (!ok)
				value.clear();
		}
		if (!value.trimmed().isEmpty()) {
			item->setText(value.trimmed());
			save();
		}
	};
	connect(editBtn, &QPushButton::clicked, this, [list, editItem] { editItem(list->currentItem()); });
	connect(list, &QListWidget::itemDoubleClicked, this, editItem);

	connect(removeBtn, &QPushButton::clicked, this, [list, save] {
		const auto selected = list->selectedItems();
		if (selected.isEmpty())
			return;
		qDeleteAll(selected);
		save();
	});

	auto move = [list, save](int delta) {
		const int row = list->currentRow();
		const int target = row + delta;
		if (row < 0 || target < 0 || target >= list->count())
			return;
		QListWidgetItem *item = list->takeItem(row);
		list->insertItem(target, item);
		list->setCurrentItem(item);
		save();
	};
	connect(upBtn, &QPushButton::clicked, this, [move] { move(-1); });
	connect(downBtn, &QPushButton::clicked, this, [move] { move(1); });

	return container;
}

// ---- frame rate -------------------------------------------------------------

QWidget *PropertiesPanel::makeFrameRateField(obs_property_t *prop, const QByteArray &key)
{
	auto *combo = new QComboBox;

	// Named options first (e.g. "Same as output").
	const size_t optionCount = obs_property_frame_rate_options_count(prop);
	for (size_t i = 0; i < optionCount; ++i) {
		const QString name = QString::fromUtf8(obs_property_frame_rate_option_name(prop, i));
		const QString desc = QString::fromUtf8(obs_property_frame_rate_option_description(prop, i));
		combo->addItem(desc.isEmpty() ? name : desc, QStringLiteral("opt:") + name);
	}

	// Then common rates that fall within the property's allowed ranges.
	const size_t rangeCount = obs_property_frame_rate_fps_ranges_count(prop);
	auto inRange = [prop, rangeCount](const media_frames_per_second &fps) {
		if (rangeCount == 0)
			return true;
		const double v = fpsValue(fps);
		for (size_t i = 0; i < rangeCount; ++i) {
			if (v >= fpsValue(obs_property_frame_rate_fps_range_min(prop, i)) &&
			    v <= fpsValue(obs_property_frame_rate_fps_range_max(prop, i)))
				return true;
		}
		return false;
	};
	auto fpsData = [](const media_frames_per_second &fps) {
		return QStringLiteral("fps:%1/%2").arg(fps.numerator).arg(fps.denominator);
	};

	static const media_frames_per_second presets[] = {
		{120, 1}, {60, 1}, {60000, 1001}, {50, 1}, {48, 1}, {30, 1}, {30000, 1001},
		{25, 1},  {24, 1}, {24000, 1001}, {20, 1}, {15, 1}, {10, 1},
	};
	if (optionCount && rangeCount)
		combo->insertSeparator(combo->count());
	for (const auto &fps : presets) {
		if (rangeCount && inRange(fps))
			combo->addItem(describeFps(fps), fpsData(fps));
	}

	// Select the stored value, adding it if it isn't one of the presets.
	media_frames_per_second fps = {0, 0};
	const char *option = nullptr;
	obs_data_get_frames_per_second(settings_, key, &fps, &option);

	int index = -1;
	if (option && *option)
		index = combo->findData(QStringLiteral("opt:") + QString::fromUtf8(option));
	else if (media_frames_per_second_is_valid(fps)) {
		index = combo->findData(fpsData(fps));
		if (index < 0) {
			combo->addItem(describeFps(fps), fpsData(fps));
			index = combo->count() - 1;
		}
	}
	combo->setCurrentIndex(index);

	connect(combo, &QComboBox::currentIndexChanged, this, [this, prop, key, combo](int i) {
		if (i < 0)
			return;
		const QString data = combo->itemData(i).toString();
		if (data.startsWith(QStringLiteral("opt:"))) {
			const QByteArray name = data.mid(4).toUtf8();
			obs_data_set_frames_per_second(settings_, key, media_frames_per_second{0, 0}, name.constData());
		} else if (data.startsWith(QStringLiteral("fps:"))) {
			const QStringList parts = data.mid(4).split(QLatin1Char('/'));
			if (parts.size() != 2)
				return;
			const media_frames_per_second value{parts[0].toUInt(), parts[1].toUInt()};
			obs_data_set_frames_per_second(settings_, key, value, nullptr);
		}
		commit(prop);
	});

	if (combo->count() == 0) {
		combo->addItem(tr("(no frame rates available)"));
		combo->setEnabled(false);
	}
	return combo;
}
