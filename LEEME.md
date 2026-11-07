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
  desde sus ojos o desde atrás (tecla F).
- **Grabar un recorrido de cámara** mientras miras el demo, para que el video
  repita ese movimiento suave.
- **Poner puntos de cámara** (como en Keygrip): la cámara viaja sola entre
  ellos en una curva suave, y puede ir mirando o siguiendo a un jugador.
- **Cámara lenta por tramo (efecto Matrix)**: entre dos puntos el juego va
  lento y la cámara se sigue moviendo a su ritmo.
- **Seguir granadas, cohetes y cuchillos** con la cámara, desde atrás, y
  verles una **estela tipo láser**.
- Ver el **nombre de cada jugador** sobre su cabeza, también detrás de las
  paredes.
- Hacer todo **con el mouse** en el modo editor (tecla **Tab**): pestañas
  con botones, listas de puntos y jugadores, y una barra de tiempo con clic.
- Marcar inicio y fin de una jugada y **exportarla a video** (MP4 o MOV) con
  imagen y sonido, a 1080p y 60 cuadros por segundo, o grabar **el demo
  completo** de una vez.
- Ver el **kill feed** y el chat, saltar de frag en frag y usar el **x-ray**
  para ver a los jugadores detrás de las paredes.

VisorDemos **no toca tu configuración del juego**: guarda la suya aparte, en
`visordemos.cfg`.

### Novedades de la versión 0.6

- **Saltar a un punto de cámara muestra su vista**: con **1 / 2** o un clic
  en la barra, la cámara queda exactamente donde la guardaste, con su zoom,
  lista para corregirla.
