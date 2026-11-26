#include "transition-pack.hpp"

#include "../actions/transition-actions.hpp"
#include "manager-hooks.hpp"
#include "transition-store.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QSaveFile>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <map>

namespace TransitionPack {

namespace {

const char *kFormat = "transition-manager-pack";
const QString kPackPrefix = QStringLiteral("$PACK/");

QString tr(const char *text)
{
	return QObject::tr(text);
}

// ---- a minimal zip: stored (uncompressed) entries, which suits media that is
// already compressed and keeps the format readable by any unzip tool.

quint32 crc32(const QByteArray &data)
{
	static quint32 table[256] = {};
	if (!table[1]) {
		for (quint32 i = 0; i < 256; ++i) {
			quint32 c = i;
			for (int k = 0; k < 8; ++k)
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
	}
	quint32 crc = 0xFFFFFFFFu;
	for (char ch : data)
		crc = table[(crc ^ (uchar)ch) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}

void put(QByteArray &b, quint32 v, int bytes)
{
	for (int i = 0; i < bytes; ++i)
		b.append(char((v >> (8 * i)) & 0xFF));
}

quint32 get(const QByteArray &b, qsizetype at, int bytes)
{
	quint32 v = 0;
	for (int i = bytes - 1; i >= 0; --i)
		v = (v << 8) | (uchar)b[at + i];
	return v;
}

using Files = QList<QPair<QString, QByteArray>>;

QByteArray zip(const Files &files)
{
	QByteArray out, dir;
	for (const auto &file : files) {
		const QByteArray n = file.first.toUtf8(), &data = file.second;
		const quint32 crc = crc32(data), offset = (quint32)out.size();
		auto header = [&](QByteArray &b, bool central) {
			put(b, central ? 0x02014b50 : 0x04034b50, 4);
			if (central)
				put(b, 20, 2); // made by
			put(b, 20, 2);         // needed
			put(b, 0x0800, 2);     // UTF-8 names
			put(b, 0, 2);          // stored
			put(b, 0, 2);          // time
			put(b, 0x21, 2);       // date: 1980-01-01
			put(b, crc, 4);
			put(b, (quint32)data.size(), 4);
			put(b, (quint32)data.size(), 4);
			put(b, (quint32)n.size(), 2);
			put(b, 0, 2); // extra
			if (central) {
				put(b, 0, 2 + 2 + 2 + 4); // comment, disk, attributes
				put(b, offset, 4);
			}
			b += n;
		};
		header(out, false);
		out += data;
		header(dir, true);
	}
	const quint32 dirOffset = (quint32)out.size();
	out += dir;
	put(out, 0x06054b50, 4);
	put(out, 0, 4);
	put(out, (quint32)files.size(), 2);
	put(out, (quint32)files.size(), 2);
	put(out, (quint32)dir.size(), 4);
	put(out, dirOffset, 4);
	put(out, 0, 2);
	return out;
}

bool unzip(const QByteArray &z, QHash<QString, QByteArray> &files, QString &error)
{
	error = tr("This isn't a transition pack, or it's damaged.");
	const qsizetype end = z.lastIndexOf(QByteArray("PK\x05\x06", 4));
	if (end < 0 || end + 22 > z.size())
		return false;
	qsizetype at = get(z, end + 16, 4);
	for (quint32 i = 0, count = get(z, end + 10, 2); i < count; ++i) {
		if (at + 46 > z.size() || get(z, at, 4) != 0x02014b50)
			return false;
		const quint32 method = get(z, at + 10, 2), size = get(z, at + 20, 4);
		const quint32 nameLen = get(z, at + 28, 2);
		const qsizetype local = get(z, at + 42, 4);
		const QString name = QString::fromUtf8(z.mid(at + 46, nameLen));
		at += 46 + nameLen + get(z, at + 30, 2) + get(z, at + 32, 2);
		if (name.endsWith(QLatin1Char('/')))
			continue;
		if (method != 0) {
			error = tr("“%1” in the pack is compressed. Packs keep their files uncompressed; export the "
				   "pack from Transition Manager again.")
					.arg(name);
			return false;
		}
		if (local + 30 > z.size())
			return false;
		const qsizetype data = local + 30 + get(z, local + 26, 2) + get(z, local + 28, 2);
		if (data + size > z.size())
			return false;
		files.insert(name, z.mid(data, size));
	}
	return true;
}

QJsonObject toJson(obs_data_t *data)
{
	return QJsonDocument::fromJson(QByteArray(obs_data_get_json(data))).object();
}

obs_data_t *fromJson(const QJsonObject &object)
{
	return obs_data_create_from_json(QJsonDocument(object).toJson(QJsonDocument::Compact).constData());
}

// Applies `f` to every string in a JSON tree.
QJsonValue mapStrings(const QJsonValue &v, const std::function<QJsonValue(const QString &)> &f)
{
	if (v.isObject()) {
		QJsonObject o = v.toObject();
		for (auto it = o.begin(); it != o.end(); ++it)
			*it = mapStrings(*it, f);
		return o;
	}
	if (v.isArray()) {
		QJsonArray a = v.toArray();
		for (QJsonValueRef e : a)
			e = mapStrings(e, f);
		return a;
	}
	return v.isString() ? f(v.toString()) : v;
}

} // namespace

void exportPack(QWidget *parent, obs_source_t *preselect)
{
	auto &store = TransitionStore::instance();
	const auto &hooks = ManagerHooks::get();

	QDialog dialog(parent);
	dialog.setWindowTitle(tr("Export Transition Pack"));
	auto *list = new QListWidget;
	for (const OBSSource &s : store.transitions()) {
		if (!obs_source_configurable(s))
			continue; // built in: every OBS has them
		auto *item = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(s)), list);
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		item->setCheckState(s.Get() == preselect ? Qt::Checked : Qt::Unchecked);
	}
	auto *rules = new QCheckBox(tr("Include Transition Tree presets that use them"));
	rules->setChecked(true);
	rules->setVisible(static_cast<bool>(hooks.exportRules));
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto *v = new QVBoxLayout(&dialog);
	v->addWidget(new QLabel(tr("Transitions to include, with their media, durations and actions:")));
	v->addWidget(list, 1);
	v->addWidget(rules);
	v->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;

