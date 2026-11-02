#!/usr/bin/env python3
"""Upload one fixed DDR resource through the existing UART.

The tool sends raw bytes in 128-byte XMODEM/CRC packets.  It never accepts a
host supplied address; the resource ID selects the firmware's fixed range.
"""
from __future__ import annotations

import argparse
import binascii
import struct
import sys
import time
import re

import json
from pathlib import Path
_catalog = json.loads((Path(__file__).resolve().parents[1] / "config/enemy_scene.json").read_text(encoding="utf-8-sig"))
RESOURCES = {a["id"]: (a["name"], int(a["base"], 16), a["length"]) for a in _catalog["assets"]}
EXT_ABI = 0x41530204
SOH, EOT, ACK, NAK, CAN = 0x01, 0x04, 0x06, 0x15, 0x18


class Responses:
    """Frame control bytes and complete lines across short serial reads."""
    def __init__(self, ser):
        self.ser = ser
        self.line = bytearray()

    def next(self, deadline):
        while time.monotonic() < deadline:
            byte = self.ser.read(1)
            if not byte:
                continue
            value = byte[0]
            if value == CAN:
                raise RuntimeError("firmware cancelled upload (CAN); query ASSET STATUS before retry")
            if value in (ACK, NAK):
                return value
            if value == 10:
                line = self.line.decode("ascii", errors="replace").strip()
                self.line.clear()
                if line.startswith("ASSET ERROR") or line.startswith("ERR "):
                    raise RuntimeError(line)
                if line.startswith("ASSET "):
                    return line
                # Existing demo telemetry may already be queued before BEGIN.
                # Ignore only complete unrelated text, never accept it as success.
            elif value != 13:
                self.line.append(value)
                if len(self.line) > 1024:
                    raise RuntimeError("overlong UART response; save serial log and query status")
        raise TimeoutError("UART response deadline expired; query ASSET STATUS before retry")

    def expect_line(self, expected, timeout):
        deadline = time.monotonic() + timeout
        reply = self.next(deadline)
        if reply != expected:
            raise RuntimeError(f"expected {expected!r}, received {reply!r}")


def crc16_ccitt(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def packet(sequence: int, data: bytes) -> bytes:
    if len(data) != 128:
        raise ValueError("packet must be exactly 128 bytes")
    return bytes((SOH, sequence & 0xFF, (~sequence) & 0xFF)) + data + struct.pack(">H", crc16_ccitt(data))


def upload(ser, resource_id: int, payload: bytes, timeout: float) -> None:
    name, base, length = RESOURCES[resource_id]
    if len(payload) != length:
        raise ValueError(f"{name} requires {length} bytes, got {len(payload)}")
    responses = Responses(ser)
    ser.write(b"ASSET HELLO\r\n")
    responses.expect_line("ASSET HELLO 1 FIXED128 ABI=41530100", timeout)
    if resource_id >= 1: # A styles ABI applies to every upload, before payload BEGIN.
        ser.write(b"ASSET CAPS\r\n")
        # The local catalog can include newer fixed slots. An older compatible
        # bit still supports its advertised IDs; never infer support for a new ID.
        reply = responses.next(time.monotonic() + timeout)
        # CMD: optional MASK_HI (IDs 32..63); an older bit without it supports IDs 1..31 only.
        caps = re.fullmatch(r"ASSET CAPS ABI=([0-9A-Fa-f]{8}) MASK=([0-9A-Fa-f]{8})(?: MASK_HI=([0-9A-Fa-f]{8}))?", reply) if isinstance(reply, str) else None
        word = (int(caps[2], 16) if resource_id < 32 else int(caps[3] or "0", 16)) if caps else 0
        if not caps or int(caps[1], 16) != EXT_ABI or not ((word >> (resource_id & 31)) & 1):
            raise RuntimeError(f"fixed resource ID {resource_id} unsupported by firmware capability: {reply!r}")
    crc = binascii.crc32(payload) & 0xFFFFFFFF
    ser.write(f"ASSET BEGIN {resource_id} {length} {crc:08X}\r\n".encode())
    responses.expect_line(f"ASSET READY {resource_id}", timeout)
    for block, offset in enumerate(range(0, length, 128), start=1):
        frame = packet(block, payload[offset:offset + 128])
        for attempt in range(5):
            ser.write(frame)
            deadline = time.monotonic() + timeout
            reply = responses.next(deadline)
            if reply == ACK:
                break
            if reply != NAK:
                raise RuntimeError(f"block {block}: unexpected response {reply!r}")
            if attempt == 4:
                raise RuntimeError(f"block {block} rejected after retries")
        if block % 256 == 0 or block == (length // 128):
            print(f"{name}: {offset + 128}/{length} bytes")
    ser.write(bytes((EOT,)))
    deadline = time.monotonic() + timeout
    if responses.next(deadline) != ACK:
        raise RuntimeError("EOT was not acknowledged")
    if responses.next(deadline) != "ASSET STORED":
        raise RuntimeError("firmware did not report exact ASSET STORED")
    # EOT success already includes safe release; this ABI has CANCEL, no EXIT.
    print(f"{name}: stored at DDR 0x{base:08X}, CRC32 {crc:08X}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--resource", type=int, choices=sorted(RESOURCES), required=True)
    parser.add_argument("file")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()
    from sample_game import verify_pair
    verify_pair()
    payload = open(args.file, "rb").read()
    try:
        import serial  # type: ignore
    except ImportError:
        print("pyserial is required for live upload", file=sys.stderr)
        return 2
    try:
        with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as ser:
            upload(ser, args.resource, payload, args.timeout)
    except (RuntimeError, TimeoutError, OSError, ValueError) as error:
        print(f"Upload failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
