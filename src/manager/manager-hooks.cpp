#include "manager-hooks.hpp"

#include <utility>

namespace ManagerHooks {

static Hooks &storage()
{
	static Hooks hooks;
	return hooks;
}

void set(Hooks hooks)
{
	storage() = std::move(hooks);
}

const Hooks &get()
{
	return storage();
}

bool hasRules()
{
	return static_cast<bool>(storage().openRules);
}

void openRules(obs_source_t *source)
{
	if (storage().openRules)
		storage().openRules(source);
}

QString usage(obs_source_t *source)
{
	if (!source || !storage().usage)
		return QString();
	return storage().usage(source);
}

} // namespace ManagerHooks
