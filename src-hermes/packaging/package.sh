#!/usr/bin/env bash
# Build and package the native Hermes client (ConsiglioHermes).
#
#   packaging/package.sh <linux|mac|win> <arch> <out-dir>
#
# Produces Consiglio-Hermes-<version>-<os>-<arch>.<ext> in <out-dir>:
#   linux  -> .deb (CPack, no maintainer scripts) and .AppImage (linuxdeploy)
#   mac    -> .dmg (macdeployqt, ad-hoc signed unless a real identity is set up)
#   win    -> .zip (windeployqt, portable; no installer scripts)
#
# None of these packages installs, updates or touches Hermes Agent or its data
# folder. The client detects Hermes on first launch and asks.
#
# Qt is taken from CMAKE_PREFIX_PATH / QT_ROOT_DIR (install-qt-action) or the
# system. Linux AppImage tooling is downloaded at pinned versions and verified.
set -euo pipefail

platform="${1:?platform}"
arch="${2:?arch}"
out="${3:?out dir}"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(sed -n 's/^project(ConsiglioHermes VERSION \([0-9.]*\).*/\1/p' "$here/CMakeLists.txt")"
[ -n "$version" ] || { echo "could not read the ConsiglioHermes version" >&2; exit 1; }
build="${BUILD_DIR:-$here/build-package}"
base="Consiglio-Hermes-$version"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

configure_args=(-S "$here" -B "$build" -DCMAKE_BUILD_TYPE=Release)
if [ -n "${QT_ROOT_DIR:-}" ]; then configure_args+=("-DCMAKE_PREFIX_PATH=$QT_ROOT_DIR"); fi

