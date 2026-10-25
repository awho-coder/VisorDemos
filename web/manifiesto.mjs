// Calcula qué archivos del juego necesita una partida (MVD o demo .dm2) para verse
// en el visor web, sin decodificar el demo entero:
//
//   1. Del demo descomprimido saca todos los textos que parecen rutas (configstrings
//      de modelos, sonidos, imágenes y skins, también los que llegan a mitad de la
//      partida) y los cruza con el índice de archivos que existen.
//   2. Del mapa (.bsp) saca las texturas y el cielo.
//   3. De los modelos (.md2) y sprites (.sp2) saca sus skins / cuadros.
//   4. Suma las imágenes que el visor abre siempre (HUD, letras, menú).
//
// Sirve igual en el navegador y en node (pruebas): no usa nada del DOM.
//
//   const { archivos, bytes } = await manifiesto({ indice, demo, leer });
//     indice: { 'ruta/en/minusculas': tamaño }   (index.json de q2web.py)
//     demo:   Uint8Array con el demo YA descomprimido
//     leer:   async (ruta) => Uint8Array        (baja un archivo del índice)

// El visor abre la imagen original (.pcx / .wal) y además la primera mejora que
// exista, en este orden (R_TEXTURE_FORMATS).
const MEJORAS = ['png', 'jpg', 'tga'];

// Imágenes que el visor carga antes de cualquier partida (sacadas de fs_debug).
const BASE = [
  'pics/colormap.pcx', 'pics/conchars', 'pics/conback', 'pics/inventory', 'pics/loading',
  'pics/field_3', 'pics/net', 'pics/pause', 'pics/ch1', 'pics/ch2', 'pics/ch3',
  'pics/num_minus', 'pics/anum_minus',
  ...Array.from({ length: 10 }, (_, i) => `pics/num_${i}`),
  ...Array.from({ length: 10 }, (_, i) => `pics/anum_${i}`),
  ...Array.from({ length: 15 }, (_, i) => `pics/m_cursor${i}`),
  ...['addressbook', 'game', 'load_game', 'multiplayer', 'options', 'save_game', 'start_server', 'video']
    .map((b) => `pics/m_banner_${b}`),
  ...['game', 'multiplayer', 'options', 'quit', 'video']
    .flatMap((b) => [`pics/m_main_${b}`, `pics/m_main_${b}_sel`]),
  'pics/m_main_logo', 'pics/m_main_plaque',
];

