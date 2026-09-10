{
  description = "Wine4Office binary release";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      release = import ./release.nix;
      wine4office = import ./package.nix { inherit pkgs release; };
    in {
      packages.${system} = {
        default = wine4office;
        wine4office = wine4office;
      };
      apps.${system}.default = {
        type = "app";
        program = "${wine4office}/bin/wine4office";
      };
    };
}
