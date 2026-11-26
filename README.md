# Transition Manager for OBS Studio

A **Transition Manager** dock that works like OBS's own Scene Transitions dock,
plus a full **Transition Manager window** for creating, editing, previewing,
duplicating, renaming, reordering and removing scene transitions.

It also includes **[Transition Tree](docs/transition-tree.md)**: rules that
choose which transition plays for each scene switch, with presets, several
transitions per rule, hotkeys and obs-websocket control. The manager is about
*what* your transitions are; Transition Tree is about *when* each one plays.

Targets OBS Studio 33+ and Qt 6. Built on the
[obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate).

## The dock

Enable it from **Docks → Transition Manager**. It mirrors OBS's Scene
Transitions dock:

- A dropdown to pick the active transition and a **Duration** box (hidden for
  fixed-length transitions, as in OBS).
- **+** asks for a name, adds the transition, makes it active and opens it in
  the Transition Manager window. That window is where its settings live.
- **Trash** removes the active transition after switching OBS to Fade (or
  another transition). Built-in Cut and Fade can't be removed.
- **⋮** has Rename, Duplicate, Properties (opens the window on this
  transition) and Open Transition Manager.
- **Open Transition Manager** opens the full window on the active transition.
  It's also in **Tools → Transition Manager**.

## The Transition Manager window

A sidebar lists every transition; the rest of the window edits the selected
one, with the preview next to its settings so you see each change as you
make it.

