{
  description = "OpenXRay (S.T.A.L.K.E.R. X-Ray engine) development shell";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      devShells = forAll (pkgs: {
        # GCC, not Clang: Clang builds hit an assertion (OpenXRay issue #1224).
        default = pkgs.mkShell.override { stdenv = pkgs.gccStdenv; } {
          # ccache: picked up automatically by OpenXRay's CMake (cmake/XRay.Compiler.GNULike.cmake)
          nativeBuildInputs = with pkgs; [ cmake ninja pkg-config git gdb ccache ];
          buildInputs = with pkgs; [
            SDL2 openal glew liblockfile
            libogg libvorbis libtheora
            lzo libjpeg mimalloc
          ];
          shellHook = ''
            echo "OpenXRay dev shell. Build:"
            echo "  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo"
            echo "  cmake --build build"
          '';
        };
      });
    };
}