	std::vector<std::string> names;
	for (int i = 0; i < list->count(); ++i)
		if (list->item(i)->checkState() == Qt::Checked)
			names.push_back(list->item(i)->text().toStdString());
	if (names.empty())
		return;

	const QString path = QFileDialog::getSaveFileName(parent, dialog.windowTitle(), QString(),
							  tr("Transition packs (*.tmpack)"));
	if (path.isEmpty())
		return;

	Files files;
	QHash<QString, QString> media; // file on disk → path in the pack
	auto packFile = [&](const QString &s) -> QJsonValue {
		if (!QDir::isAbsolutePath(s) || !QFileInfo(s).isFile())
			return s;
		if (!media.contains(s)) {
			QFile f(s);
			if (!f.open(QIODevice::ReadOnly))
				return s;
			const QString name =
				QStringLiteral("media/%1_%2").arg(media.size()).arg(QFileInfo(s).fileName());
			files.append({name, f.readAll()});
			media.insert(s, name);
		}
		return kPackPrefix + media.value(s);
	};

	QJsonArray transitions;
	for (const OBSSource &s : store.transitions()) {
		const std::string name = obs_source_get_name(s);
		if (std::find(names.begin(), names.end(), name) == names.end())
			continue;
		OBSDataAutoRelease settings = obs_source_get_settings(s);
		OBSDataAutoRelease actions = TransitionActions::save(TransitionActions::forTransition(name));
		transitions.append(QJsonObject{{"name", QString::fromStdString(name)},
					       {"type", QString::fromUtf8(obs_source_get_id(s))},
					       {"settings", mapStrings(toJson(settings), packFile)},
					       {"duration", store.defaultDuration(s)},
					       {"actions", toJson(actions)}});
	}

	QJsonObject root{{"format", kFormat}, {"version", 1}, {"transitions", transitions}};
	if (rules->isChecked() && hooks.exportRules) {
		const std::string json = hooks.exportRules(names);
		if (!json.empty())
			root.insert("rules", QJsonDocument::fromJson(QByteArray::fromStdString(json)).object());
	}
	files.prepend({QStringLiteral("pack.json"), QJsonDocument(root).toJson()});

