{
  description = "Hyprland plugin that offscreen-renders render_unfocused windows outside the active workspace";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    hyprland.url = "github:hyprwm/Hyprland";
  };

  outputs = {
    self,
    nixpkgs,
    hyprland,
  }: let
    system = "x86_64-linux";
    pkgs = nixpkgs.legacyPackages.${system}.extend hyprland.overlays.hyprland-packages;
  in {
    packages.${system}.default = pkgs.callPackage ./default.nix {
      hyprland = pkgs.hyprland;
      hyprlandPlugins = pkgs.hyprlandPlugins;
    };

    devShells.${system}.default = pkgs.mkShell {
      inputsFrom = [self.packages.${system}.default];
    };
  };
}