- **Editor con pestañas** (CAMARA, PUNTOS, VER, VIDEO, AJUSTES), una lista
  de puntos o de jugadores a la izquierda y los ajustes del punto a la
  derecha. Casi todo se maneja con el mouse, también los números (**−** /
  **+**). Un clic sobre un jugador o una granada en la imagen lo sigue. Ver
  [Modo editor](#modo-editor-con-mouse).
- **Nombres sobre los jugadores** (**Y**), también detrás de las paredes.
  Ver [Nombres](#nombres-sobre-los-jugadores).
- **Cámara lenta por tramo** (efecto Matrix, **3 / 4**). Ver
  [Cámara lenta](#cámara-lenta-por-tramo-efecto-matrix).
- **Seguir granadas, cohetes, cuchillos y bombas** (**5**, clic, o
  **< Proyectil >**), en vivo o desde un punto de cámara. Ver
  [Seguir proyectiles](#seguir-granadas-cohetes-y-cuchillos).
- **Estelas tipo láser** detrás de los proyectiles (**6**). Ver
  [Estelas](#estelas).
- Los puntos de cámara pueden **seguir a un jugador** desde atrás (**L**
  dos veces), y **Ver recorrido** dibuja en el mapa por dónde pasa la cámara.
- **Los `.mvd2` tienen todo**: puntos de cámara, recorrido, cámara lenta,
  proyectiles, estelas y nombres, con todos los jugadores de la partida.
- Arreglos: grabar un clip de un `.mvd2` que estaba en pausa ya no deja la
  imagen congelada.

Al actualizar, copia también `baseq2\visor.cfg` del `.zip`: ahí vienen las
teclas nuevas (**3**, **4**, **5**, **6** e **Y**).

---

## 1. Instalación

### Windows

1. Descomprime el `.zip`.
2. Copia **todo el contenido** de la carpeta `VisorDemos` dentro de tu carpeta
   de Quake 2 (la que tiene `baseq2` y `dday`). Quedará así:

   ```
   Quake2\
     VisorDemos.exe
     VisorDemos Editor.bat
     asociar-demos.bat
     1-Instalacion.html
     2-Guia-de-uso.html
     3-Tutorial-tomas.html
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
5. Para hacer clips con el mouse, abre **`VisorDemos Editor.bat`** (o
   arrastra un demo sobre él): abre el visor en modo editor, con botones
   (ver [Modo editor](#modo-editor-con-mouse)).

**Actualizar a una versión nueva:** baja el `.zip` nuevo y reemplaza
`VisorDemos.exe`, `VisorDemos Editor.bat` y `baseq2\visor.cfg`. Tus videos, recorridos y ajustes se
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
./instalar-linux.sh      # menu de aplicaciones (VisorDemos y VisorDemos Editor) + doble clic en .dm2/.mvd2
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

Dentro del demo presiona **H** para ver la ayuda con todas las teclas, o
**Tab** para usar el visor con el mouse.

### Modo editor (con mouse)

Si prefieres hacer los clips con el mouse, presiona **Tab** (o abre
*VisorDemos Editor*). Aparecen un panel abajo y dos paneles chicos sobre la
imagen:

- **Barra de tiempo**: haz clic o arrastra para ir a cualquier momento. Los
  cuadraditos celestes son los puntos de cámara: clic en uno para ir a él.
  Los tramos en cámara lenta se pintan en morado y los lanzamientos de
  granadas, cohetes y cuchillos son rayitas naranjas abajo.
- **Fila fija**: debajo de la barra, siempre a mano: reproducción,
  velocidad, volumen y, a la derecha, *Abrir demo*, *Ayuda* y *Ocultar
  editor*.
- **Pestañas**: **CAMARA**, **PUNTOS**, **VER**, **VIDEO** y **AJUSTES**.
  Cada una muestra sus botones debajo. **VER** cambia lo que sale en la
  pantalla y **VIDEO** lo que sale en los videos, con los mismos botones en
  el mismo orden. El editor recuerda la última pestaña.
- **Panel izquierdo**: la lista de **PUNTOS** de cámara (clic en uno para ir
  a él) o de **JUGADORES** (clic en uno para seguirlo; en gris los que no
  están en el demo en ese momento, en azul / rojo según el equipo).
- **Panel derecho**: los ajustes del punto de cámara donde estás
  (movimiento, objetivo, velocidad, campo de visión, actualizar, borrar), o
  de lo que estás siguiendo. Si no estás en un punto, un botón para poner
  uno.
- **Botones**: hacen lo mismo que las teclas (ver la tabla de abajo). Al
  pasar el mouse por encima, arriba del panel dice qué hace cada uno y cuál
  es su tecla. Los encendidos se ven en celeste; los grises no se pueden usar
  en ese momento. Los números tienen **−** / **+** a los lados.
- **Clic izquierdo sobre la imagen**: sigue al jugador, granada, cohete o
  cuchillo que esté bajo el mouse (arriba del panel dice a quién).
- **Mover la cámara**: mantén el **clic derecho** sobre la imagen y mueve el
  mouse para mirar; con el clic derecho apretado vuela con **W A S D**,
  **Espacio** / **Ctrl** para subir / bajar y **Shift** para ir rápido. Al
  soltar el clic vuelve el cursor. Si estás siguiendo un proyectil, el clic
  derecho gira la cámara alrededor de él.
- Todas las teclas siguen funcionando. **Tab** vuelve a la barra simple; el
  visor recuerda el último modo que usaste.

#### Botones del modo editor

| Dónde | Botón | Qué hace | Tecla |
|---|---|---|---|
| Fila fija | **−10 s** / **+10 s** | Retroceder / adelantar 10 segundos | Flechas izquierda / derecha |
| Fila fija | **< Frag** / **Frag >** | Frag o muerte anterior / siguiente del jugador que ves (sin jugador: de todos) | B / N |
| Fila fija | **\|\| Pausa** / **> Seguir** | Pausa / continuar (en pausa se ve en celeste) | Flecha abajo |
| Fila fija | Velocidad **−** / **+** | Más lento (cámara lenta) / más rápido, de x0.1 a x4 | Z / X |
| Fila fija | Velocidad **x1** (muestra la velocidad) | Volver a la velocidad normal | Retroceso |
| Fila fija | Volumen **−** / **+** | Bajar / subir el volumen del visor (en medio, el volumen en %) | , / . |
| Fila fija | **Abrir demo** | Elegir otro demo | Esc |
| Fila fija | **Ayuda** | Mostrar / ocultar la ayuda con teclas y ajustes | H |
| Fila fija | **Ocultar editor** | Volver a la barra simple y al teclado | Tab |
| CAMARA | **Libre** | Cámara libre sí / no (también: clic derecho mantenido) | Flecha arriba |
| CAMARA | **< Jugador** / **Jugador >** | Seguir al jugador anterior / siguiente | Q / E |
| CAMARA | **1a / 3a** | Primera / tercera persona al seguir a alguien | F |
| CAMARA | **Original** | Volver a la vista de quien grabó el demo | R |
| CAMARA | **< Proyectil** / **Proyectil >** | Ir a la granada, cohete o cuchillo lanzado anterior / siguiente y seguirlo | — |
| CAMARA | **Seguir proyectil** | Seguir el proyectil del centro de la pantalla (también: clic sobre él) | 5 |
| CAMARA | **Angulo: atras** | La cámara vuelve justo detrás del proyectil, un poco arriba | — |
| CAMARA | **DISTANCIA** / **ALTURA** − + | Distancia y altura de la cámara al seguir a un jugador en tercera persona | Rueda del mouse (distancia) |
| CAMARA | **FOV** − + | Campo de visión de la cámara libre (menos = más zoom) | — |
| CAMARA | **VUELO** − + | Velocidad de la cámara libre | Rueda del mouse |
| CAMARA | **PROYECTIL** − + | Distancia de la cámara al seguir un proyectil | — |
| PUNTOS | **+ Punto** / **Actualizar punto** | Poner un punto de cámara aquí con lo que ves; si ya hay uno aquí, lo actualiza | P |
| PUNTOS | **< Punto** / **Punto >** | Ir al punto anterior / siguiente: pausa y pone la cámara en la vista del punto | 1 / 2 |
| PUNTOS | **Usar camara** | Usar los puntos y el recorrido grabado sí / no | V |
| PUNTOS | **Grabar recorrido** / **Terminar** | Grabar la cámara mientras corre el demo / terminar | C |
| PUNTOS | **Ver recorrido** | Dibuja en el mapa la línea por donde pasa la cámara y una marca donde mira cada punto (no sale en el video) | — |
| PUNTOS | **BORRAR TODO** | Borra todos los puntos y el recorrido grabado de este demo. Pide un segundo clic en **SI, BORRAR TODO** (4 segundos) | — |
| VER | **X-ray** | Siluetas de los jugadores detrás de las paredes | T |
| VER | **Estelas: todos / el que sigo / no** | Estelas de los proyectiles **en pantalla** | 6 |
| VER | **Nombres** | Nicks sobre los jugadores **en pantalla** | Y |
| VER | **Muertes** | Kill feed **en pantalla**: sí / solo el jugador (dice "Muertes: jugador") / no | K |
| VER | **Chat** | Chat **en pantalla**: sí / con avisos del servidor (dice "Chat + avisos") / no | J |
| VER | **HUD** | HUD del juego en pantalla con la cámara libre o siguiendo a alguien | — |
| VER | **Nick arriba** | Nick del jugador que ves, arriba al centro | — |
| VER | **Hitmarker** | La X del hitmarker en el centro cuando el jugador le pega a alguien (servidor con `hitmarkers 2`) | — |
| VIDEO | **Inicio** / **Fin** | Marcar el inicio / fin del clip aquí | I / O |
| VIDEO | **Borrar marcas** | Borrar las marcas de inicio y fin | Supr |
| VIDEO | **GRABAR CLIP** | Grabar a video de la marca de inicio a la de fin | G |
| VIDEO | **DEMO COMPLETO** | Grabar todo el demo a video, sin marcar inicio ni fin | — |
| VIDEO | **Estelas**, **Nombres**, **Muertes**, **Chat**, **HUD**, **Nick arriba**, **Hitmarker** | Lo mismo que en VER, pero **en los videos** (no cambian la pantalla). La X del hitmarker sale aunque el HUD esté apagado | — |
| VIDEO | **CALIDAD** − + | Calidad del video (alta / media / baja; más alta = archivo más grande) | — |
| VIDEO | **FPS** − + | Cuadros por segundo del video | — |
| AJUSTES | **PANEL** / **KILL FEED** − + | Tamaño de la letra del editor / del kill feed y el chat | — |
| AJUSTES | **MOUSE** − + | Sensibilidad del mouse al mover la cámara | — |
| AJUSTES | **NOMBRES MIN** / **MAX** − + | Tamaño de los nombres lejos / cerca | — |
| AJUSTES | **ESTELA** / **GROSOR** − + | Cuánto dura y qué tan gruesa es la estela | — |
| AJUSTES | **Color: por tipo** | Color de las estelas: por tipo o uno solo (cambia con cada clic) | — |
| AJUSTES | **X-RAY** − + / **Color: por equipo** | Opacidad del x-ray / colores por equipo o uno solo | — |
| AJUSTES | **Paneles a los lados** | Mostrar / ocultar los paneles izquierdo y derecho | — |
| Panel izquierdo | **PUNTOS** / **JUGADORES** | Lista de puntos (clic: ir al punto) o de jugadores (clic: seguirlo) | — |
| Panel derecho | **Movimiento: Curva / Recta** | Del punto al siguiente: curva suave o línea recta | M |
| Panel derecho | **Objetivo: Nada / Mira a / Sigue a** | Cada clic: mirar al jugador del centro de la pantalla, seguirlo desde atrás, seguirlo en sus ojos (hasta el punto siguiente), o nada | L |
| Panel derecho | **Vista: 1a / 3a persona** | Del punto al siguiente, seguir al jugador en primera o tercera persona (sin jugador: el que ves) | 7 |
| Panel derecho (siguiendo en 3a persona) | **Distancia**, **Angulo**, **Altura** − + / **Camara normal** | Dónde va la cámara alrededor del jugador en ese punto: lejos o cerca, girada (0 = detrás, 90 = al costado, 180 = de frente), más arriba o más abajo / volver a lo de siempre | P (siguiéndolo con E) |
| Panel derecho | **Seguir proyectil** | Del punto al siguiente, seguir la granada, cohete o cuchillo que sigues o del centro de la pantalla | — |
| Panel derecho | **Velocidad** − + | Cámara lenta de este punto al siguiente (efecto Matrix), x0.05 a x4 | 3 / 4 |
| Panel derecho | **FOV** − + | Campo de visión del punto (menos = más zoom) | — |
| Panel derecho | **Actualizar** / **Borrar** | Guardar en el punto lo que ves ahora / borrarlo | P / U |
| Panel derecho (siguiendo un proyectil) | **Atras**, **Distancia**, **Camara libre**, **Original** | Volver al ángulo de atrás, alejar / acercar, dejar de seguirlo | — |
| Mientras graba | **DETENER GRABACION** | Es el único botón mientras se graba: corta y guarda lo grabado | G |

Si ocultas los paneles a los lados, los botones del punto (**Curva /
Recta**, **Mirar jugador**, **Borrar punto**) aparecen en la pestaña
**PUNTOS**.

Todos los botones funcionan con demos `.dm2` y `.mvd2`. Solo en una
transmisión en vivo (GTV) los de puntos de cámara, recorrido y proyectiles
se ven grises. El panel derecho muestra los
ajustes de un punto cuando estás justo en él (clic en su cuadradito, en la
lista o 1 / 2).

El panel no sale en los videos. Paso a paso, con dibujos:
[tutorial de tomas con movimiento](https://awho-coder.github.io/VisorDemos/tutorial-tomas.html)
(también viene en el `.zip` como `3-Tutorial-tomas.html`).

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
| **Rueda del mouse** (o **+ / −** del teclado numérico) | Velocidad de la cámara libre; siguiendo a alguien en tercera persona, acercar / alejar |
| **E / Q** | Seguir al siguiente / anterior jugador (en la perspectiva que tengas) |
| **F** | Primera / tercera persona al ver a un jugador |
| **R** | Volver a la vista de quien grabó el demo |
| **C** | Grabar recorrido de cámara / terminar |
| **V** | Usar el recorrido grabado y los puntos de cámara sí / no |
| **P** | Poner un punto de cámara aquí (o actualizarlo) |
| **L** | Objetivo del punto: mirar al jugador del centro de la pantalla → seguirlo → en sus ojos → nada |
| **7** | El punto sigue al jugador en primera / tercera persona |
| **M** | Del punto al siguiente: curva suave / recta |
| **U** | Borrar el punto de cámara |
| **1 / 2** | Ir al punto de cámara anterior / siguiente (la cámara queda en la vista del punto) |
| **3 / 4** | Cámara lenta de este punto al siguiente: más lento / más rápido (efecto Matrix) |
| **5** | Seguir la granada, cohete o cuchillo del centro de la pantalla |
| **6** | Estelas de los proyectiles: todos / solo el que sigues / no |
| **Y** | Nombres sobre los jugadores sí / no |
| **I** | Marcar inicio del clip |
| **O** | Marcar fin del clip |
| **G** | Grabar el clip a video (si ya está grabando: detener) |
| **Supr** | Borrar las marcas de inicio y fin |
| **K** | Kill feed: sí / solo el jugador que ves / no |
| **,** / **.** | Bajar / subir el volumen |
| **J** | Chat: sí / con avisos del servidor / no |
| **T** | X-ray: siluetas de los jugadores detrás de las paredes sí / no |
| **H** | Mostrar / ocultar la ayuda (teclas a un lado, ajustes de consola con su valor al otro) |
| **Tab** | Modo editor con mouse sí / no (pestañas, listas y barra de tiempo con clic) |
| **Clic izquierdo** (modo editor) | Seguir al jugador o proyectil que está bajo el mouse |
| **Esc** | Menú (*browse demos* para abrir otro demo, salir, etc.) |

Las teclas están en `baseq2/visor.cfg`; puedes cambiarlas ahí.

Arriba al centro aparece el nick del jugador que estás viendo (*VIENDO:*,
la vista de quien grabó) o siguiendo (*SIGUIENDO:*). Si grabaste el demo
como espectador siguiendo a alguien, muestra a quién seguías (y oculta el
texto de espectador de D-Day, que repetía lo mismo); si volabas libre, dice
*OBSERVADOR*. Con la cámara libre no aparece ningún nick.

La barra de abajo muestra el tiempo, el estado (pausa, velocidad, cámara) y
las marcas: **verde** = inicio, **rojo** = fin, **naranjo** = el clip,
**azul** (línea fina) = tramo con recorrido de cámara grabado, **celeste**
(cuadraditos) = puntos de cámara, **morado** = tramo en cámara lenta,
**naranjo** (rayitas abajo, en el editor) = lanzamientos de granadas,
cohetes y cuchillos, **amarillo**
(rayitas arriba) = frags del jugador que ves o sigues, **rojo** (rayitas
abajo) = sus muertes. Nada de esto sale en los videos.

---

## 4. Hacer un clip, paso a paso

1. Busca la jugada con las flechas (izquierda/derecha) y pausa con flecha abajo.
2. Ve un poco antes de la jugada y presiona **I** (inicio).
3. Avanza hasta el final de la jugada y presiona **O** (fin).
4. Elige la cámara:
   - **Vista del jugador**: no hagas nada (o presiona **R**).
   - **Seguir a un jugador**: **E / Q** hasta encontrarlo; **F** cambia entre
     sus ojos (primera persona) y detrás de él (tercera persona).
   - **Toma con movimiento** (paneos, vuelos): ver el punto 5.
5. Presiona **G**. El demo salta al inicio, se graba hasta el fin y se pausa.
   Arriba aparece "GRABANDO" con el porcentaje. Al terminar dice
   "Clip guardado".

**Todo el demo de una vez:** en el modo editor, el botón **Demo completo**
(o `clip_export_all` en la consola) graba el demo entero, del principio al
final, sin marcar inicio ni fin. Usa la cámara que tengas (puntos de cámara,
recorrido o seguir a un jugador). Para cortar antes: **Detener grabación** o
**G**; lo grabado hasta ahí se guarda. El archivo se llama
`<demo>_completo.mp4`. Al terminar, el demo se cierra como siempre.

Los videos quedan en la carpeta **`videos`** del mod
(`Quake2\dday\videos\` en Windows). Nunca se sobrescriben: si el nombre
existe, se agrega un número.

Mientras se graba el clip el juego puede verse más lento o más rápido que lo
normal: es esperado. El video sale perfecto igual, porque se genera cuadro
a cuadro.

### Primera y tercera persona

**F** cambia entre ver por los ojos del jugador (primera persona) y verlo
desde atrás (tercera persona). La elección se mantiene: con **E / Q** pasas
al siguiente jugador en la misma perspectiva, y si sueltas la cámara con
**flecha arriba**, al volver a presionarla regresas al mismo jugador. En
tercera persona la rueda del mouse acerca o aleja la cámara.

En un `.dm2`, la primera persona de otro jugador (no de quien grabó) se arma
con lo que trae el demo: su posición y hacia dónde mira. No se ve su arma ni
su HUD, y la mirada es algo menos precisa que la del jugador que grabó. En
un `.mvd2` todos los jugadores se ven como en el juego, con arma y HUD.

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

### Puntos de cámara (como en Keygrip)

En vez de actuar la toma, le dices al visor dónde tiene que estar la cámara
en algunos momentos y él la mueve sola entre esos puntos, en una curva suave.

1. Pausa (**flecha abajo**) donde empieza la toma, suelta la cámara
   (**flecha arriba**) y ubícala.
2. Presiona **P**: queda un punto (cuadradito celeste en la barra).
3. Avanza el demo, mueve la cámara a la siguiente posición y presiona **P**
   otra vez. Repite las veces que quieras.
4. Vuelve al inicio y deja correr el demo: la cámara viaja sola por los
   puntos (abajo dice *PUNTOS DE CAMARA*). Marca inicio y fin y graba con **G**.

- **Mirar o seguir a un jugador**: en un punto, apunta a un jugador (al
  centro de la pantalla) y presiona **L**. Desde ese punto la cámara gira
  sola para mirarlo mientras viaja. Si dos puntos miran a jugadores
  distintos, la cámara pasa de uno a otro. **L** otra vez hace que lo
  **siga** desde atrás hasta el punto siguiente (la cámara entra y sale de
  a poco), otra vez lo sigue **en sus ojos** y una cuarta vez vuelve a la
  mirada libre.
- **Primera o tercera persona**: **7** (o **Vista** en el panel derecho)
  hace que el punto siga al jugador en primera o en tercera persona. Si el
  punto no tiene jugador usa el que estás viendo: viendo el demo normal, es
  quien lo grabó, y el tramo se ve como el demo (con el arma y el HUD). De un
  punto en primera a otro en tercera del mismo jugador, la cámara sale de
  sus ojos en los últimos 2 segundos antes del punto siguiente (y entra al
  revés), siempre mirándolo. Con otros jugadores la primera persona es
  aproximada y sin arma: el demo no la trae.
- **Distancia y ángulo en tercera persona**: cada punto que sigue a un
  jugador desde atrás tiene su **Distancia**, **Angulo** (0 = detrás, 90 =
  al costado, 180 = de frente) y **Altura** en el panel derecho. También:
  ve al punto, síguelo con **E**, gira con el mouse, aleja con la rueda y
  presiona **P**. Si dos puntos seguidos lo siguen con ángulos distintos, la
  cámara da la vuelta a su alrededor de uno a otro.
- **Corregir un punto**: **1 / 2** (o un clic en la lista o en su
  cuadradito) saltan al punto, pausan y dejan la cámara **exactamente en la
  vista guardada**. Muévela desde ahí y presiona **P**: se actualiza. **U**
  lo borra.
- **Agregar un punto en medio**: deja correr el demo y pausa donde quieras;
  la cámara libre sigue desde donde iba. Muévela y presiona **P**.
- **Curva o recta**: **M** cambia cómo va la cámara de ese punto al
  siguiente.
- Cada punto guarda también el zoom (**FOV** en el panel derecho), así que
  puedes hacer acercamientos.
- **Ver recorrido** (pestaña PUNTOS) dibuja en el mapa la línea por donde
  pasará la cámara y una marca donde mira cada punto.

Mientras el demo corre, los puntos mandan sobre la cámara. En pausa también
se ve la vista de los puntos, hasta que muevas la cámara: desde ese momento
puedes preparar el siguiente punto. **V** los apaga. Se guardan por demo en
`campaths` (archivo `.keys`).

### Cámara lenta por tramo (efecto Matrix)

Cada punto de cámara tiene una **velocidad** que vale hasta el punto
siguiente. La cámara recorre el tramo en tiempo de video, así que con
**x0.2** la cámara se mueve a su ritmo normal mientras el juego va cinco
veces más lento.

1. Ve al punto donde empieza la parte lenta (**1 / 2**).
2. Presiona **3** (más lento) o **4** (más rápido), o usa **Velocidad − +**
   en el panel derecho. Va de x0.05 a x4.
3. Deja correr el demo con los puntos activos (**V**): al llegar a ese punto
   el juego se frena de a poco, y al llegar al siguiente vuelve a la
   velocidad normal.

En la barra el tramo lento se pinta morado y arriba dice *TRAMO x0.2*. Los
clips lo graban tal cual: un tramo de 1 segundo a x0.2 dura 5 segundos en el
video. Si además pusiste una velocidad con **Z / X**, las dos se
multiplican. El demo trae 10 cuadros por segundo, así que a x0.1 o menos
los movimientos de los jugadores se ven algo rectos.

### Seguir granadas, cohetes y cuchillos

Al abrir un demo el visor guarda el vuelo de cada granada, cohete y cuchillo
lanzado, y de las bombas del ataque aéreo. Para seguir uno:

- **< Proyectil / Proyectil >** (pestaña CAMARA) saltan al lanzamiento
  anterior / siguiente y lo siguen; en la barra son las rayitas naranjas.
- O busca el momento, pausa, y haz **clic izquierdo** sobre el proyectil (en
  el modo editor), o apunta con el centro de la pantalla y presiona **5**.

La cámara va detrás, un poco arriba. Con el **clic derecho mantenido** la
giras alrededor del proyectil y queda en ese ángulo durante todo el vuelo;
**Angulo: atras** la devuelve. Cuando explota, la cámara se queda 1,5
segundos mirando el lugar y pasa a cámara libre ahí mismo. `proj_list` en la
consola lista todos los lanzamientos del demo.

Para que un clip siga un proyectil, usa un punto de cámara: con la cámara
siguiendo el proyectil presiona **P** (el punto lo sigue hasta el punto
siguiente), o en un punto usa **Seguir proyectil** del panel derecho.

Las balas no se pueden seguir: el juego no las manda como objetos. En un
`.dm2`, si el proyectil se aleja de quien grabó, desaparece del demo; en un
`.mvd2` están todos.

### Estelas

Detrás de cada proyectil queda una **estela tipo láser** que se va
apagando: naranja los cohetes, verde las granadas, blanca los cuchillos y
amarilla las bombas.
La del proyectil que sigues es el doble de gruesa. **6** (o **Estelas** en
VER / VIDEO) cambia entre *todos*, *solo el que sigo* y *no*. Duración,
grosor y color están en AJUSTES (`trail_time`, `trail_width`,
`trail_color`). Las estelas salen en los videos salvo que las apagues en la
pestaña VIDEO.

### Nombres sobre los jugadores

**Y** (o **Nombres** en VER) muestra el nick de cada jugador sobre su
cabeza, con el color de su equipo, también detrás de las paredes. Los que
están lejos se ven más chicos, sin bajar del tamaño mínimo ni pasar del
máximo (**NOMBRES MIN / MAX** en AJUSTES). En los videos se activan aparte,
con **Nombres** en la pestaña VIDEO (`clip_names 1`).

### X-ray: por qué no es un wallhack

Con **T** el visor dibuja a los jugadores que están detrás de las paredes
como una silueta de color (azul los Aliados, rojo el Eje). Sirve para
entender una jugada, ver por dónde venía el enemigo o encontrar a quién
seguir antes de cortar un clip.

Un wallhack es una trampa para **jugar**: te muestra a los enemigos mientras
la partida está en curso. El x-ray del visor no sirve para eso:

- **Solo funciona viendo un demo desde un archivo** (`.dm2` o `.mvd2`). Si
  te conectas a un servidor o miras una transmisión en vivo (GTV), el visor
  lo apaga, y la tecla T solo responde *"El x-ray solo funciona al ver un
  demo desde un archivo"*.
- **El demo es una partida que ya terminó.** Nada de lo que veas cambia el
  resultado ni le da ventaja a nadie en una partida en curso.
- **No muestra nada que el demo no traiga.** La cámara libre ya permite
  atravesar paredes y mirar a cualquier jugador; el x-ray solo lo marca para
  que se vea de un vistazo. En un `.dm2` aparecen solo los jugadores que el
  servidor le mandaba a quien grabó; en un `.mvd2`, que graba el servidor,
  aparecen todos.
- **Es la vista de un espectador o de un caster**, como la que usan las
  transmisiones de torneos para explicar las jugadas.
- **No toca tu Quake 2 ni el juego**: el visor es un programa aparte, y su
  código es abierto, así que cualquiera puede revisar que funciona así
  (`src/client/visor.c`, función `xray_allowed`).

Colores y opacidad: `xray_color`, `xray_color_allies`, `xray_color_axis` y
`xray_alpha` (ver [Ajustes](#6-ajustes-consola)). `visor_players` lista en
la consola los jugadores y su equipo.

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

Lo que sale en los videos se elige aparte de lo que ves en pantalla. Por
defecto los clips llevan el kill feed y no el chat. En el modo editor, la
pestaña **VIDEO** tiene sus propios botones **Muertes / Chat**; los de la
pestaña **VER** y las teclas **K** / **J** solo cambian la pantalla. En la consola: `clip_killfeed 0` quita el kill feed
del video y `clip_chat 1` agrega el chat. `visor_killfeed 2` /
`clip_killfeed 2` muestran solo las muertes del jugador que ves.

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
| `clip_hitmark` | `1` | La X del hitmarker en el video, aunque el HUD esté apagado (`0` = no) |
| `clip_name` | `0` | `1` = el nick del jugador que se ve o se sigue sale en el video |
| `visor_name` | `1` | Muestra arriba el nick del jugador que ves o sigues (`0` = no) |
| `visor_killfeed` | `1` | Quién mató a quién, arriba a la derecha. `2` = solo el jugador que ves, `0` = no |
| `visor_chat` | `1` | Chat abajo a la izquierda. `2` = también avisos del servidor, `0` = no |
| `clip_killfeed` | `1` | Lo mismo, en el video (`0`, `1` o `2`) |
| `clip_chat` | `0` | Chat en el video (`0`, `1` o `2`) |
| `visor_feed_size` | `1` | Tamaño de la letra del kill feed y el chat (0.5 a 3) |
| `visor_killfeed_time` / `visor_chat_time` | `6` / `10` | Segundos que queda cada línea |
| `visor_killfeed_lines` / `visor_chat_lines` | `5` / `6` | Cuántas líneas se muestran a la vez |
| `s_volume` | `0.3` | Volumen del visor (también con las teclas **,** y **.**) |
| `clip_volume` | `auto` | Volumen del sonido en el video: `auto` = el mismo que escuchas en el visor, o un valor fijo de 0 a 1 |
| `freecam_speed` | `300` | Velocidad de la cámara libre (también con la rueda del mouse) |
| `freecam_fast` | `3` | Cuántas veces más rápido va la cámara libre con **Shift** |
| `freecam_smooth` | `0.15` | Suavizado del movimiento de la cámara libre (segundos; `0` = sin suavizado) |
| `freecam_hud` | `0` | `1` = mostrar el HUD del juego con la cámara libre |
| `freecam_fov` | `90` | Campo de visión de la cámara libre / al seguir jugadores (no usa el zoom del rifle) |
| `chase_dist` | `110` | Distancia al jugador que sigues (también con la rueda del mouse) |
| `campath_smooth` | `0.25` | Suavizado del recorrido de cámara (segundos) |
| `keycam_aim_height` | `12` | Altura sobre el jugador a la que miran los puntos con **L** |
| `keycam_blend` | `2` | Segundos que tarda la cámara en pasar de primera a tercera persona (o de un ángulo a otro) antes del punto siguiente |
| `keycam_show` | `0` | `1` = dibujar en el mapa el recorrido de los puntos (**Ver recorrido**) |
| `chase_height` | `28` | Altura de la cámara al seguir a un jugador en tercera persona |
| `chase_auto` | `0.6` | Siguiendo a un jugador en tercera persona, cuánto tarda la cámara en volver sola a su espalda (segundos; `0` = no vuelve) |
| `visor_names` | `1` | Nombres sobre los jugadores en pantalla (también con **Y**) |
| `clip_names` | `0` | `1` = nombres sobre los jugadores en el video |
| `visor_hitmark` | `1` | La X del hitmarker en pantalla, con el HUD (`0` = no) |
| `names_min` / `names_max` | `12` / `24` | Tamaño de los nombres lejos / cerca (alto de la letra en píxeles, en una imagen de 1080 líneas) |
| `names_dist` | `400` | Hasta esta distancia los nombres se ven del tamaño máximo |
| `visor_trails` | `1` | Estelas de los proyectiles en pantalla: `1` todos, `2` solo el que sigues, `0` no (también con **6**) |
| `clip_trails` | `1` | Lo mismo, en el video |
| `trail_time` | `2` | Segundos que dura la estela |
| `trail_width` | `3` | Grosor de la estela (la del proyectil que sigues es el doble) |
| `trail_color` | `tipo` | `tipo` = cohete naranjo, granada verde, cuchillo blanco, bomba amarilla; o uno fijo: `red`, `green`, `blue`, `yellow`, `magenta`, `cyan`, `white`, `orange` |
| `follow_dist` | `90` | Distancia de la cámara al proyectil que sigues |
| `follow_pitch` | `15` | Ángulo desde arriba al empezar a seguir un proyectil (grados) |
| `follow_hold` | `1.5` | Segundos que la cámara mira donde explotó el proyectil antes de pasar a cámara libre |
| `proj_models` | `rocket masher grenade knife` | Partes del nombre del modelo que cuentan como proyectil |
| `visor_bar` | `1` | `0` = ocultar la barra de tiempo |
| `visor_editor` | `0` | `1` = modo editor con mouse (también con **Tab**) |
| `visor_editor_size` | `1` | Tamaño de la letra del panel del editor (0.5 a 3) |
| `visor_editor_tab` | `1` | Pestaña abierta del editor: `0` CAMARA, `1` PUNTOS, `2` VER, `3` VIDEO, `4` AJUSTES |
| `visor_editor_list` | `0` | Lista del panel izquierdo: `0` puntos, `1` jugadores |
| `visor_editor_sides` | `1` | `0` = ocultar los paneles izquierdo y derecho del editor |
| `visor_help_pos` | `right` | Lado del panel de ayuda (H): `right` o `left` |
| `visor_help_size` | `0.8` | Tamaño de la letra de la ayuda (0.5 a 1.5) |
| `xray` | `0` | X-ray encendido (`1`) o apagado (`0`); es lo que cambia la tecla **T** |
| `xray_color` | `team` | Color de las siluetas del x-ray: `team` = por equipo, o uno fijo: `red`, `green`, `blue`, `yellow`, `magenta`, `cyan`, `white` |
| `xray_color_allies` / `xray_color_axis` | `blue` / `red` | Colores de Aliados y Eje con `xray_color team` |
| `xray_alpha` | `0.45` | Opacidad de las siluetas (0.1 a 1) |

### Comandos de consola

Todo lo que hacen las teclas y los botones se puede escribir en la consola
(o usar en tus propias teclas con `bind`, en `baseq2/visor.cfg`).

| Comando | Tecla | Qué hace |
|---|---|---|
| `pause` | Flecha abajo | Pausa / continuar |
| `demo_step -` / `demo_step +` | Flechas izq. / der. | Retroceder / adelantar 10 s. `demo_step + 30` = 30 s |
| `seek 1:30` / `seek 50%` | — | Ir a un momento (minutos:segundos o porcentaje del demo) |
| `demo_speed down` / `up` / `reset` / `0.5` | Z / X / Retroceso | Más lento / más rápido / normal / una velocidad fija (0.1 a 4) |
| `frag_next` / `frag_prev` | N / B | Siguiente / anterior frag o muerte del jugador que ves |
| `visor_volume up` / `down` / `0.5` | . / , | Subir / bajar el volumen, o fijarlo (0 a 1) |
| `freecam` / `freecam 1` / `freecam 0` | Flecha arriba | Cámara libre sí / no |
| `camera_speed up` / `down` / `reset` / `500` | Rueda del mouse | Velocidad de la cámara libre (siguiendo a alguien: distancia) |
| `chase_next` / `chase_prev` | E / Q | Seguir al siguiente / anterior jugador |
| `chase_player 5` | — | Seguir al jugador 5 (`visor_players` muestra los números) |
| `follow` / `follow 3` | 5 | Seguir el proyectil del centro de la pantalla / el número 3 de `proj_list` |
| `follow_angle` | — | Cámara de nuevo justo detrás del proyectil |
| `proj_next` / `proj_prev` | — | Ir al siguiente / anterior lanzamiento y seguirlo |
| `proj_list` | — | Lista en la consola las granadas, cohetes y cuchillos del demo |
| `camera_view` / `camera_view 1` / `camera_view 3` | F | Primera / tercera persona |
| `camera_reset` | R | Volver a la vista de quien grabó |
| `campath_record` | C | Grabar recorrido de cámara / terminar |
| `campath_play` | V | Usar el recorrido y los puntos de cámara sí / no |
| `campath_clear` | — | Borrar el recorrido grabado de este demo |
| `keycam_add` | P | Poner un punto de cámara aquí (o actualizar el de aquí) |
| `keycam_target` | L | Objetivo del punto: mirar al jugador del centro → seguirlo → nada. `keycam_target 5` = mirar al jugador 5 |
| `keycam_follow_proj` | — | Del punto al siguiente, seguir el proyectil que sigues o del centro (otra vez: dejar de seguirlo) |
| `keycam_speed down` / `up` / `reset` / `0.2` | 3 / 4 | Cámara lenta del punto al siguiente (x0.05 a x4) |
| `keycam_fov -5` / `keycam_fov 5` | — | Cambiar el campo de visión del punto de aquí |
| `keycam_mode` | M | Del punto al siguiente: curva / recta |
| `keycam_delete` | U | Borrar el punto de aquí |
| `keycam_prev` / `keycam_next` | 1 / 2 | Ir al punto anterior / siguiente |
| `keycam_goto 3` | — | Ir al punto 3 |
| `keycam_clear` | — | Borrar todos los puntos de cámara de este demo |
| `camera_clear` | — | Borrar todos los puntos y el recorrido (como **BORRAR TODO**) |
| `clip_in` / `clip_out` | I / O | Marcar inicio / fin del clip |
| `clip_clear` | Supr | Borrar las marcas |
| `clip_export` / `clip_export nombre` | G | Grabar el clip a video (con nombre opcional); si ya graba, lo detiene |
| `clip_export_all` | — | Grabar todo el demo a video |
| `clip_stop` | — | Detener la grabación y guardar lo grabado |
| `capture_start nombre` / `capture_stop` / `capture_toggle` | — | Grabar a mano, sin marcas: empieza / termina / alterna |
| `feed_toggle killfeed` / `feed_toggle chat` | K / J | Kill feed / chat **en pantalla**: sí / solo el jugador o con avisos / no |
| `feed_toggle killfeed clip` / `feed_toggle chat clip` | — | Lo mismo, pero **en los videos** |
| `visor_xray` | T | X-ray sí / no |
| `names_toggle` / `names_toggle clip` | Y / — | Nombres sobre los jugadores en pantalla / en el video |
| `trails_toggle` / `trails_toggle clip` | 6 / — | Estelas en pantalla / en el video: todos / el que sigues / no |
| `visor_step ajuste cambio mín máx` | — | Lo que hacen los botones − / +, por ejemplo `visor_step chase_dist 10 16 1000` |
| `visor_players` | — | Lista en la consola los jugadores y su equipo |
| `visor_help` | H | Mostrar / ocultar la ayuda |
| `toggle visor_editor` | Tab | Modo editor con mouse sí / no |
| `demo nombre` | — | Abrir un demo |

Para mejor calidad de imagen el visor usa la ventana tal como la tengas.
Si tu pantalla no es 16:9, el video se recorta al centro para quedar en 16:9.

---

## 7. Límites y problemas comunes

- **En un `.dm2` solo existe lo que veía quien grabó.** El servidor solo
  manda los jugadores cercanos o visibles. Si alejas la cámara, puede haber
  jugadores que no aparecen o que aparecen de golpe, y si quien grabó estaba
  encerrado, no habrá a quién seguir ("No hay jugadores a la vista"). Los
  demos **`.mvd2`** (grabados por el servidor) traen a todos los jugadores.
- **X-ray (T)**: solo funciona al ver un demo desde un archivo, nunca en una
  partida. En un `.dm2` muestra solo a los jugadores que el demo trae (los
  cercanos a quien grabó); en un `.mvd2` muestra a todos.
- En algunos servidores el `.dm2` no incluye el cuerpo de quien grabó, así
  que con la cámara libre no se ve a ese jugador.
- **MVD**: con un `.mvd2` funciona todo igual que con un `.dm2` (cámara
  libre, puntos de cámara, recorrido, cámara lenta, proyectiles, estelas y
  nombres), y además están todos los jugadores y proyectiles de la partida.
  **E / Q**, **R** y **F** usan la vista propia del MVD: por los ojos del
  jugador, con su arma y su HUD. Los puntos de un `.mvd2` se guardan como
  `campaths/<demo>_mvd.keys`. En una transmisión en vivo (GTV) solo están los
  modos de espectador del MVD.
- **Proyectiles**: en un `.dm2` solo están los que pasaron cerca de quien
  grabó; si uno se aleja, desaparece del demo (y la estela se corta). En un
  `.mvd2` están todos. Las balas no existen como objeto y no se pueden seguir.
- **El video no tiene sonido**: el audio se graba con el sonido por
  software. En la consola: `s_enable 1`, luego `snd_restart`.
- **Partes negras en el clip**: la cámara pasó por dentro de una pared o
  quedó fuera del mapa (por ejemplo, un punto de cámara mal ubicado). Mueve
  ese punto a un lugar dentro del mapa.
- **"cambió el tamaño de la ventana"**: no cambies el tamaño de la ventana
  ni pases a pantalla completa mientras se graba un clip.
- **Windows: no abre al hacer doble clic en un demo**: ejecuta
  `asociar-demos.bat` o usa clic derecho → Abrir con → VisorDemos.exe.
- **No veo la barra o las teclas del visor no hacen nada**: revisa que la
  ventana se llame *VisorDemos*. Si dice otra cosa, el demo se abrió con otro
  programa (tu Quake 2 normal).
- **Demos dañados**: algunos demos tienen pedazos rotos (el Quake 2 normal
  dice *Invalid file format* o *illegible server message*). El visor salta
  esas partes y sigue; se pierde a lo más un instante del demo. En la
  consola aparece "Demo danado: ...".
- **Las teclas de velocidad**: usa **Z / X** o **Av Pág / Re Pág**. Los
  símbolos `[` `]` no funcionan en teclados en español.

---

## Licencia

VisorDemos se basa en q2pro y se distribuye bajo la licencia GNU GPL
(versión 2 o posterior); ver `LICENSE`. El código fuente está en el mismo
repositorio que este archivo.