	QSaveFile out(path);
	if (!out.open(QIODevice::WriteOnly) || out.write(zip(files)) < 0 || !out.commit()) {
		QMessageBox::warning(parent, dialog.windowTitle(), tr("The pack couldn't be saved."));
		return;
	}
	QMessageBox::information(
		parent, dialog.windowTitle(),
		tr("Saved %1 transitions and %2 media files.").arg(transitions.size()).arg(media.size()));
}

void importPack(QWidget *parent)
{
	const QString title = tr("Import Transition Pack");
	const QString path =
		QFileDialog::getOpenFileName(parent, title, QString(), tr("Transition packs (*.tmpack *.zip)"));
	if (path.isEmpty())
		return;

	auto &store = TransitionStore::instance();
	if (!store.canEditList()) {
		QMessageBox::warning(parent, title,
				     tr("OBS's transition list couldn't be accessed, so transitions "
					"can't be added."));
		return;
	}

	QFile file(path);
	QHash<QString, QByteArray> files;
	QString error;
	if (!file.open(QIODevice::ReadOnly) || !unzip(file.readAll(), files, error)) {
		QMessageBox::warning(parent, title, error.isEmpty() ? tr("The pack couldn't be read.") : error);
		return;
	}
	const QJsonObject root = QJsonDocument::fromJson(files.value(QStringLiteral("pack.json"))).object();
	if (root.value("format").toString() != QLatin1String(kFormat)) {
		QMessageBox::warning(parent, title, tr("This isn't a transition pack, or it's damaged."));
		return;
	}

	// Media goes next to the plugin's config, one folder per pack.
	char *config = obs_module_config_path("packs");
	const QString dir = QDir(QString::fromUtf8(config)).filePath(QFileInfo(path).completeBaseName());
	bfree(config);
	QDir().mkpath(dir);
	auto unpackFile = [&](const QString &s) -> QJsonValue {
		if (!s.startsWith(kPackPrefix))
			return s;
		const QString inPack = s.mid(kPackPrefix.size());
		const QString target = QDir(dir).filePath(QFileInfo(inPack).fileName()); // never outside `dir`
		if (files.contains(inPack) && !QFileInfo::exists(target)) {
			QFile f(target);
			if (f.open(QIODevice::WriteOnly))
				f.write(files.value(inPack));
		}
		return target;
	};

	int added = 0;
	QStringList skipped;
	std::map<std::string, std::string> renames;
	const QJsonArray packed = root.value("transitions").toArray();
	for (const QJsonValue value : packed) {
		const QJsonObject t = value.toObject();
		const QString name = t.value("name").toString();
		const QByteArray type = t.value("type").toString().toUtf8();
		if (!obs_source_get_display_name(type.constData())) {
			skipped << tr("%1 (its type isn't installed)").arg(name);
			continue;
		}
		OBSDataAutoRelease settings = fromJson(mapStrings(t.value("settings"), unpackFile).toObject());
		obs_source_t *source = store.createTransition(type.constData(), store.uniqueName(name), settings);
		if (!source) {
			skipped << name;
			continue;
		}
		const std::string newName = obs_source_get_name(source);
		if (newName != name.toStdString())
			renames[name.toStdString()] = newName;
		if (t.value("duration").toInt() > 0)
			store.setDefaultDuration(source, t.value("duration").toInt());
		OBSDataAutoRelease actions = fromJson(t.value("actions").toObject());
		TransitionActions::setForTransition(newName, TransitionActions::load(actions));
		++added;
	}
	TransitionActions::refreshHooks();

	int presets = 0;
	const auto &hooks = ManagerHooks::get();
	if (root.contains("rules") && hooks.importRules)
		presets = hooks.importRules(QJsonDocument(root.value("rules").toObject()).toJson().toStdString(),
					    renames);

	QString message = tr("Added %1 transitions").arg(added);
	if (presets > 0)
		message += tr(" and %1 Transition Tree presets").arg(presets);
	message += QStringLiteral(".");
	if (!skipped.isEmpty())
		message += QStringLiteral("\n\n") + tr("Skipped: %1").arg(skipped.join(QStringLiteral(", ")));
	QMessageBox::information(parent, title, message);
}

} // namespace TransitionPack
