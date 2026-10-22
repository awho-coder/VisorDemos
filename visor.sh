#!/bin/sh
# Lanza el cliente de VisorDemos (q2pro modificado) sin tocar el flatpak.
#   Lee paks y demos de:  $Q2DATA   (por defecto, la carpeta del flatpak de q2pro)
#   Escribe config, capturas y videos en: $VISOR_HOME
#
# Uso:  ./visor.sh                      -> abre el cliente con D-Day
#       ./visor.sh nombre_del_demo      -> reproduce dday/demos/nombre_del_demo.dm2
#       ./visor.sh +comando ...         -> pasa comandos extra a q2pro

Q2DATA="${Q2DATA:-$HOME/.var/app/com.github.skullernet.q2pro/data}"
VISOR_HOME="${VISOR_HOME:-$HOME/.local/share/visordemos}"
GAME="${GAME:-dday}"
DIR="$(cd "$(dirname "$0")" && pwd)"

mkdir -p "$VISOR_HOME/$GAME"
cp "$DIR/visor.cfg" "$VISOR_HOME/$GAME/visor.cfg"

if [ $# -ge 1 ] && [ "${1#+}" = "$1" ]; then
    demo="$1"; shift
    set -- +demo "$demo" "$@"
fi

exec "$DIR/builddir/q2pro" \
    +set basedir "$Q2DATA" \
    +set homedir "$VISOR_HOME" \
    +set game "$GAME" \
    +set logfile 1 \
    +exec visor.cfg \
    "$@"
