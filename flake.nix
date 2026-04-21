{
  description = "RVVM - The RISC-V Virtual Machine";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };

        nativeBuildInputs = with pkgs; [
          gnumake
          pkg-config
          cmake
        ];

        buildInputs = with pkgs; [
          libx11
          libxext
          wayland
          wayland-protocols
          libxkbcommon
          alsa-lib
          SDL2
        ];

        # Runtime-only deps that RVVM dlopens (not link-time). Must be
        # reachable via LD_LIBRARY_PATH / RUNPATH or dlopen returns NULL
        # and the device falls back to "no backend" silently.
        runtimeDeps = with pkgs; [ alsa-lib ];

        # PipeWire's ALSA plugin — alsa-lib reads /etc/alsa/conf.d on
        # NixOS which points `default` at `pcm.pipewire`, but the plugin
        # itself lives in pipewire's out and alsa-lib only finds it via
        # ALSA_PLUGIN_DIR.
        alsaPluginDir = "${pkgs.pipewire}/lib/alsa-lib";

        rvvm = pkgs.stdenv.mkDerivation {
          pname = "rvvm";
          version = "0.7-git";
          src = ./.;

          inherit buildInputs;
          nativeBuildInputs = nativeBuildInputs ++ [ pkgs.makeWrapper ];

          enableParallelBuilding = true;

          makeFlags = [
            "PREFIX=$(out)"
            "USE_WAYLAND=1"
            "USE_X11=1"
            "USE_NET=1"
            "USE_SOUND=1"
            "USE_ALSA=1"
          ];

          # alsa-lib's global.h redefines `struct timespec` unless
          # _POSIX_C_SOURCE is defined — collides with glibc.
          # LDFLAGS: force alsa-lib into RUNPATH. Nothing link-time
          # references it (ALSA is pure dlopen via dlib), so the linker
          # drops it with --as-needed otherwise.
          env.NIX_CFLAGS_COMPILE = "-D_POSIX_C_SOURCE=200809L";
          NIX_LDFLAGS = "-rpath ${pkgs.alsa-lib}/lib";

          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            cp release.*/rvvm* $out/bin/ 2>/dev/null || find . -maxdepth 2 -name 'rvvm' -executable -type f -exec cp {} $out/bin/ \;

            # Route `default` ALSA device through PipeWire at runtime.
            # NixOS's /etc/alsa/conf.d points default→pipewire, but
            # alsa-lib only locates the plugin via ALSA_PLUGIN_DIR.
            for bin in $out/bin/rvvm_*; do
              [ -x "$bin" ] || continue
              wrapProgram "$bin" --set-default ALSA_PLUGIN_DIR "${alsaPluginDir}"
            done

            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "RISC-V Virtual Machine";
            homepage = "https://github.com/LekKit/RVVM";
            license = [ licenses.gpl2Plus licenses.mpl20 ];
            platforms = platforms.unix;
            mainProgram = "rvvm";
          };
        };
      in
      {
        packages.default = rvvm;
        packages.rvvm = rvvm;

        devShells.default = pkgs.mkShell {
          inherit buildInputs;

          nativeBuildInputs = nativeBuildInputs ++ (with pkgs; [
            gcc
            clang
            clang-tools
            gdb
            git
          ]);

          # Needed for `make` inside the devshell to pick up the same
          # fix as the package build (alsa-lib vs glibc timespec clash).
          CPPFLAGS = "-D_POSIX_C_SOURCE=200809L";

          # dev-built binaries don't get wrapped, so expose the same
          # audio runtime bits via env. Prepend so it doesn't stomp any
          # outer env already pointing at something.
          LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath runtimeDeps;
          ALSA_PLUGIN_DIR = alsaPluginDir;

          shellHook = ''
            echo "RVVM dev shell"
            echo "  Build:  make -j\$(nproc) USE_SOUND=1 USE_ALSA=1"
            echo "  Run:    ./release.*/rvvm_*"
            echo "  CMake:  cmake -B build && cmake --build build -j\$(nproc)"
          '';
        };

        formatter = pkgs.nixpkgs-fmt;
      });
}
