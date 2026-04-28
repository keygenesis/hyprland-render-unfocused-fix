{
  lib,
  hyprland,
  hyprlandPlugins,
}:
hyprlandPlugins.mkHyprlandPlugin hyprland {
  pluginName = "render-unfocused-fix";
  version = "0.1.0";
  src = ./.;

  nativeBuildInputs = [];

  buildInputs = [];

  meta = with lib; {
    description = "Hyprland plugin that offscreen-renders render_unfocused windows outside the active workspace";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
