#pragma once

class TransitionManagerWindow;

// Lifecycle of the Transition Manager component: the store, the window, the
// dock and the Tools menu entry. Independent of any other component.
namespace ManagerModule {

// Call from obs_module_load(). Returns false if the component couldn't start.
bool load();
void unload();

// The Transition Manager window, or nullptr before load().
TransitionManagerWindow *window();

} // namespace ManagerModule
