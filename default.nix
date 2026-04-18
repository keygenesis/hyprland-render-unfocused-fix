{
  lib,
  hyprland,
  hyprlandPlugins,
}:
hyprlandPlugins.mkHyprlandPlugin {
  pluginName = "render-unfocused-fix";
  version = "0.1";
  src = ./.;

  inherit (hyprland) nativeBuildInputs;

  meta = with lib; {
    homepage = "https://github.com/hyprwm/hyprland-plugins/tree/main/render-unfocused-fix";
    description = "Renders a 1x1 indicator from a fullscreen window on another workspace";
    license = licenses.bsd3;
    platforms = platforms.linux;
  };
}
