# VisorDemos

**Visor de demos de Quake 2 para hacer videos.** Reproduce demos `.dm2` y
`.mvd2`, te deja mover la cámara libremente o seguir a cualquier jugador, y
exporta clips de video (MP4 1080p a 60 fps, con sonido) listos para tu editor.
Pensado para **D-Day: Normandy**, pero funciona con cualquier mod.

Basado en [q2pro](https://github.com/q2pro/q2pro).

## Qué hace

- Barra de tiempo con pausa, saltos de 10 s y velocidad de x0.1 a x4.
- **Cámara libre** (volar por el mapa) y **seguir jugadores** desde atrás;
  la velocidad de la cámara se cambia con la rueda del mouse.
- **Nick del jugador que estás viendo o siguiendo**, también en demos que
  grabaste como espectador (muestra a quién seguías).
- **Recorridos de cámara**: graba el movimiento de la cámara mientras miras
  el demo y el clip lo repite, suavizado.
- **Clips de video**: marca inicio y fin, presiona una tecla y obtienes un MP4
  (H.264 + AAC) o MOV (ProRes) a resolución y cuadros fijos, sin HUD.
- Cámara lenta fluida (los cuadros se generan, no se interpolan en el editor).
- No toca tu configuración del juego: guarda la suya en `visordemos.cfg`.

## Descargar

- **Windows**: baja el `.zip` desde la [última versión](../../releases/latest).
  Adentro vienen `1-Instalacion.html` y `2-Guia-de-uso.html`.
- **Linux**: compila desde el código con `./compilar.sh` (dependencias en
  [LEEME.md](LEEME.md#linux)).

## Documentación

- Guías en línea: **https://awho-coder.github.io/VisorDemos/**
  ([instalación](https://awho-coder.github.io/VisorDemos/instalacion.html) ·
  [guía de uso](https://awho-coder.github.io/VisorDemos/guia-de-uso.html)).
- [LEEME.md](LEEME.md): instalación, teclas, cómo hacer clips, ajustes y límites.

## Compilar

| Plataforma | Comando |
|---|---|
| Linux | `./compilar.sh`, luego `./visor.sh` e (opcional) `./instalar-linux.sh` |
| Windows (desde Linux) | `./compilar-windows.sh` genera `VisorDemos-windows-rN.zip` (usa mingw si está instalado; si no, descarga uno portátil) |

Al publicar una versión (tag `v*`), GitHub Actions arma el `.zip` de Windows y
lo adjunta a la Release ([.github/workflows/windows.yml](.github/workflows/windows.yml)).

## Desarrollo

El proyecto sigue **gitflow**:

| Rama | Para qué |
|---|---|
| `main` | Versiones publicadas. Cada versión lleva un tag `vX.Y.Z` |
| `develop` | Integración: aquí se juntan las funciones terminadas |
| `feature/…` | Una función nueva, sale de `develop` y vuelve a `develop` |
| `release/X.Y.Z` | Preparar una versión: sale de `develop`, se une a `main` (con tag) y a `develop` |
| `hotfix/X.Y.Z` | Arreglo urgente de una versión publicada: sale de `main`, vuelve a `main` y `develop` |

Al subir un tag `v*` a `main`, GitHub Actions arma el `.zip` de Windows y lo
adjunta a la Release. El q2pro original está como remote `upstream` para traer
sus mejoras.

## Licencia

GNU GPL versión 2 o posterior, igual que q2pro (ver [LICENSE](LICENSE)).

---

## Q2PRO (proyecto base)

Q2PRO is an enhanced Quake 2 client and server for Windows and Linux. Supported
features include:

* unified OpenGL renderer with support for wide range of OpenGL versions
* enhanced console with persistent command history and autocompletion
* rendering / physics / packet rate separation
* syncing to GPU for reduced input lag with vsync on
* ZIP packfiles (.pkz)
* JPEG/PNG textures and screenshots
* MD3 and MD5 (re-release) models
* Ogg Vorbis music and Ogg Theora cinematics
* compatibility with re-release assets
* fast and secure HTTP downloads
* multichannel sound using OpenAL
* stereo WAV files support
* seeking in demos, recording from demos, server side multiview demos
* live game broadcasting capabilities
* network protocol extensions for larger maps
* eliminates frame overflows (even for legacy clients)
* won't crash if game data is corrupted

Q2PRO doesn't have releases. It is always recommended to use the latest nighly
build from the top of Releases page.

Linux binaries are not provided. Users are advised to build from source. See
BUILDING.md file for instructions.

For information on using and configuring Q2PRO, refer to client and server
manuals available in doc/ subdirectory.
