#!/bin/sh
# Registra VisorDemos en el escritorio: aparece en el menu de aplicaciones y
# los archivos .dm2 / .mvd2 se abren con doble clic. No necesita sudo.
# Para deshacerlo:  ./instalar-linux.sh --quitar
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
APPS="$HOME/.local/share/applications"
MIME="$HOME/.local/share/mime"
ICONS="$HOME/.local/share/icons/hicolor/scalable/apps"

if [ "$1" = "--quitar" ]; then
    rm -f "$APPS/visordemos.desktop" "$MIME/packages/visordemos.xml" "$ICONS/visordemos.xpm"
    update-mime-database "$MIME" >/dev/null 2>&1 || true
    update-desktop-database "$APPS" >/dev/null 2>&1 || true
    echo "VisorDemos quitado del escritorio."
    exit 0
fi

mkdir -p "$APPS" "$MIME/packages" "$ICONS"
cp "$DIR/src/unix/res/q2pro.xpm" "$ICONS/visordemos.xpm"

cat > "$MIME/packages/visordemos.xml" <<XML
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/x-quake2-demo">
    <comment>Demo de Quake 2</comment>
    <glob pattern="*.dm2"/>
    <glob pattern="*.mvd2"/>
    <glob pattern="*.mvd2.gz"/>
  </mime-type>
</mime-info>
XML

cat > "$APPS/visordemos.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=VisorDemos
GenericName=Visor de demos de Quake 2
Comment=Ver demos de Quake 2 y sacar clips de video
Exec="$DIR/visor.sh" %f
Icon=$ICONS/visordemos.xpm
Terminal=false
Categories=Game;
MimeType=application/x-quake2-demo;
DESKTOP

update-mime-database "$MIME" >/dev/null 2>&1 || true
update-desktop-database "$APPS" >/dev/null 2>&1 || true
xdg-mime default visordemos.desktop application/x-quake2-demo 2>/dev/null || true

echo "Listo: los demos .dm2 y .mvd2 se abren con VisorDemos al hacer doble clic."
