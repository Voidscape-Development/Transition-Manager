#include "transition-dialogs.hpp"
#include "manager-hooks.hpp"
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
	auto &store = TransitionStore::instance();
	if (store.obsPrompts())
		return store.renameTransition(source, QString()); // OBS asks for the name
	const QString name =
		askName(parent, tr("Rename transition"), QString::fromUtf8(obs_source_get_name(source)), source);
	return !name.isEmpty() && store.renameTransition(source, name);
}

bool remove(QWidget *parent, obs_source_t *source, bool allowActive)
{
	auto &store = TransitionStore::instance();
	if (!source || !store.canRemove(source, allowActive))
		return false;
	const QString name = QString::fromUtf8(obs_source_get_name(source));
	const QString usage = ManagerHooks::usage(source);

	if (store.obsPrompts()) {
		// OBS confirms the removal itself, but doesn't know what else uses
		// the transition, so warn about that first.
		if (!usage.isEmpty()) {
			const auto answer = QMessageBox::question(
				parent, tr("Remove transition"),
				tr("%1.\n\nRemove \"%2\" anyway? Transition Tree keeps referring to "
				   "it by name; while it's missing, OBS uses its current transition instead.")
					.arg(usage, name));
			if (answer != QMessageBox::Yes)
				return false;
		}
		return store.removeTransition(source, allowActive);
	}

	QString text = tr("Remove \"%1\"? This can't be undone.\n\nQuick transition buttons that use it keep working "
			  "until OBS restarts.")
			       .arg(name);
	if (!usage.isEmpty())
		text += QStringLiteral("\n\n") +
			tr("%1. Transition Tree keeps referring to it by name; while it's missing, OBS uses its current "
			   "transition instead.")
				.arg(usage);

	const auto answer = QMessageBox::question(parent, tr("Remove transition"), text);
	return answer == QMessageBox::Yes && store.removeTransition(source, allowActive);
}

} // namespace TransitionDialogs