- **Sidebar.** Built-in transitions (Cut, Fade) are grouped separately with a
  lock, since they can't be removed. Custom transitions follow, in OBS's order.
  The active transition has a green **Active** badge, and transitions with
  their own duration show it as a chip. Filter by name or type, drag custom
  transitions to reorder them (OBS's dropdown follows), and use **New** to add
  one. Right-click a transition for its actions.
- **One list, shared with OBS.** These are exactly the transitions in OBS's
  Scene Transitions dropdown. Transitions created here go into that dropdown
  the same way OBS's **+** button adds them. OBS saves them with the scene
  collection, and they work everywhere OBS uses transitions: scene switches,
  studio mode, quick transitions and per-scene overrides.
- **Header.** The name with a rename button, its type and whether it's
  active, **Duplicate**, **Make active** (or an *Active in OBS* label), and a
  **⋮** menu with Rename, Duplicate and Remove.
- **Live preview.** A private copy of the transition that you can play, loop
  or scrub, with the elapsed and total time. It never touches the transition
  driving your program output. Preview between two test colors, or from the
  program scene to the preview scene (outside studio mode: the next scene in
  the list). For a Stinger, the length comes from its video and the scrubber
  marks where the scene cuts.
- **Duration.** Choose **OBS default** or **Custom**. A custom duration is
  applied to OBS whenever the transition becomes active, and saved per scene
  collection. Fixed-length transitions (Cut, Stinger) explain their length
  instead.
- **Settings.** A form built from the transition's own properties, with labels
  above fields. Every OBS property type is supported, including fonts, editable
  lists, frame rates and sliders. Fields that appear or hide based on other
  settings update as you edit. Luma Wipe's image is picked from thumbnails of
  the actual wipe images. Changes apply immediately and are mirrored into the
  preview.
- **Stinger transition point.** **Detect transition point** plays the video
  privately and measures how much of the screen each frame covers (its alpha,
  or the track matte's brightness, inverted if the matte is). It suggests the
  middle of the stretch where it covers the most, in the Stinger's own unit
  (ms or frames), says whether the video has sound, and **Use it** applies it.
- **Actions.** Run something when a transition starts or ends: mute, unmute or
  toggle mute, set a source's volume, show or hide a source, restart a media
  source, or press any OBS or source hotkey, each with an optional delay.
  **Edit…** sets the selected transition's actions, **Global actions…** those
  of every transition, and each Transition Tree rule has its own. When a
  transition plays, the global actions run, then the transition's, then those
  of the rule that picked it. A transition or rule can **replace broader
  actions** instead. The preview never runs actions.
- **Transition packs.** **⋮ → Export Pack…** saves the chosen transitions with
  their media (Stinger videos, mattes, images), custom durations and actions,
  and optionally the Transition Tree presets that use them, as one `.tmpack`
  file (an uncompressed zip). **Import Pack…** (in **⋮** or **New**) copies the
  media into the plugin's config folder (`packs/<pack name>/`), adds the
  transitions under unique names and points imported presets at them.
- **Theme-aware.** Colors and icons come from the current OBS theme, light or
  dark, and update when you switch themes.

### Rules

- Removing follows OBS's own rules: built-in transitions without settings (Cut,
  Fade) can't be removed. In the window, the active transition can't be removed
  either, so switch away from it first. The dock's trash button switches away
  for you, like OBS's.
- Names must be unique, as in OBS.

## Transition Tree

Open it from **Tools → Transition Tree**. Its full documentation, including
the rule-matching order and the obs-websocket API, is in
[docs/transition-tree.md](docs/transition-tree.md).

The two parts are independent, but each is aware of the other:

- **From a rule to its transitions.** Each transition in a rule has an
  **✎** button that opens it in the Transition Manager window. **New
  Transition…** picks a type, creates it the same way the manager does, opens
  its settings in the manager and adds it to the selected rule. The editor's
  bottom bar has a **Transition Manager** button.
- **Durations.** A transition added to a rule starts with the custom duration
  set for it in the manager. Switching a rule's transition carries the new
  one's duration over too, unless you typed a duration yourself.
- **Missing transitions.** A rule that names a transition that no longer exists
  shows a warning and skips it. Its **✎** button then creates a replacement
  (suggesting the old name) and points every rule that used the old name at it.
- **Preview a switch.** In **Which Rule Wins?**, **▶ Preview** plays the
  winning rule's transition in the manager's preview, between the two scenes
  you picked and at the rule's duration. With several transitions you choose
  which. The scene pair stays available as a third **Preview between**
  option, and as everywhere in the preview, program output is never touched.
- **From a transition to its rules.** The manager's header says how many
  Transition Tree rules and preset defaults use the selected transition, and
  so does the removal prompt. **Rules Using This Transition…** in the window's
  and dock's **⋮** menus opens Transition Tree filtered to that transition.
- **Renames are followed.** Renaming a transition, here or in OBS, updates every
  rule and preset default that used the old name.

### Moving from the standalone Transition Tree plugin

Remove the standalone `transition-tree` plugin. Everything carries over:
local presets, hotkeys and settings stay in the scene collection under the
same key, the obs-websocket vendor is still `transition-tree`, and global
presets are copied from the old plugin's config folder on first start.

While the standalone plugin is still installed, the copy included here stays
off so the two don't fight over the same hotkeys, scene overrides and
websocket vendor; a warning in the OBS log says so.

## Layout

The source is split so each part can be read and changed on its own.
`manager/` and `tree/` never include each other (both may use the shared
`actions/`); `bridge/` is the only code
that knows both, and talks to them through optional hooks
(`manager/manager-hooks.hpp` and `tt::Integration` in `tree/transition-tree.hpp`).

| Path | Role |
|---|---|
| `src/plugin-main.cpp` | Module entry. Loads the manager, then (after every module has loaded) Transition Tree and the bridge |
| `src/bridge/` | Fills in each part's hooks so they link up (see above) |
| `src/tree/` | Transition Tree: rule model and serialization (`-model`), the override engine (`-engine`), editor (`-dialog`), obs-websocket vendor (`-websocket`) and its module lifecycle, hooks and rename tracking (`-integration`) |
| `src/manager/manager-module.*` | The manager's lifecycle: starts the store, creates the window, registers the dock and the Tools menu item |
| `src/manager/manager-hooks.*` | Optional extension points other parts can fill in |
| `src/manager/transitions-dock.*` | The compact dock modelled on OBS's Scene Transitions dock |
| `src/manager/transition-manager-window.*` | The full window: sidebar list, header actions, preview, duration and settings |
| `src/manager/transition-list.*` | Item roles and painting for the sidebar list (sections, type icons, badges) |
| `src/manager/tm-style.*` | Palette-derived colors, SVG icons and the window style sheet |
| `src/manager/timeline-slider.hpp` | Scrub slider that can mark a Stinger's transition point |
| `src/manager/stinger-analyzer.*` | Plays a Stinger's video privately and finds its transition point from the frames' coverage |
| `src/manager/transition-pack.*` | Exports and imports `.tmpack` transition packs (a minimal stored zip) |
| `src/actions/` | Start and end actions: the model, the editor, and the runtime that hooks every transition, saved under the `transition_actions` key of the scene collection. Shared by the manager and Transition Tree, and depends on neither |
| `src/manager/media-probe.*` | Measures a Stinger video's length and frame count the same way the Stinger does |
| `src/manager/transition-dialogs.*` | Name, rename and remove prompts shared by the dock and the window |
| `src/manager/transition-store.*` | Reads and edits OBS's transition list, hooks transitions it creates into OBS's scene-switch signals, and saves per-transition durations under the `transition_manager` key of the scene collection |
| `src/manager/obs-qt-types.hpp` | Declares the `OBSSource` Qt metatype under the same name OBS uses, so entries added here are readable by OBS |
| `src/manager/transition-preview.*` | Native `obs_display` that renders a private copy of the selected transition between test colors or real scenes |
| `src/manager/properties-panel.*` | Builds Qt widgets from `obs_properties_t` and writes changes back |

## Building

Use the template's presets, which enable the frontend API and Qt:

```
cmake --preset ubuntu-x86_64      # or macos / windows-x64
cmake --build --preset ubuntu-x86_64
```

On Ubuntu 26.04 you can also build against OBS Studio 33 from the OBS PPA
(`obs-studio`, `qt6-base-dev`, `qt6-base-private-dev`):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

Transition Tree is included by default. To build the manager alone, configure
with `-DENABLE_TRANSITION_TREE=OFF`.

Then install it with `cmake --install build --prefix /usr`, which places the module in
`/usr/lib/<arch>/obs-modules/plugins/` and its data in `/usr/share/obs/obs-modules/plugins/obs-transition-manager/`.

The supported build environments, GitHub Actions workflows, release tagging and
macOS signing work exactly as in the plugin template. See the
[Plugin Template Wiki](https://github.com/obsproject/obs-plugintemplate/wiki).

## Known limitations

- **Relies on OBS's widget layout.** No public API adds a transition to OBS's
  list, so the store finds the main window's `transitions` combo box and checks
  it matches what `obs_frontend_get_transitions` reports before writing to it.
  If a future OBS version changes this, the window shows a note, and adding,
  duplicating, renaming, removing and reordering are turned off in both the
  dock and the window. Settings, preview and switching keep
  working.
- **No list-changed event for others.** OBS doesn't let plugins raise
  `OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED`, so other plugins (and
  obs-websocket clients) aren't told when this plugin adds, renames, removes or
  reorders a transition. OBS's own menus read the list when they open, so they
  are unaffected.
- **Quick transitions.** Removing a transition used by a quick-transition button
  leaves that button working with the removed transition until OBS restarts.
- **Packs on OBS 32+.** OBS asks for each imported transition's name itself,
  as it does for every new transition there.
- **Rule actions follow the rule that armed the switch.** They run when the
  switch uses the transition the rule picked. A quick transition or another
  plugin's override runs only the global and transition actions.
- **Durations are stored by name,** the same way OBS stores per-scene transition
  overrides. Renaming in this plugin carries the duration over. Renaming in OBS's
  own dialog doesn't.
- **Wayland.** The preview surface is wired for Windows, macOS and Linux/X11.
  On Wayland the preview stays blank (everything else works).
- **Scene preview.** The preview copy is marked active, as in OBS's own
  transition properties preview, so media-based transitions such as Stinger
  actually play. In "Program → Preview scene" mode the target scene therefore
  counts as active while it's in the preview. Media sources in it that are set
  to restart when activated can restart.
