#!/usr/bin/env python3
"""Outil BLE pour le SenseCAP T1000-A (PC Linux / BlueZ, bibliothèque bleak).

Sous-commandes :
  scan [-t s]                    liste les appareils BLE visibles (nom, adresse, RSSI)
  gatt [--name N] [-t s]         attend l'appareil (fenêtre courte après appui long), s'y connecte
                                 immédiatement et liste services / caractéristiques
  flash FICHIER [--name N]       DFU Goodix vers le bootloader (SENSECAP_GR_DFU_xxxx)
  at CMD… [--name N]             envoie des commandes AT au firmware Seeed (UART transparent BLE)
  console [--name N]             console texte du firmware de test (service GUS)

Exemples :
  t1000_ble.py gatt -t 120       puis appui long sur le bouton du T1000-A
  t1000_ble.py flash firmware/hello/build/t1000a_hello_dfu.bin
"""
import re
import argparse, asyncio, struct, sys, time
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

GOODIX = '-d344-460a-8075-b9e8ec90d71b'
GUS_TX   = 'a6ed0202' + GOODIX   # notifications appareil -> PC
GUS_RX   = 'a6ed0203' + GOODIX   # écriture PC -> appareil
OTA_TX   = 'a6ed0402' + GOODIX
OTA_RX   = 'a6ed0403' + GOODIX
OTA_CTRL = 'a6ed0404' + GOODIX
# Variante du bootloader Seeed (SENSECAP_GR_DFU_xxxx), relevée sur le boîtier :
# service a6ed0701, 0702 = write-without-response + notify, 0703 = write-without-response
SEEED_OTA_A = 'a6ed0702' + GOODIX
SEEED_OTA_B = 'a6ed0703' + GOODIX

# Firmware Seeed d'origine : service « Transparent UART » Microchip/ISSC, canal des commandes AT
SEEED_SVC = '49535343-fe7d-4ae5-8fa9-9fafd205e455'
SEEED_TX  = '49535343-1e4d-4bd9-ba61-23c647249616'   # notifications appareil -> PC
SEEED_RX  = '49535343-8841-43f4-a8d4-ecbe34729bb3'   # écriture PC -> appareil

APP_LOAD_ADDR = 0x0101B000      # seule adresse acceptée pour une application T1000-A


def scanner(cb):
    """Scanner BLE qui voit chaque annonce. Par défaut (bleak : DuplicateData=False), BlueZ ne signale plus un
    appareil déjà en cache dont l'annonce n'a pas changé, et les contrôleurs à filtre de doublons strict
    (Intel…) ne le rapportent qu'une fois par session de scan : un bootloader déjà vu restait invisible.
    Un filtre RSSI fait passer BlueZ en « service discovery », où le noyau relance périodiquement le scan."""
    return BleakScanner(cb, bluez={'filters': {'RSSI': -127, 'DuplicateData': True}})


class NotFound(Exception):
    pass


DFU_SERVICES = ('a6ed0701' + GOODIX, 'a6ed0401' + GOODIX)   # FastDfu (Seeed), OTA Goodix


async def wait_for(name_prefix, timeout, address=None, services=None, exclude=()):
    """Attend un appareil dont le nom contient name_prefix (ou d'adresse name_prefix si c'est une adresse),
    ou qui annonce l'un des `services`. `exclude` : adresses à ignorer (le cache BlueZ resignale une
    application qu'on vient de faire redémarrer), sauf si elles annoncent un service DFU."""
    what = f'« {name_prefix}… »' + (' ou un bootloader DFU' if services else '') + (f' ou {address}' if address else '')
    print(f'attente de {what} ({timeout:.0f} s)…', flush=True)
    found = asyncio.get_running_loop().create_future()

    by_addr = bool(re.fullmatch(r'([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}', name_prefix))   # --name AA:BB:… = adresse
    excl = {e.upper() for e in exclude}

    def cb(dev, adv):
        dfu = bool(services and any(u.lower() in services for u in adv.service_uuids))
        if dev.address.upper() in excl and not dfu:
            return             # l'application qu'on vient de redémarrer (même adresse que le bootloader : Seeed)
        n = adv.local_name or dev.name or ''
        hit = (dev.address.upper() == name_prefix.upper()) if by_addr else name_prefix.lower() in n.lower()
        hit = hit or bool(address and dev.address.upper() == address.upper())
        hit = hit or dfu
        if hit and not found.done():
            found.set_result((dev, adv))

    async with scanner(cb):
        try:
            return await asyncio.wait_for(found, timeout)
        except asyncio.TimeoutError:
            raise NotFound('introuvable (appareil hors de portée, éteint, ou connecté à un téléphone ?)')


