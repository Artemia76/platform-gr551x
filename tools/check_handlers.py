#!/usr/bin/env python3
"""Refuse un ELF GR551x dont un gestionnaire d'exception vital est resté sur Default_Handler.

SVC_Handler sert aux correctifs de la ROM (FPB → SVC → fonction corrigée) : s'il vaut Default_Handler
(boucle infinie), la pile BLE se bloque dans ble_stack_init(). HardFault_Handler doit lui aussi être le
gestionnaire du SDK (enregistrement des fautes). Lecture des symboles sans dépendance externe.

Usage : check_handlers.py firmware.elf
"""
import struct, sys

REQUIRED = ('SVC_Handler', 'HardFault_Handler', 'PendSV_Handler')


def symbols(path):
    d = open(path, 'rb').read()
    if d[:4] != b'\x7fELF' or d[4] != 1 or d[5] != 1:
        sys.exit('check_handlers : ELF32 little-endian attendu')
    shoff, = struct.unpack_from('<I', d, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', d, 0x2E)
    secs = [struct.unpack_from('<IIIIIIIIII', d, shoff + i * shentsize) for i in range(shnum)]
    out = {}
    for name, typ, flags, addr, off, size, link, info, align, entsize in secs:
        if typ != 2:                       # SHT_SYMTAB
            continue
        stroff = secs[link][4]
        for i in range(size // 16):
            st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', d, off + i * 16)
            if st_name:
                end = d.index(b'\0', stroff + st_name)
                out.setdefault(d[stroff + st_name:end].decode('latin1'), st_value & ~1)
    return out


def main():
    syms = symbols(sys.argv[1])
    default = syms.get('Default_Handler')
    bad = [n for n in REQUIRED if n not in syms or syms[n] == default]
    if bad:
        sys.exit('check_handlers : REFUS — gestionnaire(s) resté(s) sur Default_Handler : ' + ', '.join(bad) +
                 '\n  (ordre d\'édition de liens : startup_gr55xx.o doit venir après gr_interrupt.o)')
    print('check_handlers : ' + ', '.join(f'{n}={syms[n]:#x}' for n in REQUIRED) + ' — OK')


if __name__ == '__main__':
    main()
