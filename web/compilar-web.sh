#!/bin/sh
# Compila VisorDemos para el navegador (WebAssembly + WebGL2) con Emscripten y deja
# el sitio en build-web/sitio/: visor.html, manifiesto.mjs, q2pro.js/.wasm/.data.
#
# Requiere emsdk (https://emscripten.org): por defecto en ~/emsdk.
# Los archivos del juego NO van aquí: el visor los baja de ?datos= (ver q2web.py).
set -e
cd "$(dirname "$0")/.."

EMSDK="${EMSDK:-$HOME/emsdk}"
# shellcheck disable=SC1091
. "$EMSDK/emsdk_env.sh" >/dev/null 2>&1

# configuración base que va empaquetada dentro del visor (q2pro.data)
BASE="build-web/base"
rm -rf "$BASE"
mkdir -p "$BASE/baseq2"
cp web/base/baseq2/default.cfg "$BASE/baseq2/"
cp src/client/ui/q2pro.menu "$BASE/baseq2/"
cp visor.cfg "$BASE/baseq2/"

if [ ! -f build-web/build.ninja ]; then
    meson setup build-web --cross-file cross-emscripten.txt \
        -Dbuildtype=release -Dsystem-wide=false -Ddefault-game=dday \
        -Dlibcurl=disabled -Davcodec=disabled -Dopenal=disabled -Dlibjpeg=disabled \
        -Dx11=disabled -Dwayland=disabled -Dsystem-console=false -Dsave-games=false \
        -Dsoftware-sound=enabled -Dwindows-crash-dumps=disabled -Dicmp-errors=disabled \
        "-Dweb-link-args=['--preload-file','$(pwd)/$BASE@/q2']"
fi
meson compile -C build-web

SITIO="build-web/sitio"
mkdir -p "$SITIO"
cp build-web/q2pro.js build-web/q2pro.wasm build-web/q2pro.data "$SITIO/"
cp web/visor.html "$SITIO/index.html"
cp web/manifiesto.mjs "$SITIO/"
echo
echo "Listo: $SITIO ($(du -sh "$SITIO" | cut -f1))"