import contextlib


@contextlib.asynccontextmanager
async def disconnecting(c):
    """Garantit la déconnexion d'un client déjà connecté (BleakClient en `async with` reconnecterait)."""
    try:
        yield c
    finally:
        with contextlib.suppress(Exception):
            await c.disconnect()


async def connect(name, timeout, address=None, services=None, exclude=()):
    """Attend l'appareil puis s'y connecte, en réessayant tant que le délai n'est pas écoulé :
    BlueZ échoue souvent au premier essai (br-connection-canceled) sur une fenêtre d'annonce courte."""
    deadline = time.time() + timeout
    while True:
        dev, adv = await wait_for(name, max(1.0, deadline - time.time()), address, services, exclude)
        print(f'trouvé : {adv.local_name} {dev.address} rssi={adv.rssi} services_pub={adv.service_uuids}',
              flush=True)
        c = BleakClient(dev, timeout=10)
        try:
            await c.connect()
            print(f'connecté, MTU={c.mtu_size}', flush=True)
            return c, adv
        except Exception as e:
            print(f'échec de connexion ({e}), nouvel essai…', flush=True)
            if time.time() > deadline:
                sys.exit('abandon : délai écoulé')
            await asyncio.sleep(0.3)


async def cmd_scan(a):
    seen = {}

    def cb(dev, adv):
        seen[dev.address] = (adv.local_name or dev.name or '?', adv.rssi, adv.service_uuids)

    async with scanner(cb):
        await asyncio.sleep(a.t)
    for addr, (n, rssi, uu) in sorted(seen.items(), key=lambda x: -x[1][1]):
        print(f'{addr}  {rssi:4d} dBm  {n}  {" ".join(uu)}')


async def cmd_gatt(a):
    c, adv = await connect(a.name, a.t)
    try:
        for s in c.services:
            print(f'service {s.uuid}  {s.description}')
            for ch in s.characteristics:
                val = ''
                if 'read' in ch.properties:
                    try:
                        v = await c.read_gatt_char(ch)
                        val = f'  = {v.hex()} {v!r}'
                    except Exception as e:
                        val = f'  (lecture : {e})'
                print(f'   car {ch.uuid}  h={ch.handle}  {",".join(ch.properties)}{val}')
    finally:
        await c.disconnect()


# ---------------------------------------------------------------- DFU Goodix

class DfuError(Exception):
    pass