// Lo que el cliente carga fijo (src/client/tent.c, precache.c...) y el jugador por defecto.
const FIJOS = [
  'models/objects/dmspot/tris.md2', 'models/objects/explode/tris.md2', 'models/objects/flash/tris.md2',
  'models/objects/smoke/tris.md2', 'models/objects/laser/tris.md2', 'models/objects/r_explode/tris.md2',
  'models/proj/beam/tris.md2', 'models/proj/lightning/tris.md2', 'models/ctf/segment/tris.md2',
  'models/items/armor/effect/tris.md2', 'models/items/c_head/tris.md2', 'sprites/s_bfg2.sp2',
  'players/male/tris.md2', 'players/male/weapon.md2', 'players/male/grunt', 'players/male/grunt_i',
  ...['misc/tele1.wav', 'misc/bigtele.wav', 'player/fall1.wav', 'player/fall2.wav', 'player/land1.wav',
    'items/respawn1.wav', 'world/ric1.wav', 'world/ric2.wav', 'world/ric3.wav', 'world/spark5.wav',
    'world/spark6.wav', 'world/spark7.wav', 'weapons/disrupthit.wav', 'weapons/rocklx1a.wav',
    'weapons/grenlx1a.wav', 'weapons/xpld_wat.wav', 'weapons/tesla.wav', 'weapons/lashit.wav',
    'weapons/marker.wav', 'weapons/railgf1a.wav', 'weapons/shotgf1b.wav', 'weapons/blastf1a.wav',
    'weapons/hyprbf1a.wav', 'weapons/rocklf1a.wav', 'weapons/grenlf1a.wav', 'weapons/sshotf1b.wav',
    // fogonazos y efectos que el cliente arma al vuelo (src/client/effects.c)
    ...[1, 2, 3, 4, 5].map((n) => `weapons/machgf${n}b.wav`),
    'weapons/rocklr1b.wav', 'weapons/grenlr1b.wav', 'weapons/railgr1b.wav', 'weapons/shotgr1b.wav',
    'weapons/plasshot.wav', 'weapons/rippfire.wav', 'weapons/proxlr1a.wav', 'weapons/nail1.wav',
    'weapons/shotg2.wav', 'weapons/bfg__l1a.wav', 'weapons/bfg__f1y.wav', 'weapons/disint2.wav',
    'misc/talk.wav', 'misc/talk1.wav',
    'infantry/infatck1.wav', 'soldier/solatck1.wav', 'soldier/solatck2.wav', 'soldier/solatck3.wav',
    'gunner/gunatck2.wav', 'gunner/gunatck3.wav', 'flyer/flyatck3.wav', 'medic/medatck1.wav',
    'hover/hovatck1.wav', 'floater/fltatck1.wav', 'tank/tnkatck1.wav', 'tank/tnkatck3.wav',
    'tank/rocket.wav', 'chick/chkatck2.wav', 'makron/bfg_fire.wav', 'makron/blaster.wav',
    'boss3/xfire.wav', 'guncmdr/gcdratck2.wav', 'guncmdr/gcdratck3.wav',
    ...['a', 'b', 'c', 'd', 'e'].map((c) => `tank/tnkatk2${c}.wav`),
  ].map((w) => `sound/${w}`),
];

const SKY_SIDES = ['rt', 'bk', 'lf', 'ft', 'up', 'dn'];

function textos(bytes, minimo = 3) {
  const out = new Set();
  let ini = -1;
  for (let i = 0; i <= bytes.length; i++) {
    const c = i < bytes.length ? bytes[i] : 0;
    if (c >= 0x20 && c < 0x7f) {
      if (ini < 0) ini = i;
    } else if (ini >= 0) {
      if (i - ini >= minimo) out.add(String.fromCharCode(...bytes.subarray(ini, i)));
      ini = -1;
    }
  }
  return out;
}

function cadena(bytes, ofs, largo) {
  let fin = ofs;
  while (fin < ofs + largo && bytes[fin]) fin++;
  return String.fromCharCode(...bytes.subarray(ofs, fin));
}

const sinExt = (r) => r.replace(/\.[^./]+$/, '');

