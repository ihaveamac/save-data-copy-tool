{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    devkitNix.url = "github:bandithedoge/devkitNix";
  };

  outputs = { self, nixpkgs, devkitNix }: let
    pkgs = import nixpkgs { system = "x86_64-linux"; overlays = [ devkitNix.overlays.default ]; };
  in {
    devShells.x86_64-linux.default = pkgs.mkShell.override { stdenv = pkgs.devkitNix.stdenvARM; } {};

    packages.x86_64-linux = rec {
      save-data-copy-tool = pkgs.devkitNix.stdenvARM.mkDerivation rec {
        pname = "save-data-copy-tool";
        version = "0";
        src = builtins.path { path = ./.; name = pname; };

        makeFlags = [ "TARGET=${pname}" ];

        installPhase = ''
          mkdir $out
          cp ${pname}.3dsx ${pname}.smdh ${pname}.elf $out
        '';
      };
      default = save-data-copy-tool;
    };
  };
}
