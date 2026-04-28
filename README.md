# render-unfocused-fix

`render-unfocused-fix` is a Hyprland plugin that performs offscreen rendering for selected windows without drawing any visible overlay on screen.

The plugin keeps its own set of `CFramebuffer` snapshots for windows that match the conditions below and refreshes those snapshots on a timer. The result is "virtual" rendering only: the windows are rendered offscreen, but nothing is composited into the final monitor image.

## What the plugin does

- scans all current windows;
- selects every window that should be treated as `renderUnfocused`;
- ignores windows that are on the currently active workspace of the focused monitor;
- renders every selected window into its own offscreen framebuffer;
- refreshes those framebuffers at the rate defined by Hyprland's global `misc:render_unfocused_fps` setting;
- rebuilds or refreshes snapshots when windows open, close, move between workspaces, change active state, update window rules, when the active workspace changes, when the focused monitor changes, and when the config is reloaded.

The plugin does **not**:

- draw previews, thumbnails, pixels, or overlays on screen;
- change focus;
- move windows;
- create its own config keywords.

## Window selection rules

A window is rendered by the plugin only if all of the following are true:

- the window exists and is mapped;
- the window has a workspace and a monitor;
- the window is not on a special workspace;
- the window's `WindowRuleApplicator` reports `renderUnfocused() == true`;
- the window is **not** on the currently active workspace of the focused monitor.

In practice this means the plugin follows the effective window-rule state, not a hardcoded class list and not fullscreen state by itself.

## Ordering

When multiple windows match, the plugin keeps snapshots for all of them. The internal order is:

1. windows on the focused monitor first;
2. then lower workspace id first;
3. then by class;
4. then by title.

This order matters only for deterministic internal bookkeeping. Since nothing is drawn on screen, it currently has no visual effect.

## Refresh rate

Snapshot refresh rate is derived from:

```ini
misc {
    render_unfocused_fps = 15
}
```

The plugin reads `misc:render_unfocused_fps` directly and updates its timer again after config reload.

## Building

build it with:

```bash
make
```

The resulting plugin file is:

```bash
./render-unfocused-fix.so
```

Example manual load:

```bash
hyprctl plugin load {FULL_PATH}/render-unfocused-fix.so
```

Example `hyprland.conf` entry:

```ini
exec-once = hyprctl plugin load {FULL_PATH}/render-unfocused-fix.so
```

The plugin should be built against a Hyprland version compatible with the headers and plugin API available in your current environment.

## NixOS

In `flake.nix`:

```nix
{
  inputs = {
    render-unfocused-fix = {
      url = "github:yayuuu/hyprland-render-unfocused-fix";
      inputs.hyprland.follows = "hyprland";
    };
  };
}
```

Then add the plugin via the Hyprland home-manager module:

```nix
wayland.windowManager.hyprland = {
  enable = true;
  plugins = [
    inputs.render-unfocused-fix.packages.${pkgs.system}.default
  ];
};
```

## Example rule

Example rule that enables the plugin for fullscreen windows:

```ini
windowrule {
    name = render-unfocused-fullscreen
    match:fullscreen = true

    render_unfocused = on
}
```

Important: the plugin does not require fullscreen by itself. Fullscreen is only one possible rule condition. Any rule that results in `renderUnfocused == true` for a window is enough, as long as that window is not on the current workspace.

## Current limitation

This plugin assumes that forcing a qualifying window through an offscreen render path is enough to keep it updated in the background. If the underlying client or Hyprland rendering path behaves differently than expected, a window may still not behave exactly the same as when it is visibly composited on screen.
