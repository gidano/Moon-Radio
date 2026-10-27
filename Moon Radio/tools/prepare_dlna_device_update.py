"""Prepare a web update over the device's saved LittleFS, preserving other files.

Only operates on local backup/image files. Does not open the serial port.
"""
import hashlib
import argparse
import json
import struct
import zlib
from pathlib import Path
from littlefs import LittleFS

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'build-dlna'
BACKUP = OUTPUT / 'device-backup'
FLASH = BACKUP / 'full-flash-before-dlna.bin'
SIZE = 0x1000000


def entries(table):
    result = []
    for i in range(0, 0xC00, 32):
        raw = table[i:i + 32]
        magic = struct.unpack_from('<H', raw)[0]
        if magic in (0xFFFF, 0xEBEB):
            break
        if magic != 0x50AA:
            raise RuntimeError('Invalid partition table')
        _, kind, sub, offset, length, name, flags = struct.unpack('<HBBII16sI', raw)
        result.append(dict(type=kind, subtype=sub, offset=offset, size=length,
                           name=name.rstrip(b'\0').decode('ascii'), flags=flags))
    return result


def hashes(filesystem):
    result = {}
    for directory, _, files in filesystem.walk('/'):
        for name in files:
            full = directory.rstrip('/') + '/' + name
            with filesystem.open(full, 'rb') as handle:
                result[full.lstrip('/')] = hashlib.sha256(handle.read()).hexdigest()
    return result


def mount(data, size):
    # The legacy PlatformIO mklittlefs cannot unpack this device's LittleFS
    # revision. Use the already installed, newer littlefs-python on a copy.
    fs = LittleFS(block_size=4096, block_count=size // 4096, mount=False)
    fs.context.buffer = bytearray(data)
    fs.mount()
    return fs


def main():
    global FLASH, BACKUP, OUTPUT
    parser = argparse.ArgumentParser()
    parser.add_argument('--backup', type=Path, default=FLASH)
    parser.add_argument('--output-dir', type=Path, default=OUTPUT)
    args = parser.parse_args()
    FLASH = args.backup.resolve()
    BACKUP = FLASH.parent
    OUTPUT = args.output_dir.resolve()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    if FLASH.stat().st_size != SIZE:
        raise RuntimeError('Complete 16 MiB flash backup required')
    flash = FLASH.read_bytes()
    actual = entries(flash[0x8000:0x9000])
    expected = entries((ROOT / '.pio/build/esp32-s3-devkitc1-n16r8/partitions.bin').read_bytes())
    if actual != expected:
        raise RuntimeError('Device partition layout differs from the prepared firmware; update stopped')
    ota = next(p for p in actual if p['type'] == 1 and p['subtype'] == 0)
    ota_bytes = flash[ota['offset']:ota['offset'] + ota['size']]
    records = []
    for offset in (0, 0x1000):
        raw = ota_bytes[offset:offset + 32]
        seq, _, state, crc = struct.unpack('<I20sII', raw)
        # IDF bootloader_common_ota_select_crc: crc32_le(UINT32_MAX, seq, 4).
        if seq not in (0, 0xFFFFFFFF) and state not in (3, 4) and crc == zlib.crc32(raw[:4], 0xFFFFFFFF):
            if state not in (2, 0xFFFFFFFF):
                raise RuntimeError('OTA image awaits verification; update stopped')
            records.append(seq)
    apps = sorted((p for p in actual if p['type'] == 0 and 0x10 <= p['subtype'] <= 0x1F), key=lambda p: p['subtype'])
    if not records and (ota_bytes[:32] != b'\xff' * 32 or ota_bytes[0x1000:0x1020] != b'\xff' * 32):
        raise RuntimeError('No valid OTA selection record; update stopped')
    active = apps[(max(records) - 1) % len(apps)] if records else apps[0]
    filesystem = next(p for p in actual if p['type'] == 1 and p['subtype'] == 0x82)
    original = BACKUP / 'littlefs-before.bin'
    original.write_bytes(flash[filesystem['offset']:filesystem['offset'] + filesystem['size']])
    staging = mount(original.read_bytes(), filesystem['size'])
    before = hashes(staging)
    if not before:
        raise RuntimeError('Device filesystem unpacked empty; update stopped')
    for source in (ROOT / 'data/web').rglob('*'):
        if source.is_file():
            relative = source.relative_to(ROOT / 'data')
            destination = '/' + relative.as_posix()
            staging.makedirs(str(relative.parent).replace('\\', '/'), exist_ok=True)
            content = source.read_bytes()
            if before.get(relative.as_posix()) != hashlib.sha256(content).hexdigest():
                with staging.open(destination, 'wb') as handle:
                    handle.write(content)
    image = OUTPUT / 'littlefs-preserved.bin'
    staging.unmount()
    image.write_bytes(staging.context.buffer)
    verified = mount(image.read_bytes(), filesystem['size'])
    after = hashes(verified)
    verified.unmount()
    unchanged = {name: digest for name, digest in before.items() if not name.startswith('web/')}
    if any(after.get(name) != digest for name, digest in unchanged.items()):
        raise RuntimeError('A non-web file changed or disappeared; update stopped')
    for source in (ROOT / 'data/web').rglob('*'):
        if source.is_file():
            name = source.relative_to(ROOT / 'data').as_posix()
            if after.get(name) != hashlib.sha256(source.read_bytes()).hexdigest():
                raise RuntimeError(f'Web file verification failed: {name}')
    manifest = dict(flashBackup=FLASH.name, flashSize=SIZE,
                    backupSha256=hashlib.sha256(flash).hexdigest(), partitions=actual,
                    preservedNonWebFiles=len(unchanged), originalFiles=len(before),
                    updatedFiles=len(after), filesystemOffset=filesystem['offset'],
                    filesystemSize=filesystem['size'],
                    firmwareOffset=active['offset'], firmwarePartition=active['name'],
                    updateSha256=hashlib.sha256(image.read_bytes()).hexdigest())
    (BACKUP / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f"Backup verified: {SIZE} bytes; partition layout matches")
    print(f"Active firmware partition: {active['name']} at {active['offset']:#x}")
    print(f"Filesystem verified: {len(unchanged)} non-web files preserved; {len(after)} files total")
    print(f"Update: {image.name}, offset={filesystem['offset']:#x}, size={filesystem['size']}")


if __name__ == '__main__':
    main()