class GoodixDfu:
    """Maître DFU Goodix (transcription de components/libraries/dfu_master/dfu_master.c),
    mode normal (non « fast ») : blocs de 256 octets acquittés un par un."""
    GET_INFO, PROGRAM_START, PROGRAM_FLASH, PROGRAM_END = 0x01, 0x23, 0x24, 0x25
    SYSTEM_INFO, DFU_MODE_SET, DFU_FW_INFO_GET = 0x27, 0x41, 0x42
    ONCE = 256

    def __init__(self, client):
        self.c = client
        self.rx = bytearray()
        self.frames = asyncio.Queue()

    def _on_notify(self, _h, data):
        self.rx += data
        while True:
            i = self.rx.find(b'\x44\x47')
            if i < 0:
                del self.rx[:-1]
                return
            del self.rx[:i]
            if len(self.rx) < 6:
                return
            typ, ln = struct.unpack('<HH', self.rx[2:6])
            if len(self.rx) < 8 + ln:
                return
            body = bytes(self.rx[6:6 + ln])
            cks = struct.unpack('<H', self.rx[6 + ln:8 + ln])[0]
            del self.rx[:8 + ln]
            if (sum(struct.pack('<HH', typ, ln)) + sum(body)) & 0xFFFF != cks:
                print('trame reçue avec somme invalide, ignorée')
                continue
            self.frames.put_nowait((typ, body))

    async def start(self):
        if self.c.services.get_characteristic(SEEED_OTA_A) is not None:
            self.tx, self.rx_candidates = SEEED_OTA_A, [SEEED_OTA_B, SEEED_OTA_A]
            print('bootloader Seeed : service a6ed0701')
            await self.c.start_notify(self.tx, self._on_notify)
            return
        self.tx, self.rx_candidates = OTA_TX, [OTA_RX]
        if self.c.services.get_characteristic(OTA_TX) is None:
            for sv in self.c.services:
                print(f'service {sv.uuid}')
                for ch in sv.characteristics:
                    print(f'   car {ch.uuid} {",".join(ch.properties)}')
            raise DfuError('service OTA Goodix absent (voir la liste ci-dessus)')
        await self.c.start_notify(self.tx, self._on_notify)

    async def send(self, typ, data=b''):
        hdr = struct.pack('<HH', typ, len(data))
        frame = b'\x44\x47' + hdr + data + struct.pack('<H', (sum(hdr) + sum(data)) & 0xFFFF)
        mtu = max(20, self.c.mtu_size - 3)
        for i in range(0, len(frame), mtu):
            await self.c.write_gatt_char(self.rx_candidates[0], frame[i:i + mtu], response=False)

    async def probe(self):
        """GET_INFO (lecture seule) en essayant chaque caractéristique d'écriture candidate."""
        for rx in list(self.rx_candidates):
            self.rx_candidates = [rx] + [r for r in self.rx_candidates if r != rx]
            try:
                return await self.request(self.GET_INFO)
            except DfuError as e:
                print(f'pas de réponse via {rx[:8]} ({e})')
        raise DfuError('GET_INFO sans réponse sur toutes les caractéristiques')

    async def request(self, typ, data=b'', timeout=4.0):
        await self.send(typ, data)
        while True:
            try:
                rt, body = await asyncio.wait_for(self.frames.get(), timeout)
            except asyncio.TimeoutError:
                raise DfuError(f'pas de réponse à la commande {typ:#x}')
            if rt == typ:
                if not body or body[0] != 0x01:
                    raise DfuError(f'commande {typ:#x} refusée : {body.hex()}')
                return body
            print(f'trame inattendue {rt:#x} : {body.hex()}')


class FastDfu:
    """Client « FastDfu » Goodix (service a6ed0701), utilisé par le bootloader Seeed.
    Transcription de GoodixBleDFU.Android : libdfu2/.../fastdfu/GR5xxxFastDfu.java.
    Commande (car. 0702) : 'DOOG' (0x474F4F44 LE) + opcode + paramètres LE.
    Acquittement (notification 0702) : opcode + paramètres. Données brutes sur 0703."""
    HDR = struct.pack('<I', 0x474F4F44)
    ERASE, FLUSH, VERIFY, WRITE_BOOT, FLASH_TYPE = 0x01, 0x02, 0x03, 0x04, 0x05
    PAUSE, RESUME, BUFSIZE, NEXT_BUFFER, VERSION = 0x06, 0x07, 0x09, 0x0A, 0x0B

    def __init__(self, client):
        self.c = client
        self.acks = asyncio.Queue()
        self.paused = asyncio.Event()

    def _on_notify(self, _h, data):
        data = bytes(data)
        if data[:4] == self.HDR:          # tolérance si l'acquittement reprend l'en-tête
            data = data[4:]
        if data and data[0] == self.PAUSE:
            self.paused.set()
        elif data and data[0] == self.RESUME:
            self.paused.clear()
        self.acks.put_nowait(data)

    async def start(self):
        await self.c.start_notify(SEEED_OTA_A, self._on_notify)

    async def post(self, op, params=b''):
        await self.c.write_gatt_char(SEEED_OTA_A, self.HDR + bytes([op]) + params, response=False)

    async def ack(self, timeout=5.0):
        try:
            return await asyncio.wait_for(self.acks.get(), timeout)
        except asyncio.TimeoutError:
            raise DfuError('pas d\'acquittement')

    async def cmd(self, op, params=b'', timeout=5.0):
        await self.post(op, params)
        while True:
            a = await self.ack(timeout)
            if a and a[0] == op:
                return a
            if a and a[0] in (self.PAUSE, self.RESUME):
                continue
            raise DfuError(f'acquittement inattendu pour {op:#x} : {a.hex()}')


