{
  description = "Frequent Traveller — Pebble multi-timezone watchface dev environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      # Systems we support a dev shell for.
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = f:
        nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; }));
    in
    {
      devShells = forAllSystems (pkgs: {
        # mkShellNoCC: do NOT pull in a host C compiler. The Pebble `waf` build
        # must use the SDK's bundled arm-none-eabi toolchain; a stdenv gcc in
        # PATH (and an exported CC=gcc) shadows it and fails on the ARM-only
        # `-mthumb`/`-mcpu` flags.
        default = pkgs.mkShellNoCC {
          packages = [
            pkgs.uv          # installs/runs the coredevices `pebble` CLI (uv tool)
            pkgs.nodejs      # `pebble build` needs npm >= 3.0.0 to bundle pkjs
            pkgs.python3     # screenshot/asset helpers
            pkgs.libpng      # qemu-pebble links against it; see DYLD note below
            pkgs.pre-commit  # git hook runner (see .pre-commit-config.yaml)
            pkgs.convco     # conventional-commit linting + release versioning
          ];

          shellHook = ''
            # Belt-and-suspenders: ensure no host compiler is advertised to waf,
            # so it falls back to the SDK's arm-none-eabi cross-compiler.
            unset CC CXX

            # The SDK ships a prebuilt qemu-pebble whose only non-system
            # dependency is a hardcoded Homebrew path for libpng16. Rather than
            # layering Homebrew onto the base OS, point dyld at the Nix libpng.
            # DYLD_LIBRARY_PATH is consulted by leaf name before the baked-in
            # absolute path, so this satisfies the load without patching the SDK.
            if [ "$(uname)" = "Darwin" ]; then
              export DYLD_LIBRARY_PATH="${pkgs.libpng.out}/lib''${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
            fi

            # The Pebble SDK CLI (coredevices/repebble fork) is distributed as a
            # `uv` tool rather than a nixpkgs package. Install it once, locally,
            # without touching the base OS. It self-manages the SDK download.
            # Advisory output goes to stderr: `nix develop --command foo` runs
            # this hook first, so anything on stdout ends up inside a
            # `$(...)` capture of foo's output. The publish workflow captures
            # `convco version` that way.
            if ! command -v pebble >/dev/null 2>&1; then
              echo "pebble CLI not found. Install it with:" >&2
              echo "    uv tool install pebble-tool" >&2
              echo "(uv is provided by this dev shell)." >&2
            fi

            # Keep the git hooks in sync with .pre-commit-config.yaml on every
            # shell entry, so a fresh clone (or a config change) is wired up
            # without a manual `pre-commit install` step. The hook types come
            # from `default_install_hook_types` (pre-commit and commit-msg, the
            # latter running `convco check` on the message).
            if [ -d .git ]; then
              pre-commit install --install-hooks >/dev/null
            fi
          '';
        };
      });
    };
}
