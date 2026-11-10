# Transition Manager for OBS Studio

A dock for creating, editing, previewing, duplicating, renaming, reordering and
removing OBS Studio scene transitions, without opening OBS's transition
properties window.

Targets OBS 30+ (uses `obs_frontend_add_dock_by_id`) and Qt 6. Built on the
[obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate).

## Features

- **One list, shared with OBS.** The dock shows exactly the transitions in OBS's
  Scene Transitions dropdown, in the same order. Transitions created here go
  into that dropdown the same way OBS's **+** button adds them. OBS saves them
  with the scene collection, and they work everywhere OBS uses transitions:
  scene switches, studio mode, quick transitions and per-scene overrides.
- **Live preview.** Each selected transition gets a private copy that you can
  play or scrub with the slider. The preview never touches the transition
  driving your program output. It can run between two test colors, or from your
  program scene to your preview scene (outside studio mode: the next scene in
  the list).
- **Settings editor.** The dock builds a form from the transition's own
  properties. Every OBS property type is supported, including fonts, editable
  lists, frame rates and sliders. Fields that appear or hide based on other
  settings (Stinger, Luma Wipe, …) update as you edit. Changes apply to the real
  transition immediately and are mirrored into the preview.
- **New / Duplicate / Rename / Remove / Set active.** Available from the buttons
  or the right-click menu, and double-clicking a transition sets it active.
  Duplicating deep-copies its settings.
- **Search and reorder.** Filter by name or type, and drag to reorder. OBS's
  dropdown follows the new order.
- **Per-transition durations.** Tick **Own duration** to remember a duration
  for the selected transition. Whenever that transition becomes active, OBS's
  duration box is set to it. Saved per scene collection.

### Rules

- Removing follows OBS's own rules: built-in transitions without settings (Cut,
  Fade) can't be removed. The active transition can't be removed either, so
  switch away from it first.
- Names must be unique, as in OBS.

## Layout

| File | Role |
|---|---|
| `src/plugin-main.cpp` | Module entry. Registers the dock and starts the store |
| `src/transition-store.*` | Reads and edits OBS's transition list, hooks transitions it creates into OBS's scene-switch signals, and saves per-transition durations under the `transition_manager` key of the scene collection |
| `src/obs-qt-types.hpp` | Declares the `OBSSource` Qt metatype under the same name OBS uses, so entries added here are readable by OBS |
| `src/transition-preview.*` | Native `obs_display` that renders a private copy of the selected transition between test colors or real scenes |
| `src/properties-panel.*` | Builds Qt widgets from `obs_properties_t` and writes changes back |
| `src/transition-manager-dock.*` | The dock: preview, list, actions and settings |

## Building

Use the template's presets, which enable the frontend API and Qt:

```
cmake --preset ubuntu-x86_64      # or macos / windows-x64
cmake --build --preset ubuntu-x86_64
```

On Ubuntu 24.04 you can also build against the distro packages
(`libobs-dev`, `qt6-base-dev`, `qt6-base-private-dev`):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

Then copy the module into your OBS plugins folder, e.g. on Linux
`~/.config/obs-studio/plugins/obs-transition-manager/bin/64bit/obs-transition-manager.so`.

The supported build environments, GitHub Actions workflows, release tagging and
macOS signing work exactly as in the plugin template. See the
[Plugin Template Wiki](https://github.com/obsproject/obs-plugintemplate/wiki).

## Known limitations

- **Relies on OBS's widget layout.** No public API adds a transition to OBS's
  list, so the store finds the main window's `transitions` combo box and checks
  it matches what `obs_frontend_get_transitions` reports before writing to it.
  If a future OBS version changes this, the dock shows a note and turns off
  New/Duplicate/Rename/Remove/reorder. Settings, preview and switching keep
  working.
- **No list-changed event for others.** OBS doesn't let plugins raise
  `OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED`, so other plugins (and
  obs-websocket clients) aren't told when this dock adds, renames, removes or
  reorders a transition. OBS's own menus read the list when they open, so they
  are unaffected.
- **Quick transitions.** Removing a transition used by a quick-transition button
  leaves that button working with the removed transition until OBS restarts.
- **Durations are stored by name,** the same way OBS stores per-scene transition
  overrides. Renaming in this dock carries the duration over. Renaming in OBS's
  own dialog doesn't.
- **Wayland.** The preview surface is wired for Windows, macOS and Linux/X11.
  On Wayland the preview stays blank (everything else works).
- **Scene preview.** The preview copy is marked active, as in OBS's own
  transition properties preview, so media-based transitions such as Stinger
  actually play. In "Program → Preview scene" mode the target scene therefore
  counts as active while it's in the preview. Media sources in it that are set
  to restart when activated can restart.