async def fast_flash(c, a, img, tail):
    d = FastDfu(c)
    await d.start()
    ver = (await d.cmd(d.VERSION))[1]
    print(f'FastDfu : version de protocole {ver}')
    bufsize = 4096
    if ver < 3:
        bufsize = struct.unpack('<I', (await d.cmd(d.BUFSIZE))[1:5])[0]
    print(f'taille de tampon : {bufsize}')
    load = struct.unpack('<I', tail[12:16])[0]
    end = load + len(img)
    print(f'zone à écrire : {load:#x}..{end:#x}')
    if load != APP_LOAD_ADDR or end > 0x010E2000:
        sys.exit('zone hors de l\'espace applicatif autorisé : abandon')
    if a.dry_run:
        print('--dry-run : arrêt avant toute commande modifiante (le bootloader repart seul après 3 min)')
        return

    await d.cmd(d.FLASH_TYPE, b'\x00')                  # Flash interne
    await d.post(d.ERASE, struct.pack('<II', load, len(img)))
    while True:
        r = await d.ack(timeout=30)
        if not r or r[0] != d.ERASE or len(r) < 2:
            raise DfuError(f'effacement : réponse {r.hex()}')
        phase = r[1]
        if phase == 0x03:
            break
        if phase in (0x01, 0x02):
            n = struct.unpack('<H', r[2:4])[0] if len(r) >= 4 else 0
            print(f'\r  effacement… {"début" if phase == 1 else n} secteur(s)', end='', flush=True)
            continue
        raise DfuError({0: 'adresse non alignée', 4: 'chevauche le firmware en cours',
                        5: 'échec d\'effacement', 6: 'pas de Flash externe'}.get(phase, f'code {phase}'))
    print('\neffacement terminé, écriture…')

    mtu = max(20, c.mtu_size - 3)
    t0, sent = time.time(), 0
    if ver >= 3:
        while sent < len(img):
            while d.paused.is_set():
                await asyncio.sleep(0.01)
            chunk = img[sent:sent + mtu]
            await c.write_gatt_char(SEEED_OTA_B, chunk, response=False)
            sent += len(chunk)
            if sent % (mtu * 50) < mtu:
                print(f'\r  {sent * 100 // len(img):3d} %', end='', flush=True)
    else:
        while sent < len(img):
            block = img[sent:sent + bufsize]
            for i in range(0, len(block), mtu):
                await c.write_gatt_char(SEEED_OTA_B, block[i:i + mtu], response=False)
            sent += len(block)
            r = await d.ack(timeout=10)
            if r[0] != d.NEXT_BUFFER:
                raise DfuError(f'écriture : réponse {r.hex()}')
            print(f'\r  {sent * 100 // len(img):3d} %', end='', flush=True)
    await d.cmd(d.FLUSH, timeout=20)
    print(f'\nécrit en {time.time() - t0:.1f} s')
    await asyncio.sleep(0.2)
    cks = sum(img) & 0xFFFFFFFF
    r = await d.cmd(d.VERIFY, struct.pack('<I', cks), timeout=20)
    got = struct.unpack('<I', r[1:5])[0]
    if got != cks:
        raise DfuError(f'somme de contrôle Flash {got:#x} ≠ fichier {cks:#x} : NE PAS redémarrer, refaire le flash')
    print(f'somme de contrôle vérifiée ({cks:#x})')
    await d.post(d.WRITE_BOOT, tail[:40])
    print('WRITE_BOOT envoyé : le boîtier redémarre sur le nouveau firmware')


