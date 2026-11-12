#!/usr/bin/env python3
"""Fabrique une image applicative DFU pour le bootloader Seeed/Goodix du T1000-A.

Ajoute à la fin du binaire brut la structure Goodix `dfu_image_info_t` (48 octets),
au même format que le firmware d'origine Seeed v2.5 :

    uint16 pattern = 0x4744, uint16 version = 1,
    uint32 bin_size, uint32 check_sum (somme des octets), uint32 load_addr, uint32 run_addr,
    uint32 xqspi_xip_cmd = 0x0B, uint32 xqspi_cfg = 0x344, char comments[12]

Contrôles de sécurité (refus de produire une image douteuse) :
  - le vecteur de réinitialisation doit pointer dans l'image (Thumb) ;
  - la pile initiale doit être en RAM ;
  - l'image ne doit pas déborder sur le secteur de garde (0x010E3000) ;
  - avec --check-elf, le point d'entrée de l'ELF doit correspondre au vecteur.

Usage : mkimage.py brut.bin sortie_dfu.bin [--load 0x0101B000] [--comment Tracker-Lr11]
        mkimage.py --info image_dfu.bin
"""
import argparse, struct, sys

PATTERN = 0x4744
APP_LIMIT = 0x010E2000          # secteurs réservés au-delà : diag 0x010E2000, garde 0x010E3000, DFU 0x010E4000
RAM_RANGES = [(0x00800000, 0x00840000), (0x30000000, 0x30040000)]


def info(path):
    d = open(path, 'rb').read()
    pat, ver, size, cks, load, run, x1, x2 = struct.unpack('<HHIIIIII', d[-48:-20])
    comment = d[-20:-8].split(b'\0')[0].decode('latin1')
    ok = pat == PATTERN and size == len(d) - 48 and (sum(d[:size]) & 0xFFFFFFFF) == cks
    print(f'pattern={pat:#06x} version={ver} taille={size:#x} somme={cks:#010x} '
          f'load={load:#010x} run={run:#010x} xqspi={x1:#x}/{x2:#x} commentaire="{comment}" '
          f'-> {"VALIDE" if ok else "INVALIDE"}')
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src', nargs='?')
    ap.add_argument('dst', nargs='?')
    ap.add_argument('--load', type=lambda s: int(s, 0), default=0x0101B000)
    ap.add_argument('--comment', default='Tracker-Lr11')
    ap.add_argument('--limit', type=lambda s: int(s, 0), default=APP_LIMIT,
                    help='première adresse interdite (début du système de fichiers ou des secteurs réservés)')
    ap.add_argument('--check-elf')
    ap.add_argument('--info')
    a = ap.parse_args()
    if a.info:
        sys.exit(0 if info(a.info) else 1)

    d = open(a.src, 'rb').read()
    while len(d) % 16:                 # alignement, comme les images Goodix
        d += b'\xff'
    sp, reset = struct.unpack('<II', d[:8])
    errors = []
    if a.limit > APP_LIMIT:
        errors.append(f'limite {a.limit:#x} au-delà des secteurs réservés ({APP_LIMIT:#x})')
    if not any(lo < sp <= hi for lo, hi in RAM_RANGES):
        errors.append(f'pile initiale {sp:#x} hors RAM')
    if not (reset & 1) or not (a.load <= (reset & ~1) < a.load + len(d)):
        errors.append(f'vecteur reset {reset:#x} hors image ou non Thumb')
    if a.load + len(d) + 48 > a.limit:
        errors.append(f'image trop grande : fin {a.load + len(d) + 48:#x} > {a.limit:#x}')
    if a.check_elf:
        with open(a.check_elf, 'rb') as f:
            hdr = f.read(0x1C)
        entry = struct.unpack_from('<I', hdr, 0x18)[0]   # e_entry (ELF32 little-endian)
        if entry != reset:
            errors.append(f'point d\'entrée ELF {entry:#x} ≠ vecteur reset {reset:#x}')
    app_info = struct.unpack('<IIIII', d[0x200:0x214])
    if app_info[0] != 0x47525858 or app_info[3] != a.load:
        errors.append(f'APP_INFO à +0x200 inattendu ({app_info[0]:#x}, load={app_info[3]:#x})')
    if errors:
        sys.exit('mkimage : REFUS\n  ' + '\n  '.join(errors))

    comment = a.comment.encode()[:12].ljust(12, b'\0')
    tail = struct.pack('<HHIIIIII', PATTERN, 1, len(d), sum(d) & 0xFFFFFFFF, a.load, a.load, 0x0B, 0x344)
    tail += comment + b'\0' * 8
    assert len(tail) == 48
    open(a.dst, 'wb').write(d + tail)
    print(f'mkimage : {a.dst} ({len(d) + 48} octets)')
    info(a.dst)


if __name__ == '__main__':
    main()
