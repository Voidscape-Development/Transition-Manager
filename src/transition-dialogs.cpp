#include "transition-dialogs.hpp"
#include "transition-store.hpp"

#include <QCoreApplication>
#include <QInputDialog>
#include <QMessageBox>

static QString tr(const char *text)
{
	return QCoreApplication::translate("TransitionDialogs", text);
}

namespace TransitionDialogs {

QString askName(QWidget *parent, const QString &title, const QString &initial, obs_source_t *ignore)
{
	auto &store = TransitionStore::instance();
	QString name = initial;
	for (;;) {
		bool ok = false;
		name = QInputDialog::getText(parent, title, tr("Name:"), QLineEdit::Normal, name, &ok);
		if (!ok)
			return QString();
		name = name.trimmed();
		if (name.isEmpty()) {
			QMessageBox::warning(parent, title, tr("Please enter a name."));
			continue;
		}
		if (store.nameTaken(name, ignore)) {
			QMessageBox::warning(parent, title, tr("A transition named \"%1\" already exists.").arg(name));
			continue;
		}
		return name;
	}
}

bool rename(QWidget *parent, obs_source_t *source)
{
	if (!source)
		return false;
	const QString name =
		askName(parent, tr("Rename transition"), QString::fromUtf8(obs_source_get_name(source)), source);
	return !name.isEmpty() && TransitionStore::instance().renameTransition(source, name);
}

bool remove(QWidget *parent, obs_source_t *source, bool allowActive)
{
	auto &store = TransitionStore::instance();
	if (!source || !store.canRemove(source, allowActive))
		return false;

	const auto answer = QMessageBox::question(
		parent, tr("Remove transition"),
		tr("Remove \"%1\"? This can't be undone.\n\nQuick transition buttons that use it keep working until "
		   "OBS restarts.")
			.arg(QString::fromUtf8(obs_source_get_name(source))));
	return answer == QMessageBox::Yes && store.removeTransition(source, allowActive);
}

} // namespace TransitionDialogs