async def cmd_flash(a):
    img = open(a.file, 'rb').read()
    tail = img[-48:]
    pat, ver, bin_size, cks, load, run = struct.unpack('<HHIIII', tail[:20])
    if pat != 0x4744 or bin_size != len(img) - 48 or (sum(img[:bin_size]) & 0xFFFFFFFF) != cks:
        sys.exit('image invalide (passer par tools/mkimage.py)')
    if load != APP_LOAD_ADDR or run != APP_LOAD_ADDR:
        sys.exit(f'adresse de chargement {load:#x} ≠ {APP_LOAD_ADDR:#x} : refus (protège le bootloader)')
    img_info = tail[:40]          # dfu_image_info_t : pattern, version, boot_info (24), comments (12)
    print(f'image : {len(img)} octets, commentaire « {tail[28:40].split(bytes(1))[0].decode()} »')

    app_addr = None
    if a.enter_dfu:
        app_addr = await enter_dfu(a.enter_dfu, a.t)
        if app_addr:
            print('le boîtier redémarre en bootloader DFU…')
            await asyncio.sleep(3.0)          # laisser le firmware redémarrer (≈1,2 s)
            bluez_forget(app_addr)
        else:
            await asyncio.sleep(1.0)

    # Le bootloader n'a pas forcément l'adresse de l'application (T1000-A : …DC:F8 contre …7A:8F) : on le
    # reconnaît à son nom ou à son service DFU annoncé, et on ignore l'adresse de l'application, que le cache
    # BlueZ resignale encore quelques secondes. Une coupure pendant le flash relance une tentative complète :
    # le bootloader reste en attente tant qu'aucun WRITE_BOOT valide n'a été reçu.
    for attempt in range(1, a.retries + 1):
        if attempt > 1:
            print(f'\n--- nouvelle tentative ({attempt}/{a.retries}) ---', flush=True)
            await asyncio.sleep(2.0)
        try:
            # Le bootloader Seeed repart sur l'application si une connexion échoue : avec --enter-dfu, on
            # n'attend que 30 s puis on redemande le DFU à l'application.
            c, adv = await connect(a.name, 30 if a.enter_dfu else a.t, services=DFU_SERVICES,
                                   exclude=[app_addr] if app_addr else ())
        except NotFound:
            if not a.enter_dfu:
                raise
            print('bootloader absent (reparti sur l\'application ?) : nouvelle demande de DFU', flush=True)
            addr = await enter_dfu(a.enter_dfu, a.t)
            if addr:
                app_addr = addr
                await asyncio.sleep(3.0)
                bluez_forget(addr)
            continue
        try:
            done = await flash_once(c, a, img, tail, img_info)
        except (DfuError, BleakError, asyncio.TimeoutError, EOFError, OSError) as e:
            print(f'\néchec : {e}', flush=True)
            continue
        if done is False:          # pas un bootloader : on cherche encore
            continue
        if done is not None and app_addr and not a.dry_run:
            await wait_back(app_addr)
        return
    sys.exit(f'échec après {a.retries} tentatives. Le bootloader attend encore : relancer\n'
             f'  t1000_ble.py flash {a.file}\n'
             'ou, s\'il a disparu : maintenir le bouton en rebranchant le câble (voir docs/materiel-t1000a.md).')


async def wait_back(app_addr, timeout=40):
    """Après WRITE_BOOT : attend que l'application réapparaisse à son adresse."""
    print(f'attente du redémarrage de l\'application ({app_addr})…', flush=True)
    bluez_forget(app_addr)
    found = asyncio.get_running_loop().create_future()

    def cb(dev, adv):
        if dev.address.upper() == app_addr.upper() and not found.done():
            found.set_result(adv)

    async with scanner(cb):
        try:
            adv = await asyncio.wait_for(found, timeout)
            print(f'application en marche : {adv.local_name}', flush=True)
        except asyncio.TimeoutError:
            print('application pas revue en 40 s (connectée à un téléphone ? sinon vérifier le boîtier)', flush=True)


