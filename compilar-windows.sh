#!/bin/sh
# Compila VisorDemos para Windows 64 bits desde Linux y arma el .zip para
# repartir. Requiere: x86_64-w64-mingw32-gcc (paquete mingw64-gcc en Fedora,
# gcc-mingw-w64 en Debian/Ubuntu), nasm, meson, ninja, make, git, zip
# y conexion a internet (descarga FFmpeg, zlib, libpng y libjpeg-turbo).
set -e
cd "$(dirname "$0")"
SRC="$(pwd)"
OUT="$SRC/build-win64"
FFMPEG_BRANCH="release/7.1"
JOBS="$(nproc 2>/dev/null || echo 4)"

for t in x86_64-w64-mingw32-gcc nasm meson ninja make git zip; do
    command -v "$t" >/dev/null 2>&1 || { echo "Falta '$t' (ver LEEME.md, seccion Windows)." >&2; exit 1; }
done

mkdir -p "$OUT"

### FFmpeg minimo y estatico: lo que q2pro necesita para cinematicas, mas los
### codificadores para los clips (H.264 por Media Foundation, ProRes, AAC, PCM)
if [ ! -f "$OUT/ffmpeg-prefix/lib/pkgconfig/libavcodec.pc" ]; then
    [ -d "$OUT/ffmpeg" ] || git clone --depth=1 -b "$FFMPEG_BRANCH" https://github.com/FFmpeg/FFmpeg.git "$OUT/ffmpeg"
    mkdir -p "$OUT/ffmpeg/build"
    cd "$OUT/ffmpeg/build"
    ../configure \
        --prefix="$OUT/ffmpeg-prefix" \
        --cross-prefix=x86_64-w64-mingw32- \
        --arch=x86 \
        --target-os=mingw64 \
        --disable-everything \
        --enable-decoder=theora --enable-decoder=vorbis --enable-decoder=idcin \
        --enable-decoder='pcm_s16le,pcm_u8,pcm_s16be' \
        --enable-demuxer=ogg --enable-demuxer=idcin --enable-demuxer=wav \
        --enable-parser=vp3 --enable-parser=vorbis \
        --enable-encoder=h264_mf --enable-encoder=prores_ks \
        --enable-encoder=aac --enable-encoder=pcm_s16le --enable-encoder=mpeg4 \
        --enable-muxer=mp4 --enable-muxer=mov \
        --enable-mediafoundation --enable-w32threads \
        --disable-protocols --enable-protocol=file \
        --disable-avdevice --disable-avfilter --disable-programs \
        --disable-autodetect --disable-network --disable-doc \
        --disable-debug
    make -j"$JOBS"
    make install
    cd "$SRC"
fi

### VisorDemos
export PKG_CONFIG_LIBDIR="$OUT/ffmpeg-prefix/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=""
if [ ! -d "$OUT/build" ]; then
    meson setup --cross-file "$SRC/.ci/x86_64-w64-mingw32.txt" \
        -Dbuildtype=release -Dwrap_mode=forcefallback -Dprefer_static=true \
        -Davcodec=enabled -Dlibcurl=disabled -Dopenal=disabled \
        -Dsdl2=disabled -Dwayland=disabled -Dx11=disabled \
        -Ddefault-game=dday -Dwindows-crash-dumps=disabled \
        "$OUT/build" "$SRC"
fi
meson compile -C "$OUT/build"

### Paquete
PKG="$OUT/VisorDemos"
rm -rf "$PKG"
mkdir -p "$PKG/baseq2"
cp "$OUT/build/q2pro.exe" "$PKG/VisorDemos.exe"
x86_64-w64-mingw32-strip "$PKG/VisorDemos.exe"
cp "$SRC/visor.cfg" "$PKG/baseq2/visor.cfg"
cp "$SRC/src/client/ui/q2pro.menu" "$PKG/baseq2/q2pro.menu"
cp "$SRC/windows/asociar-demos.bat" "$PKG/"
sed 's/$/\r/' "$SRC/LEEME.md" > "$PKG/LEEME.txt"
sed 's/$/\r/' "$SRC/LICENSE" > "$PKG/LICENSE.txt"
sed -i 's/$/\r/' "$PKG/baseq2/visor.cfg"

VER="$(git -C "$SRC" rev-list --count HEAD 2>/dev/null || echo 0)"
ZIP="$SRC/VisorDemos-windows-r$VER.zip"
rm -f "$ZIP"
(cd "$OUT" && zip -9 -r "$ZIP" VisorDemos)
echo
echo "Listo: $ZIP"