case "$platform" in
linux)
    cmake "${configure_args[@]}" -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build "$build" --parallel --target ConsiglioHermes

    # .deb: no preinst/postinst/prerm/postrm, so installing it never runs code.
    (cd "$build" && cpack -G DEB)
    deb="$(ls "$build"/consiglio-hermes_*.deb | head -n1)"
    if dpkg-deb --ctrl-tarfile "$deb" | tar -t | grep -Eq '(pre|post)(inst|rm)$'; then
        echo "the .deb must not carry maintainer scripts" >&2; exit 1
    fi
    cp "$deb" "$out/$base-linux-$( [ "$arch" = x64 ] && echo amd64 || echo "$arch").deb"

    # AppImage via linuxdeploy + the Qt plugin (pinned + checksummed).
    tools="$build/appimage-tools"
    mkdir -p "$tools"
    fetch() { # url sha256 dest
        [ -f "$3" ] || curl -fsSL --retry 3 -o "$3" "$1"
        echo "$2  $3" | sha256sum -c - >/dev/null
        chmod +x "$3"
    }
    fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
        c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d "$tools/linuxdeploy-x86_64.AppImage"
    fetch https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage \
        15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724 "$tools/linuxdeploy-plugin-qt-x86_64.AppImage"
    appdir="$build/AppDir"
    rm -rf "$appdir"
    DESTDIR="$appdir" cmake --install "$build"
    export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1
    export QMAKE="${QMAKE:-$(command -v qmake || echo /usr/lib/qt5/bin/qmake)}"
    [ -x "$QMAKE" ] || QMAKE=/usr/lib/qt5/bin/qmake
    export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so"
    rm -f "$build"/*.AppImage
    (cd "$build" && LDAI_OUTPUT="$base-linux-x86_64.AppImage" PATH="$tools:$PATH" \
        "$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" --plugin qt \
        --desktop-file "$appdir/usr/share/applications/consiglio-hermes.desktop" \
        --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/consiglio-hermes.png" \
        --output appimage)
    mv "$build/$base-linux-x86_64.AppImage" "$out/"
    QT_QPA_PLATFORM=offscreen "$out/$base-linux-x86_64.AppImage" --appimage-extract-and-run --version
    ;;
mac)
    cmake "${configure_args[@]}" -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
    cmake --build "$build" --parallel --target ConsiglioHermes
    stage="$build/dmg"
    rm -rf "$stage" && mkdir -p "$stage"
    cp -R "$build/ConsiglioHermes.app" "$stage/Consiglio Hermes.app"
    app="$stage/Consiglio Hermes.app"
    macdeployqt="${MACDEPLOYQT:-$(command -v macdeployqt)}"
    "$macdeployqt" "$app" -always-overwrite
    # Nothing in the bundle may point back at the build machine's Qt.
    if find "$app/Contents" -type f \( -perm -111 -o -name '*.dylib' \) -print0 |
        xargs -0 otool -L 2>/dev/null | grep -E '^\s+(/opt/homebrew|/usr/local|/Users/)' ; then
        echo "bundle still links against build-machine libraries" >&2; exit 1
    fi
    # Unsigned builds still need an ad-hoc signature to launch on Apple silicon.
    codesign --force --deep --sign "${MAC_CODESIGN_IDENTITY:--}" "$app"
    codesign --verify --deep --strict "$app"
    QT_QPA_PLATFORM=offscreen "$app/Contents/MacOS/ConsiglioHermes" --version
    ln -s /Applications "$stage/Applications"
    hdiutil create -volname "Consiglio Hermes $version" -srcfolder "$stage" -ov -format UDZO \
        "$out/$base-mac-$arch.dmg"
    ;;
win)
    cmake "${configure_args[@]}" -A x64
    cmake --build "$build" --config Release --parallel --target ConsiglioHermes
    stage="$build/zip/ConsiglioHermes"
    rm -rf "$build/zip" && mkdir -p "$stage"
    cp "$build/Release/ConsiglioHermes.exe" "$stage/"
    windeployqt="${WINDEPLOYQT:-$(command -v windeployqt.exe || command -v windeployqt)}"
    "$windeployqt" --release --no-translations --no-compiler-runtime "$stage/ConsiglioHermes.exe"
    # Ship the MSVC runtime next to the exe (app-local deployment).
    vsroot="$("/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe" -latest -property installationPath | tr -d '\r')"
    crt="$(ls -d "$(cygpath -u "$vsroot")"/VC/Redist/MSVC/*/x64/Microsoft.VC14*.CRT 2>/dev/null | sort | tail -n1)"
    [ -n "$crt" ] || { echo "MSVC runtime not found" >&2; exit 1; }
    cp "$crt"/*.dll "$stage/"
    for f in Qt5Core.dll Qt5Gui.dll Qt5Widgets.dll Qt5Svg.dll platforms/qwindows.dll; do
        [ -f "$stage/$f" ] || { echo "missing $f" >&2; exit 1; }
    done
    (cd "$build/zip" && 7z a -tzip -mx=9 "$out/$base-win-$arch.zip" ConsiglioHermes >/dev/null)
    ;;
*)
    echo "unknown platform: $platform" >&2; exit 2 ;;
esac

# Per-platform note for the release SBOM (scripts/hermes-sbom.mjs).
if [ -n "${SBOM_INPUT:-}" ]; then
    qmake_bin="${QMAKE:-$(command -v qmake || true)}"
    qt_version="$([ -n "$qmake_bin" ] && "$qmake_bin" -query QT_VERSION | tr -d '\r' || echo NOASSERTION)"
    files="$(cd "$out" && ls "$base"-* | sed 's/.*/"&"/' | paste -sd, -)"
    mkdir -p "$(dirname "$SBOM_INPUT")"
    printf '{"platform":"%s","arch":"%s","qtVersion":"%s","qtSource":"%s","files":[%s]}\n' \
        "$platform" "$arch" "$qt_version" "${QT_SOURCE:-system}" "$files" > "$SBOM_INPUT"
    cat "$SBOM_INPUT"
fi

ls -la "$out"
