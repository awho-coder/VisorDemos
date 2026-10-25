#!/usr/bin/env python3
"""Arma el árbol de archivos que baja el visor web.

Desempaqueta los .pak/.pkz de un mod y los junta con los archivos sueltos,
respetando el mismo orden de búsqueda de Q2PRO (los paks ganan a los sueltos y,
entre paks, gana el último que se carga: pak0..pakN por número y después el
resto por orden alfabético). Solo copia carpetas de recursos (nunca .cfg) y
deja todos los nombres en minúsculas. Escribe además index.json con
{ruta: tamaño}, que el visor usa para saber qué existe sin preguntar al servidor.

uso: q2web.py <carpeta_del_mod> <salida>
     ej. q2web.py /home/ubuntu/dday/dday /home/ubuntu/dday/q2web/dday
"""
import json
import os
import re
import shutil
import struct
import sys
import zipfile

DIRS = ('maps/', 'textures/', 'env/', 'models/', 'players/', 'sound/', 'sprites/', 'pics/')
SKIP_EXT = ('.cfg', '.ent', '.ctl', '.cmp', '.txt', '.ini', '.log', '.so', '.dll')


def wanted(path):
    if path.endswith('/') or '..' in path.split('/') or '\\' in path:
        return False  # carpetas dentro del pak o nombres que saldrían del árbol
    return path.startswith(DIRS) and not path.endswith(SKIP_EXT)


def pak_order(names):
    """Mismo orden que pakcmp() de Q2PRO: pak<N> por número, luego alfabético."""
    def key(name):
        low = name.lower()
        if low.startswith('pak'):
            m = re.match(r'(\d*)', low[3:])
            num = int(m.group(1)) if m.group(1) else 0
            return (0, num, low)
        return (1, 0, low)
    return sorted(names, key=key)


def pak_entries(path):
    with open(path, 'rb') as f:
        head = f.read(12)
        if head[:4] != b'PACK':
            return []
        ofs, length = struct.unpack('<ii', head[4:])
        f.seek(ofs)
        data = f.read(length)
    out = []
    for i in range(0, length, 64):
        name = data[i:i + 56].split(b'\0')[0].decode('latin1')
        pos, size = struct.unpack('<ii', data[i + 56:i + 64])
        out.append((name, pos, size))
    return out


def main():
    gamedir, outdir = sys.argv[1], sys.argv[2]
    files = {}  # ruta en minúsculas -> ('loose', origen) | ('pak', pak, pos, size) | ('zip', zip, nombre)

    # 1) archivos sueltos (se buscan después que los paks: los pisan)
    for root, _dirs, names in os.walk(gamedir):
        for name in names:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, gamedir).replace(os.sep, '/').lower()
            if wanted(rel):
                files[rel] = ('loose', full)

    # 2) paks en orden de carga: el que se carga después gana
    packs = [n for n in os.listdir(gamedir) if n.lower().endswith(('.pak', '.pkz'))]
    for name in pak_order(packs):
        full = os.path.join(gamedir, name)
        if name.lower().endswith('.pkz'):
            with zipfile.ZipFile(full) as z:
                for entry in z.namelist():
                    rel = entry.lower()
                    if not entry.endswith('/') and wanted(rel):
                        files[rel] = ('zip', full, entry)
        else:
            for entry, pos, size in pak_entries(full):
                rel = entry.lower()
                if wanted(rel):
                    files[rel] = ('pak', full, pos, size)

    # 3) copiar
    index = {}
    zips = {}
    for rel, src in sorted(files.items()):
        dst = os.path.join(outdir, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if src[0] == 'loose':
            shutil.copyfile(src[1], dst)
        elif src[0] == 'pak':
            with open(src[1], 'rb') as f:
                f.seek(src[2])
                data = f.read(src[3])
            with open(dst, 'wb') as f:
                f.write(data)
        else:
            z = zips.get(src[1]) or zips.setdefault(src[1], zipfile.ZipFile(src[1]))
            with open(dst, 'wb') as f:
                f.write(z.read(src[2]))
        index[rel] = os.path.getsize(dst)

    with open(os.path.join(outdir, 'index.json'), 'w') as f:
        json.dump(index, f, separators=(',', ':'), sort_keys=True)
    total = sum(index.values())
    print('%d archivos, %.0f MB' % (len(index), total / 1048576))


if __name__ == '__main__':
    main()