async def flash_once(c, a, img, tail, img_info):
    """Une tentative complète sur un client connecté. Retourne False si l'appareil n'est pas un bootloader."""
    async with disconnecting(c):
        with contextlib.suppress(Exception):
            await c._backend._acquire_mtu()            # BlueZ : MTU réellement négocié
        print(f'MTU={c.mtu_size}')
        if c.services.get_characteristic(SEEED_OTA_A) is not None:
            await fast_flash(c, a, img, tail)
            return True
        if c.services.get_characteristic(OTA_RX) is None and c.services.get_characteristic(OTA_CTRL) is None:
            print('pas de service DFU sur cet appareil : ce n\'est pas le bootloader, on continue de chercher')
            return False
        d = GoodixDfu(c)
        await d.start()
        info = await d.probe()
        print(f'écriture via {d.rx_candidates[0][:8]}, notifications via {d.tx[:8]}')
        dfu_ver = info[17] if len(info) > 17 else 0
        print(f'GET_INFO ok (version DFU {dfu_ver}) : {info.hex()}')

        sysinfo = await d.request(d.SYSTEM_INFO, b'\x00' + struct.pack('<IH', 0x01000000, 48))
        boot_bin, boot_cks, boot_load, boot_run = struct.unpack('<IIII', sysinfo[8:24])
        print(f'bootloader : load={boot_load:#x} taille={boot_bin:#x} sécurité={sysinfo[1] >> 4}')
        if sysinfo[1] & 0xF0:
            sys.exit('le bootloader exige des images signées : abandon')

        save_addr = APP_LOAD_ADDR
        if dfu_ver >= 2:
            fw = await d.request(d.DFU_FW_INFO_GET)
            save_addr = struct.unpack('<I', fw[1:5])[0]
            run_in_app = fw[5]
            print(f'adresse d\'écriture annoncée={save_addr:#x}, exécution en appli={run_in_app}')
            if save_addr != APP_LOAD_ADDR:
                sys.exit(f'adresse annoncée {save_addr:#x} inattendue : abandon par sécurité')
            if run_in_app:
                sys.exit('l\'appareil n\'est pas en mode bootloader DFU : abandon')
            if a.dry_run:
                print('--dry-run : arrêt avant toute commande modifiante (le bootloader repart seul après 3 min)')
                return True
            await d.send(d.DFU_MODE_SET, b'\x02')          # non-copy (simple banque), pas de réponse
            await asyncio.sleep(0.2)
        if boot_load + boot_bin + 48 >= save_addr:
            sys.exit('chevauchement avec le bootloader : abandon')

        if a.dry_run:
            print('--dry-run : arrêt avant toute écriture (le bootloader repart seul sur l\'appli après 3 min)')
            return
        info40 = bytearray(img_info)
        struct.pack_into('<I', info40, 12, save_addr)       # boot_info.load_addr
        await d.request(d.PROGRAM_START, b'\x00' + bytes(info40), timeout=20)
        print('effacement fait, écriture…')

        total, addr, sent, t0 = len(img), save_addr, 0, time.time()
        while sent < total:
            chunk = img[sent:sent + d.ONCE]
            await d.request(d.PROGRAM_FLASH, b'\x01' + struct.pack('<IH', addr, len(chunk)) + chunk)
            sent += len(chunk)
            addr += len(chunk)
            print(f'\r  {sent * 100 // total:3d} %  ({sent}/{total})', end='', flush=True)
        print(f'\nécrit en {time.time() - t0:.1f} s')
        await d.request(d.PROGRAM_END, b'\x01' + struct.pack('<I', sum(img) & 0xFFFFFFFF), timeout=10)
        print('PROGRAM_END accepté : l\'appareil redémarre sur le nouveau firmware')
        return True



# ---------------------------------------------------------------- AT (firmware Seeed)

def bluez_forget(address):
    """Retire l'appareil du cache BlueZ (nom, services) pour qu'il soit redécouvert tel quel."""
    import subprocess
    subprocess.run(['bluetoothctl', 'remove', address], capture_output=True, timeout=10)


async def at_session(name, cmds, timeout):
    c, adv = await connect(name, timeout)
    address = c.address
    try:
        await c.start_notify(SEEED_TX, lambda _h, d: print(d.decode(errors='replace'), end='', flush=True))
        for cmd in cmds:
            print(f'>>> {cmd}', flush=True)
            data = cmd.encode() + b'\r\n'
            for i in range(0, len(data), 20):
                await c.write_gatt_char(SEEED_RX, data[i:i + 20], response=True)
            await asyncio.sleep(2.5)
    finally:
        try:
            await c.disconnect()
        except Exception:
            pass                      # normal après AT+DFU_ENTER (redémarrage)
    return address


