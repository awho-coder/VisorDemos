#!/bin/sh
# Compila VisorDemos en Linux. Requiere meson, ninja, gcc y las librerias de
# desarrollo listadas en LEEME.md (seccion "Linux").
set -e
cd "$(dirname "$0")"

if ! command -v meson >/dev/null 2>&1; then
    echo "Falta meson. Instala las dependencias indicadas en LEEME.md" >&2
    exit 1
fi

if [ ! -d builddir ]; then
    meson setup builddir -Dbuildtype=release
fi
meson compile -C builddir

echo
echo "Listo. Ahora puedes:"
echo "  ./visor.sh                 abrir el visor"
echo "  ./instalar-linux.sh        abrir los demos .dm2 / .mvd2 con doble clic"