export async function manifiesto({ indice, demo, leer }) {
  const archivos = new Set();
  const cache = new Map();
  const modelos = new Set();     // players/<modelo> vistos en las skins
  const pendientes = [];         // md2 / sp2 / bsp que hay que abrir

  const existe = (r) => Object.prototype.hasOwnProperty.call(indice, r);
  const agregar = (r) => {
    if (!existe(r) || archivos.has(r)) return false;
    archivos.add(r);
    if (/\.(md2|sp2|bsp)$/.test(r)) pendientes.push(r);
    return true;
  };
  // imagen: la original (pcx o wal) y la primera mejora que exista
  const imagen = (base, original = 'pcx') => {
    let alguna = existe(`${base}.${original}`);
    agregar(`${base}.${original}`);
    for (const e of MEJORAS) {
      if (existe(`${base}.${e}`)) { agregar(`${base}.${e}`); return true; }
    }
    return alguna;
  };
  // cielo: la primera que exista (png/jpg/tga), si no la pcx
  const cielo = (base) => {
    for (const e of [...MEJORAS, 'pcx']) {
      if (existe(`${base}.${e}`)) { agregar(`${base}.${e}`); return; }
    }
  };
  const conVariantes = (r) => {
    const m = /\.(pcx|wal|tga|png|jpg)$/.exec(r);
    if (m) imagen(sinExt(r), m[1] === 'wal' ? 'wal' : 'pcx');
    else agregar(r);
  };

  const resolver = (crudo) => {
    let s = crudo.trim().toLowerCase();
    if (!s || s.length > 120) return;
    // skin de jugador: "nick\modelo/skin"
    if (s.includes('\\')) {
      const ms = s.split('\\').pop();
      const [modelo, skin] = ms.split('/');
      if (modelo && skin && existe(`players/${modelo}/tris.md2`)) {
        modelos.add(modelo);
        agregar(`players/${modelo}/tris.md2`);
        agregar(`players/${modelo}/weapon.md2`);   // arma por defecto (su skin sale del md2)
        imagen(`players/${modelo}/${skin}`);
        imagen(`players/${modelo}/${skin}_i`);
      }
      return;
    }
    if (s.startsWith('/')) s = s.slice(1);
    if (s.startsWith('*')) {          // sonido "sexado": va en la carpeta del modelo
      for (const m of modelos) agregar(`players/${m}/${s.slice(1)}`);
      agregar(`players/male/${s.slice(1)}`);
      return;
    }
    if (existe(s)) { conVariantes(s); return; }
    if (existe(`sound/${s}`)) { agregar(`sound/${s}`); return; }
    if (!s.includes('.')) {
      if (!imagen(`pics/${s}`)) imagen(s);
    }
  };

  // 1) textos del demo (dos pasadas: las skins definen los modelos para los sonidos "*")
  const ts = textos(demo);
  for (const t of ts) if (t.includes('\\')) resolver(t);
  for (const t of ts) if (!t.includes('\\')) resolver(t);

  // 4) base
  for (const b of [...BASE, ...FIJOS]) /\.\w+$/.test(b) ? conVariantes(b) : imagen(b);

  const abrir = async (r) => {
    if (!cache.has(r)) cache.set(r, await leer(r));
    return cache.get(r);
  };

  // 2) y 3): abrir bsp / md2 / sp2 (pueden traer más md2 o sp2)
  while (pendientes.length) {
    const lote = pendientes.splice(0);
    const datos = await Promise.all(lote.map((r) => abrir(r).catch(() => null)));
    lote.forEach((r, i) => {
      const b = datos[i];
      if (!b) return;
      const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
      const i32 = (o) => dv.getInt32(o, true);
      if (r.endsWith('.bsp')) {
        // lumps: 0 = entidades, 5 = texinfo (76 bytes c/u, nombre en +40)
        const entOfs = i32(8), entLen = i32(12);
        const texOfs = i32(8 + 5 * 8), texLen = i32(12 + 5 * 8);
        for (let o = texOfs; o + 76 <= texOfs + texLen; o += 76) {
          imagen(`textures/${cadena(b, o + 40, 32).toLowerCase()}`, 'wal');
        }
        const ent = cadena(b, entOfs, entLen);
        for (const m of ent.matchAll(/"([^"]*)"\s+"([^"]*)"/g)) {
          const [, clave, valor] = m;
          if (clave === 'sky') for (const lado of SKY_SIDES) cielo(`env/${valor.toLowerCase()}${lado}`);
          else resolver(valor);
        }
      } else if (r.endsWith('.md2')) {
        // header IDP2: num_skins en +20, ofs_skins en +44, 64 bytes por nombre
        const n = i32(20), ofs = i32(44);
        for (let k = 0; k < n && ofs + (k + 1) * 64 <= b.length; k++) {
          const skin = cadena(b, ofs + k * 64, 64).toLowerCase().replace(/^\//, '');
          if (skin) conVariantes(skin);
        }
      } else if (r.endsWith('.sp2')) {
        // header IDS2: numframes en +8, cuadros de 80 bytes desde +12, nombre en +16
        const n = i32(8);
        for (let k = 0; k < n && 12 + (k + 1) * 80 <= b.length; k++) {
          const cuadro = cadena(b, 12 + k * 80 + 16, 64).toLowerCase();
          if (cuadro) conVariantes(cuadro);
        }
      }
    });
  }

  const lista = [...archivos].sort();
  return { archivos: lista, bytes: lista.reduce((s, r) => s + indice[r], 0), cache };
}
