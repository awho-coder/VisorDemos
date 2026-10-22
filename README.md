# VisorDemos

**Visor de demos de Quake 2 para hacer videos.** Reproduce demos `.dm2` y
`.mvd2`, te deja mover la cámara libremente o seguir a cualquier jugador, y
exporta clips de video (MP4 1080p a 60 fps, con sonido) listos para tu editor.
Pensado para **D-Day: Normandy**, pero funciona con cualquier mod.

Basado en [q2pro](https://github.com/q2pro/q2pro).

## Qué hace

- Barra de tiempo con pausa, saltos de 10 s y velocidad de x0.1 a x4.
- **Cámara libre** (volar por el mapa) y **seguir jugadores** desde atrás.
- **Recorridos de cámara**: graba el movimiento de la cámara mientras miras
  el demo y el clip lo repite, suavizado.
- **Clips de video**: marca inicio y fin, presiona una tecla y obtienes un MP4
  (H.264 + AAC) o MOV (ProRes) a resolución y cuadros fijos, sin HUD.
- Cámara lenta fluida (los cuadros se generan, no se interpolan en el editor).
- No toca tu configuración del juego: guarda la suya en `visordemos.cfg`.

## Descargar

- **Windows**: baja el `.zip` desde [Releases](../../releases). Adentro vienen
  `1-Instalacion.html` y `2-Guia-de-uso.html` con las instrucciones.
- **Linux**: compila desde el código con `./compilar.sh` (dependencias en
  [LEEME.md](LEEME.md#linux)).

## Documentación

- [LEEME.md](LEEME.md): instalación, teclas, cómo hacer clips, ajustes y límites.
- [docs/instalacion.html](docs/instalacion.html) y
  [docs/guia-de-uso.html](docs/guia-de-uso.html): las mismas guías en formato
  página (se ven al abrirlas desde el `.zip`).

## Compilar

| Plataforma | Comando |
|---|---|
| Linux | `./compilar.sh`, luego `./visor.sh` e (opcional) `./instalar-linux.sh` |
| Windows (desde Linux) | `./compilar-windows.sh` genera `VisorDemos-windows-rN.zip`; no necesita instalar mingw |

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
