{
  lib,
  stdenv,
  pkg-config,
  hyprland,
  pixman,
  libdrm,
  pango,
  libinput,
  systemd,
  wayland,
  libxkbcommon,
  aquamarine,
  mesa, 
}:
stdenv.mkDerivation {
  pname = "render-unfocused-fix";
  version = "0.1.0";

  src = ./.;

  strictDeps = true;

  nativeBuildInputs = [
    pkg-config
  ];

  buildInputs = [
    hyprland
    pixman
    libdrm
    pango
    libinput
    systemd
    wayland
    libxkbcommon
  ];

  buildPhase = ''
    runHook preBuild
    make
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    install -Dm755 render-unfocused-fix.so $out/lib/render-unfocused-fix.so
    runHook postInstall
  '';

  meta = with lib; {
    description = "Hyprland plugin that offscreen-renders render_unfocused windows outside the active workspace";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
