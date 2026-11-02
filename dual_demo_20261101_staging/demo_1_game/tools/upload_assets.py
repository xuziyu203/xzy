#!/usr/bin/env python3
"""B3 missiles: validate matching packed enemy assets, preserve original PB, then upload game assets (IDs 1..27) through ASSET.

Manifest: local assets/upload_manifest.json. IDs1..23 retain exact B2 payload bytes.
IDs24..27 are the reviewed new blue/red missile payloads; B3 board acceptance is pending.

--check-only never opens a serial port. Live upload is an explicit user action;
this script never loads bit/ELF, resets hardware, or writes Flash.
"""
from pathlib import Path
import argparse,hashlib,json,sys,binascii
sys.path.insert(0,str(Path(__file__).resolve().parent))
import asset_upload
D=Path(__file__).resolve().parent;R=D.parent
MANIFEST=R/'assets/upload_manifest.json'

def payloads(ids=None,skip_background=False):
    if skip_background and ids is not None and 1 in ids:
        raise ValueError('--skip-background conflicts with explicit ID1')
    entries=json.loads(MANIFEST.read_text(encoding="utf-8-sig"))['resources'];result=[]
    for entry in entries:
        rid=entry['id']
        if skip_background and rid==1:continue
        if ids is not None and rid not in ids:continue
        p=R/entry['path'];data=p.read_bytes()
        if rid not in asset_upload.RESOURCES or len(data)!=asset_upload.RESOURCES[rid][2]:
            raise ValueError(f'ID{rid}: fixed catalog length mismatch ({len(data)} vs catalog {asset_upload.RESOURCES.get(rid)})')
        if len(data)!=entry['bytes'] or hashlib.sha256(data).hexdigest()!=entry['sha256'] or f'{binascii.crc32(data)&0xffffffff:08X}'!=entry['crc32']:
            raise ValueError(f'ID{rid}: payload hash/CRC mismatch: {p}')
        if rid in (11,13,25,27) and (len(data)!=128 or any(data[64:])):
            raise ValueError(f'ID{rid}: mask tail padding must be zero')
        if rid in (25,27):
            width=8 if rid==25 else 12
            if any(data[y*2+x//8] & (128>>(x%8)) for y in range(32) for x in range(width,16)):
                raise ValueError(f'ID{rid}: mask draw/storage padded columns must be zero')
        result.append((rid,data))
    return result

def main():
    raise SystemExit("Internal payload module. Use tools/upload_single.py --check-only or tools/upload_single.py --port PORT.")

if __name__=='__main__':raise SystemExit(main())

