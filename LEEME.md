# VisorDemos — visor de demos de Quake 2 para hacer videos

VisorDemos es un cliente de Quake 2 (basado en [q2pro](https://github.com/q2pro/q2pro))
hecho para **ver demos y sacar clips de video** listos para un editor.
Funciona con demos `.dm2` (los que graba cualquier jugador) y `.mvd2`
(los que graba el servidor). Pensado para D-Day: Normandy, pero sirve con
cualquier mod.

Qué puedes hacer:

- Ver el demo con una barra de tiempo, pausar, adelantar, retroceder y
  cambiar la velocidad (cámara lenta).
- **Soltar la cámara** y volar por el mapa, o **seguir a cualquier jugador**
  desde atrás.
- **Grabar un recorrido de cámara** mientras miras el demo, para que el video
  repita ese movimiento suave.
- Marcar inicio y fin de una jugada y **exportarla a video** (MP4 o MOV) con
  imagen y sonido, a 1080p y 60 cuadros por segundo.

VisorDemos **no toca tu configuración del juego**: guarda la suya aparte, en
`visordemos.cfg`.

---

## 1. Instalación

### Windows

1. Descomprime el `.zip`.
2. Copia **todo el contenido** de la carpeta `VisorDemos` dentro de tu carpeta
   de Quake 2 (la que tiene `baseq2` y `dday`). Quedará así:

   ```
   Quake2\
     VisorDemos.exe
     asociar-demos.bat
     1-Instalacion.html
     2-Guia-de-uso.html
     LEEME.txt
     baseq2\visor.cfg
     baseq2\q2pro.menu
     dday\ ...
   ```

   No uses una carpeta dentro de *Archivos de programa*: ahí Windows no deja
   guardar los videos.
3. (Opcional) Haz doble clic en `asociar-demos.bat` para que los demos `.dm2`
   y `.mvd2` se abran con VisorDemos al hacerles doble clic.
4. Abre `VisorDemos.exe`. La primera vez Windows puede mostrar *"Windows
   protegió su PC"*: **Más información → Ejecutar de todas formas** (el
   programa no tiene firma digital). Por defecto usa el mod D-Day; para otro
   mod, crea un acceso directo con `VisorDemos.exe +set game nombre_del_mod`.

**Actualizar a una versión nueva:** baja el `.zip` nuevo y reemplaza
`VisorDemos.exe` y `baseq2\visor.cfg`. Tus videos, recorridos y ajustes se
mantienen.

### Linux

Instala las dependencias de compilación (una sola vez):

- **Fedora**: `sudo dnf install meson ninja-build gcc sdl2-compat-devel openal-soft-devel libpng-devel libjpeg-turbo-devel zlib-ng-compat-devel mesa-libGL-devel libglvnd-devel libX11-devel libXi-devel wayland-devel wayland-protocols-devel libdecor-devel ffmpeg-devel`
  (con RPM Fusion; sin RPM Fusion usa `ffmpeg-free-devel`: puede que no
  tenga codificador H.264 y el MP4 salga en un formato más simple; en ese
  caso exporta en MOV).
- **Debian/Ubuntu**: `sudo apt install meson gcc libsdl2-dev libopenal-dev libpng-dev libjpeg-dev zlib1g-dev mesa-common-dev libx11-dev libxi-dev libwayland-dev wayland-protocols libdecor-0-dev libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev`

Luego, en la carpeta de VisorDemos:

```
./compilar.sh            # compila
./instalar-linux.sh      # menu de aplicaciones + doble clic en .dm2/.mvd2
./visor.sh               # abre el visor
```

`visor.sh` busca Quake 2 en la carpeta del flatpak de q2pro, en `~/.q2pro` y en
`~/quake2`. Si lo tienes en otro lado: `Q2DATA=/ruta/a/quake2 ./visor.sh`.
Para otro mod: `GAME=nombre_del_mod ./visor.sh`. En Linux la configuración,
los videos y los recorridos quedan en `~/.local/share/visordemos/<mod>/`.

---

## 2. Abrir un demo

- Doble clic en el archivo `.dm2` / `.mvd2` (si hiciste el paso opcional), o
- arrástralo sobre `VisorDemos.exe` / `visor.sh`, o
- abre el visor, presiona **Esc → browse demos** y elígelo de la lista, o
- en la consola (tecla **`**): `demo nombre_del_demo` (también sirve
  `map nombre_del_demo.dm2`).

Dentro del demo presiona **H** para ver la ayuda con todas las teclas.

Para saber si estás en el visor: la ventana se llama **VisorDemos** y abajo
aparece la barra con *H: AYUDA*. Si no ves eso, el demo se abrió con tu
Quake 2 normal.

---

## 3. Controles

| Tecla | Qué hace |
|---|---|
| **Flecha abajo** | Pausa / continuar |
| **Flecha izquierda / derecha** | Retroceder / adelantar 10 segundos |
| **Z / X** | Más lento / más rápido (x0.1 a x4). También Av Pág / Re Pág |
| **Retroceso** (Backspace) | Velocidad normal |
| **F1** (mantener) | Tabla de puntos, solo si quien grabó la abrió en ese momento |
| **N / B** (o **Shift + flecha derecha / izquierda**) | Saltar al siguiente / anterior frag o muerte del jugador que ves o sigues |
| **Flecha arriba** | Cámara libre sí / no |
| **W A S D + mouse** | Mover y mirar con la cámara libre |
| **Espacio / Ctrl** | Subir / bajar |
| **Shift** (mantener) | Mover la cámara más rápido |
| **Rueda del mouse** (o **+ / −** del teclado numérico) | Velocidad de la cámara libre; siguiendo a alguien, acercar / alejar |
| **E / Q** | Seguir al siguiente / anterior jugador |
| **R** | Volver a la vista de quien grabó el demo |
| **C** | Grabar recorrido de cámara / terminar |
| **V** | Usar el recorrido grabado sí / no |
| **I** | Marcar inicio del clip |
| **O** | Marcar fin del clip |
| **G** | Grabar el clip a video |
| **Supr** | Borrar las marcas de inicio y fin |
| **K** | Kill feed: sí / solo el jugador que ves / no |
| **J** | Chat: sí / con avisos del servidor / no |
| **H** | Mostrar / ocultar la ayuda (teclas a un lado, ajustes de consola con su valor al otro) |
| **Esc** | Menú (*browse demos* para abrir otro demo, salir, etc.) |

Las teclas están en `baseq2/visor.cfg`; puedes cambiarlas ahí.

Arriba al centro aparece el nick del jugador que estás viendo (*VIENDO:*,
la vista de quien grabó) o siguiendo (*SIGUIENDO:*). Si grabaste el demo
como espectador siguiendo a alguien, muestra a quién seguías (y oculta el
texto de espectador de D-Day, que repetía lo mismo); si volabas libre, dice
*OBSERVADOR*. Con la cámara libre no aparece ningún nick.

La barra de abajo muestra el tiempo, el estado (pausa, velocidad, cámara) y
las marcas: **verde** = inicio, **rojo** = fin, **naranjo** = el clip,
**azul** (línea fina) = tramo con recorrido de cámara grabado, **amarillo**
(rayitas arriba) = frags del jugador que ves o sigues, **rojo** (rayitas
abajo) = sus muertes. Nada de esto sale en los videos.

---

## 4. Hacer un clip, paso a paso

1. Busca la jugada con las flechas (izquierda/derecha) y pausa con flecha abajo.
2. Ve un poco antes de la jugada y presiona **I** (inicio).
3. Avanza hasta el final de la jugada y presiona **O** (fin).
4. Elige la cámara:
   - **Vista del jugador**: no hagas nada (o presiona **R**).
   - **Seguir a un jugador**: **E / Q** hasta encontrarlo.
   - **Toma con movimiento** (paneos, vuelos): ver el punto 5.
5. Presiona **G**. El demo salta al inicio, se graba hasta el fin y se pausa.
   Arriba aparece "GRABANDO" con el porcentaje. Al terminar dice
   "Clip guardado".

Los videos quedan en la carpeta **`videos`** del mod
(`Quake2\dday\videos\` en Windows). Nunca se sobrescriben: si el nombre
existe, se agrega un número.

Mientras se graba el clip el juego puede verse más lento o más rápido que lo
normal: es esperado. El video sale perfecto igual, porque se genera cuadro
a cuadro.

### Tomas con movimiento: recorrido de cámara

1. Ve al inicio del clip y presiona **C**: empieza a grabar la cámara.
2. Deja correr el demo y mueve la cámara como quieras (vuela con la cámara
   libre, sigue jugadores, cambia entre ellos). Puedes usar cámara lenta
   (**Z**) para tener más control.
3. Presiona **C** de nuevo para terminar. El recorrido se guarda solo.
4. Presiona **G**: el clip repite tu recorrido, suavizado.

Si un tramo no te gustó, vuelve a ese momento y graba solo ese tramo con
**C**: se reemplaza únicamente esa parte. **V** activa o desactiva el
recorrido. Los recorridos se guardan por demo en la carpeta `campaths` y se
cargan solos la próxima vez que abras el mismo demo.

### Muertes y chat

Arriba a la derecha aparece quién mató a quién, en corto (`asesino  sniped  víctima`),
con el color de cada equipo; `HS` es headshot y `TK` fuego amigo. Se resaltan
las líneas del jugador que ves o sigues. Abajo a la izquierda va el chat.
Van con el tiempo del demo (en pausa se quedan, en cámara lenta duran más).

Al abrir un demo el visor lo lee entero una vez (toma una fracción de
segundo), así que después de cualquier salto ves las muertes y el chat de
los segundos anteriores, y un clip empieza con el kill feed ya lleno. Con
**N** / **B** (o **Shift + flechas**) saltas al siguiente / anterior frag o muerte del jugador que
ves o sigues, un par de segundos antes, en orden (en el centro dice
*Frag 3 de 14* o *Muerte 2 de 9*). En la barra sus frags se marcan en
amarillo (arriba) y sus muertes en rojo (abajo). Sin jugador (cámara
libre) salta entre todas las muertes.

En los clips sale el kill feed y no el chat: `clip_chat 1` lo agrega y
`clip_killfeed 0` lo quita. `visor_killfeed 2` / `clip_killfeed 2` muestran
solo las muertes del jugador que ves.

---

## 5. Llevar los clips al editor de video

Por defecto los clips salen en **MP4 (H.264 + AAC), 1920×1080, 60 fps**,
el formato que abre cualquier editor: Premiere, Vegas, CapCut, Filmora,
Shotcut, Kdenlive, DaVinci Resolve en Windows y Mac.

- Importa los clips, ordénalos en la línea de tiempo y agrega música,
  textos y transiciones.
- Para cámara lenta extra en el editor conviene grabar el clip ya en cámara
  lenta dentro del visor (**Z** antes de **G**): queda fluido porque se
  generan todos los cuadros.
- Graba los clips **sin HUD** (es lo predeterminado; solo sale el kill feed)
  y agrega los textos en el editor. Si quieres el HUD del juego en el video: `clip_hud 1` en la
  consola.

**DaVinci Resolve gratuito en Linux** no abre H.264/AAC. En ese caso exporta
en MOV (ProRes + audio sin comprimir): en la consola escribe
`clip_format mov`. Los archivos MOV son mucho más grandes (calidad de edición
profesional).

---

## 6. Ajustes (consola)

Abre la consola con la tecla **`** (al lado del 1) y escribe, por ejemplo,
`clip_size 2560x1440`. Los ajustes se guardan.

| Ajuste | Por defecto | Qué hace |
|---|---|---|
| `clip_size` | `1920x1080` | Resolución del video. Vacío (`clip_size ""`) = tamaño de la ventana |
| `clip_fps` | `60` | Cuadros por segundo del video |
| `clip_format` | `mp4` | `mp4` (H.264+AAC) o `mov` (ProRes+PCM) |
| `clip_quality` | `16` | Calidad H.264 en Linux (menor = mejor; 14 a 20 está bien) |
| `clip_hud` | `0` | `1` = incluir el HUD del juego en el video |
| `clip_name` | `0` | `1` = el nick del jugador que se ve o se sigue sale en el video |
| `visor_name` | `1` | Muestra arriba el nick del jugador que ves o sigues (`0` = no) |
| `visor_killfeed` | `1` | Quién mató a quién, arriba a la derecha. `2` = solo el jugador que ves, `0` = no |
| `visor_chat` | `1` | Chat abajo a la izquierda. `2` = también avisos del servidor, `0` = no |
| `clip_killfeed` | `1` | Lo mismo, en el video (`0`, `1` o `2`) |
| `clip_chat` | `0` | Chat en el video (`0`, `1` o `2`) |
| `visor_feed_size` | `1` | Tamaño de la letra del kill feed y el chat (0.5 a 3) |
| `visor_killfeed_time` / `visor_chat_time` | `6` / `10` | Segundos que queda cada línea |
| `visor_killfeed_lines` / `visor_chat_lines` | `5` / `6` | Cuántas líneas se muestran a la vez |
| `clip_volume` | `0.7` | Volumen del sonido en el video |
| `freecam_speed` | `300` | Velocidad de la cámara libre (también con la rueda del mouse) |
| `freecam_fov` | `90` | Campo de visión de la cámara libre / al seguir jugadores (no usa el zoom del rifle) |
| `chase_dist` | `110` | Distancia al jugador que sigues (también con la rueda del mouse) |
| `campath_smooth` | `0.25` | Suavizado del recorrido de cámara (segundos) |
| `visor_bar` | `1` | `0` = ocultar la barra de tiempo |
| `visor_help_pos` | `right` | Lado del panel de ayuda (H): `right` o `left` |
| `visor_help_size` | `0.8` | Tamaño de la letra de la ayuda (0.5 a 1.5) |

Comandos útiles: `clip_in`, `clip_out`, `clip_export nombre`,
`capture_start nombre` / `capture_stop` (grabar a mano, sin marcas),
`demo_speed 0.5`, `campath_clear`.

Para mejor calidad de imagen el visor usa la ventana tal como la tengas.
Si tu pantalla no es 16:9, el video se recorta al centro para quedar en 16:9.

---

## 7. Límites y problemas comunes

- **En un `.dm2` solo existe lo que veía quien grabó.** El servidor solo
  manda los jugadores cercanos o visibles. Si alejas la cámara, puede haber
  jugadores que no aparecen o que aparecen de golpe, y si quien grabó estaba
  encerrado, no habrá a quién seguir ("No hay jugadores a la vista"). Los
  demos **`.mvd2`** (grabados por el servidor) traen a todos los jugadores.
- En algunos servidores el `.dm2` no incluye el cuerpo de quien grabó, así
  que con la cámara libre no se ve a ese jugador.
- **MVD**: la cámara libre y el seguimiento usan los modos propios de MVD
  (flecha arriba = volar / seguir, E / Q = cambiar de jugador). El recorrido
  de cámara (C / V) por ahora solo funciona con `.dm2`.
- **El video no tiene sonido**: el audio se graba con el sonido por
  software. En la consola: `s_enable 1`, luego `snd_restart`.
- **"cambió el tamaño de la ventana"**: no cambies el tamaño de la ventana
  ni pases a pantalla completa mientras se graba un clip.
- **Windows: no abre al hacer doble clic en un demo**: ejecuta
  `asociar-demos.bat` o usa clic derecho → Abrir con → VisorDemos.exe.
- **No veo la barra o las teclas del visor no hacen nada**: revisa que la
  ventana se llame *VisorDemos*. Si dice otra cosa, el demo se abrió con otro
  programa (tu Quake 2 normal).
- **Las teclas de velocidad**: usa **Z / X** o **Av Pág / Re Pág**. Los
  símbolos `[` `]` no funcionan en teclados en español.

---

## Licencia

VisorDemos se basa en q2pro y se distribuye bajo la licencia GNU GPL
(versión 2 o posterior); ver `LICENSE`. El código fuente está en el mismo
repositorio que este archivo.
