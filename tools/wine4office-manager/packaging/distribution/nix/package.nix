{ pkgs, release }:
let
  manager = pkgs.fetchurl {
    url = release.managerUrl;
    hash = release.managerHash;
  };
  runner = pkgs.fetchurl {
    url = release.wineUrl;
    hash = release.wineHash;
  };
  payload = pkgs.stdenvNoCC.mkDerivation {
    pname = "wine4office-payload";
    version = release.version;
    dontUnpack = true;
    dontPatchELF = true;
    dontPatchShebangs = true;
    dontStrip = true;
    nativeBuildInputs = [ pkgs.zstd ];
    installPhase = ''
      runHook preInstall
      mkdir -p "$out/opt/wine4office/bin" "$out/opt/wine4office/runner"
      install -m755 ${manager} "$out/opt/wine4office/bin/Wine4OfficeManager"
      work=$(mktemp -d)
      tar --zstd -xf ${runner} -C "$work"
      runner_root=$(find "$work" -mindepth 1 -maxdepth 1 -type d -print)
      test -x "$runner_root/bin/wine"
      cp -a "$runner_root/." "$out/opt/wine4office/runner/"
      printf 'Wine4OfficeManager\n' > "$out/opt/wine4office/STANDALONE"
      printf '%s\n' ${release.version} > "$out/opt/wine4office/VERSION"
      printf '%s\n' ${release.version} > "$out/opt/wine4office/WINE_VERSION"
      printf '%s\n' ${release.wineBaseVersion} > "$out/opt/wine4office/WINE_BASE_VERSION"
      printf 'stable\n' > "$out/opt/wine4office/UPDATE_CHANNEL"
      cat > "$out/opt/wine4office/PACKAGE-INSTALLATION.json" <<'EOF'
      {
        "schema_version": 1,
        "provider": "nix",
        "package": "wine4office",
        "components": ["manager", "wine"]
      }
      EOF
      runHook postInstall
    '';
  };
  dispatcher = pkgs.writeShellScript "wine4office-dispatch" ''
    if [ "''${1-}" = "--exec" ]; then
      shift
      if [ "$#" -eq 0 ]; then
        echo "wine4office: --exec requires a command" >&2
        exit 2
      fi
      exec "$@"
    fi
    exec ${payload}/opt/wine4office/bin/Wine4OfficeManager "$@"
  '';
  fhs = pkgs.buildFHSEnv {
    name = "wine4office-fhs";
    targetPkgs = p: with p; [
      alsa-lib cups dbus ffmpeg fontconfig freetype glib gnutls krb5
      libGL libgphoto2 libpulseaudio libusb1 libva libxkbcommon
      ocl-icd pcsclite sane-backends SDL2 stdenv.cc.cc.lib
      unixodbc vulkan-loader wayland zlib
      gst_all_1.gstreamer gst_all_1.gst-plugins-base
      libx11 libxcomposite libxcursor libxext libxi libxinerama
      libxrandr libxrender libxxf86vm
      libxcb-util libxcb-render-util xcb-util-cursor
      xcbutilimage xcbutilkeysyms xcbutilwm
    ];
    multiPkgs = p: with p; [
      alsa-lib fontconfig freetype gnutls stdenv.cc.cc.lib zlib
    ];
    profile = ''
      export WINE4OFFICE_MANAGER_ROOT=${payload}/opt/wine4office
    '';
    runScript = dispatcher;
  };
in pkgs.symlinkJoin {
  name = "wine4office-${release.version}";
  paths = [ fhs ];
  postBuild = ''
    cat > "$out/bin/wine4office" <<EOF
    #!${pkgs.runtimeShell}
    case \$0 in
      /*) package_wrapper=\$0 ;;
      *) package_wrapper=\$(command -v -- "\$0") ;;
    esac
    export WINE4OFFICE_PACKAGE_WRAPPER=\$package_wrapper
    exec ${fhs}/bin/wine4office-fhs "\$@"
    EOF
    chmod 0755 "$out/bin/wine4office"
    ln -s wine4office "$out/bin/Wine4OfficeManager"
  '';
  meta = {
    description = "Wine and manager tuned for Microsoft Office";
    homepage = "https://github.com/ttv20/wine4office";
    license = pkgs.lib.licenses.lgpl21Plus;
    platforms = [ "x86_64-linux" ];
    mainProgram = "wine4office";
  };
}
