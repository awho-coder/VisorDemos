#!/bin/sh
# Compila VisorDemos para Windows 64 bits desde Linux y arma el .zip para
# repartir. Requiere meson, ninja, make, git, curl, zip y conexion a internet
# (descarga FFmpeg, zlib, libpng y libjpeg-turbo). Si no hay mingw ni nasm
# instalados, descarga un compilador portatil (llvm-mingw) y compila nasm en
# build-win64/tools, sin tocar el sistema.
set -e
cd "$(dirname "$0")"
SRC="$(pwd)"
OUT="$SRC/build-win64"
FFMPEG_BRANCH="release/7.1"
JOBS="$(nproc 2>/dev/null || echo 4)"
mkdir -p "$OUT"

# sin mingw ni nasm instalados: usar versiones portatiles en build-win64/tools
# (llvm-mingw y nasm compilado localmente; no necesita sudo)
TOOLS="$OUT/tools"
LLVM_MINGW="llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64"
NASM_VER="2.16.03"
mkdir -p "$OUT/dl" "$TOOLS"
if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    if [ ! -x "$TOOLS/llvm-mingw/bin/x86_64-w64-mingw32-gcc" ]; then
        echo "Descargando llvm-mingw (compilador para Windows, portatil)..."
        curl -L -o "$OUT/dl/llvm-mingw.tar.xz" \
            "https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/$LLVM_MINGW.tar.xz"
        tar xf "$OUT/dl/llvm-mingw.tar.xz" -C "$TOOLS"
        mv "$TOOLS/$LLVM_MINGW" "$TOOLS/llvm-mingw"
    fi
    PATH="$TOOLS/llvm-mingw/bin:$PATH"
fi
if ! command -v nasm >/dev/null 2>&1; then
    if [ ! -x "$TOOLS/nasm/bin/nasm" ]; then
        echo "Compilando nasm $NASM_VER..."
        curl -L -o "$OUT/dl/nasm.tar.xz" \
            "https://www.nasm.us/pub/nasm/releasebuilds/$NASM_VER/nasm-$NASM_VER.tar.xz"
        tar xf "$OUT/dl/nasm.tar.xz" -C "$OUT/dl"
        (cd "$OUT/dl/nasm-$NASM_VER" && ./configure --prefix="$TOOLS/nasm" >/dev/null && make -j"$JOBS" >/dev/null && make install >/dev/null)
    fi
    PATH="$TOOLS/nasm/bin:$PATH"
fi
export PATH

for t in x86_64-w64-mingw32-gcc nasm meson ninja make git zip; do
    command -v "$t" >/dev/null 2>&1 || { echo "Falta '$t' (ver LEEME.md, seccion Windows)." >&2; exit 1; }
done


### x264: el mismo codificador H.264 que en Linux, para que los MP4 salgan
### igual en cualquier Windows (el de Media Foundation queda de respaldo)
PREFIX="$OUT/ffmpeg-prefix"
if [ ! -f "$PREFIX/lib/libx264.a" ]; then
    [ -d "$OUT/x264" ] || git clone --depth=1 -b stable https://code.videolan.org/videolan/x264.git "$OUT/x264"
    cd "$OUT/x264"
    ./configure \
        --host=x86_64-w64-mingw32 \
        --cross-prefix=x86_64-w64-mingw32- \
        --prefix="$PREFIX" \
        --enable-static --disable-cli --disable-opencl --enable-strip \
        --enable-win32thread
    make -j"$JOBS"
    make install
    cd "$SRC"
fi

### FFmpeg minimo y estatico: lo que q2pro necesita para cinematicas, mas los
### codificadores para los clips (H.264 con x264 o Media Foundation, ProRes,
### AAC, PCM)
if [ ! -f "$PREFIX/.visordemos-x264" ]; then
    [ -d "$OUT/ffmpeg" ] || git clone --depth=1 -b "$FFMPEG_BRANCH" https://github.com/FFmpeg/FFmpeg.git "$OUT/ffmpeg"
    rm -rf "$OUT/ffmpeg/build" "$OUT/build"
    mkdir -p "$OUT/ffmpeg/build"
    cd "$OUT/ffmpeg/build"
    PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig" PKG_CONFIG_PATH="" \
    ../configure \
        --prefix="$PREFIX" \
        --cross-prefix=x86_64-w64-mingw32- \
        --arch=x86 \
        --target-os=mingw64 \
        --pkg-config=pkg-config --pkg-config-flags=--static \
        --enable-gpl --enable-libx264 \
        --disable-everything \
        --enable-decoder=theora --enable-decoder=vorbis --enable-decoder=idcin \
        --enable-decoder='pcm_s16le,pcm_u8,pcm_s16be' \
        --enable-demuxer=ogg --enable-demuxer=idcin --enable-demuxer=wav \
        --enable-parser=vp3 --enable-parser=vorbis \
        --enable-encoder=libx264 --enable-encoder=h264_mf --enable-encoder=prores_ks \
        --enable-encoder=aac --enable-encoder=pcm_s16le --enable-encoder=mpeg4 \
        --enable-muxer=mp4 --enable-muxer=mov \
        --enable-mediafoundation --enable-w32threads \
        --disable-protocols --enable-protocol=file \
        --disable-avdevice --disable-avfilter --disable-programs \
        --disable-autodetect --disable-network --disable-doc \
        --disable-debug
    make -j"$JOBS"
    make install
    touch "$PREFIX/.visordemos-x264"
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
        -Dc_link_args=-static \
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
cp "$SRC/docs/instalacion.html" "$PKG/1-Instalacion.html"
cp "$SRC/docs/guia-de-uso.html" "$PKG/2-Guia-de-uso.html"
sed 's/$/\r/' "$SRC/LEEME.md" > "$PKG/LEEME.txt"
sed 's/$/\r/' "$SRC/LICENSE" > "$PKG/LICENSE.txt"
sed -i 's/$/\r/' "$PKG/baseq2/visor.cfg"

VER="$(git -C "$SRC" rev-list --count HEAD 2>/dev/null || echo 0)"
ZIP="$SRC/VisorDemos-windows-r$VER.zip"
rm -f "$ZIP"
(cd "$OUT" && zip -9 -r "$ZIP" VisorDemos)
echo
echo "Listo: $ZIP"