async def enter_dfu(name, timeout):
    """Fait passer l'appareil en bootloader DFU, quel que soit le firmware en place :
    firmware Seeed (UART transparent ISSC) -> AT+DFU_ENTER ; nos firmwares (console GUS) -> 'dfu'.
    Si un bootloader DFU est déjà visible (flash précédent interrompu), retourne None : rien à faire."""
    c, adv = await connect(name, timeout, services=DFU_SERVICES)
    address = c.address
    try:
        if (c.services.get_characteristic(SEEED_OTA_A) is not None
                or c.services.get_characteristic(OTA_CTRL) is not None):
            print('déjà en bootloader DFU', flush=True)
            return None
        if c.services.get_characteristic(SEEED_RX) is not None:
            print('firmware Seeed : AT+DFU_ENTER', flush=True)
            await c.start_notify(SEEED_TX, lambda _h, d: print(d.decode(errors='replace'), end='', flush=True))
            await c.write_gatt_char(SEEED_RX, b'AT+DFU_ENTER\r\n', response=True)
        elif c.services.get_characteristic(GUS_RX) is not None:
            print('firmware T1000-A (console GUS) : dfu', flush=True)
            await c.start_notify(GUS_TX, lambda _h, d: print(d.decode(errors='replace'), end='', flush=True))
            await c.write_gatt_char(GUS_RX, b'dfu\r\n', response=False)
        else:
            sys.exit('aucun canal connu pour demander le DFU sur cet appareil')
        await asyncio.sleep(2.5)
    finally:
        with contextlib.suppress(Exception):
            await c.disconnect()
    return address


async def cmd_at(a):
    await at_session(a.name, a.cmds, a.t)


# ---------------------------------------------------------------- console GUS

async def cmd_console(a):
    c, adv = await connect(a.name, a.t)
    async with disconnecting(c):
        await c.start_notify(GUS_TX, lambda _h, d: print(d.decode(errors='replace'), end='', flush=True))
        print(f'connecté à {adv.local_name} (Ctrl-D pour quitter)')
        loop = asyncio.get_running_loop()
        while c.is_connected:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line:
                break
            await c.write_gatt_char(GUS_RX, line.rstrip('\n').encode() + b'\r\n', response=False)
            await asyncio.sleep(0.3)


class _SafeOut:
    """stdout qui ignore une sortie fermée (ex. « | head ») : le flash continue en silence."""
    def __init__(self, f):
        self.f, self.dead = f, False

    def write(self, t):
        if not self.dead:
            try:
                return self.f.write(t)
            except (BrokenPipeError, OSError):
                self.dead = True
        return len(t)

    def flush(self):
        if not self.dead:
            try:
                self.f.flush()
            except (BrokenPipeError, OSError):
                self.dead = True

    def __getattr__(self, n):
        return getattr(self.f, n)


def main():
    sys.stdout = _SafeOut(sys.stdout)
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('scan'); p.add_argument('-t', type=float, default=10)
    p = sub.add_parser('gatt'); p.add_argument('--name', default='T1000'); p.add_argument('-t', type=float, default=120)
    p = sub.add_parser('flash'); p.add_argument('file'); p.add_argument('--name', default='SENSECAP_GR_DFU')
    p.add_argument('-t', type=float, default=180)
    p.add_argument('--dry-run', action='store_true', help='interroge le bootloader sans rien écrire')
    p.add_argument('--retries', type=int, default=3, help='tentatives en cas de coupure BLE (défaut 3)')
    p.add_argument('--enter-dfu', metavar='NOM', help='passer d\'abord l\'appareil NOM en DFU (AT+DFU_ENTER pour Seeed, « dfu » pour nos firmwares)')
    p = sub.add_parser('at'); p.add_argument('cmds', nargs='+'); p.add_argument('--name', default='T1000')
    p.add_argument('-t', type=float, default=180)
    p = sub.add_parser('console'); p.add_argument('--name', default='T1000A-Test'); p.add_argument('-t', type=float, default=60)
    a = ap.parse_args()
    try:
        asyncio.run({'scan': cmd_scan, 'gatt': cmd_gatt, 'flash': cmd_flash, 'at': cmd_at, 'console': cmd_console}[a.cmd](a))
    except DfuError as e:
        sys.exit(f'DFU : {e}')
    except NotFound as e:
        sys.exit(str(e))


if __name__ == '__main__':
    main()
