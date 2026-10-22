#!/bin/sh
# Lanza VisorDemos (q2pro modificado para ver demos y sacar clips de video).
#
#   Lee paks y demos de:  $Q2DATA   (carpeta de Quake 2 que tiene baseq2/ y el mod)
#   Escribe su configuracion (visordemos.cfg), videos, recorridos de camara y
#   capturas en:          $VISOR_HOME   (nunca toca la configuracion del juego)
#
# Uso:  visor.sh                          abre el visor (menu Demos con Esc)
#       visor.sh /ruta/a/partida.dm2      abre ese demo (sirve para doble clic)
#       visor.sh nombre_del_demo          reproduce <mod>/demos/nombre_del_demo.dm2
#       visor.sh +comando ...             pasa comandos extra al visor
#
# Variables opcionales: Q2DATA, VISOR_HOME, GAME (mod, por defecto dday).

DIR="$(cd "$(dirname "$0")" && pwd)"

# carpeta de Quake 2: la variable, la del flatpak de q2pro, ~/.q2pro o ~/quake2
if [ -z "$Q2DATA" ]; then
    for d in "$HOME/.var/app/com.github.skullernet.q2pro/data" "$HOME/.q2pro" "$HOME/quake2"; do
        if [ -d "$d/baseq2" ]; then Q2DATA="$d"; break; fi
    done
fi
if [ -z "$Q2DATA" ] || [ ! -d "$Q2DATA/baseq2" ]; then
    echo "No encuentro tu carpeta de Quake 2 (la que contiene baseq2/)." >&2
    echo "Indica la ruta asi:  Q2DATA=/ruta/a/quake2 $0" >&2
    exit 1
fi

VISOR_HOME="${VISOR_HOME:-$HOME/.local/share/visordemos}"
GAME="${GAME:-dday}"

BIN="$DIR/builddir/q2pro"
[ -x "$BIN" ] || BIN="$DIR/visordemos"
if [ ! -x "$BIN" ]; then
    echo "No encuentro el ejecutable. Compila primero con ./compilar.sh" >&2
    exit 1
fi

mkdir -p "$VISOR_HOME/baseq2" "$VISOR_HOME/$GAME/demos"
cp "$DIR/visor.cfg" "$VISOR_HOME/baseq2/visor.cfg"
rm -f "$VISOR_HOME/$GAME/visor.cfg"   # ubicacion antigua

if [ $# -ge 1 ] && [ "${1#+}" = "$1" ]; then
    demo="$1"; shift
    if [ -f "$demo" ]; then
        # un archivo cualquiera (doble clic / arrastrar): enlazarlo en demos/
        name="$(basename "$demo")"
        ln -sf "$(cd "$(dirname "$demo")" && pwd)/$name" "$VISOR_HOME/$GAME/demos/$name"
        demo="$name"
    fi
    set -- +demo "$demo" "$@"
fi

exec "$BIN" \
    +set basedir "$Q2DATA" \
    +set homedir "$VISOR_HOME" \
    +set game "$GAME" \
    +set logfile 1 \
    "$@"
