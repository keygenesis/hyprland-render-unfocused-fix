{
  description = "Hyprland plugin that performs offscreen rendering for selected windows without drawing any visible overlay on screen";

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
    pkgs = nixpkgs.legacyPackages.${system};
  in {
    packages.${system}.default = pkgs.callPackage ./default.nix {
      hyprland = hyprland.packages.${system}.hyprland-dev;
    };

    devShells.${system}.default = pkgs.mkShell {
      inputsFrom = [self.packages.${system}.default];
    };
  };
}
