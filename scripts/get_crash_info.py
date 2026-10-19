#!/usr/bin/env python3
import sys
import glob
import time
import struct
import subprocess
import serial

TOOLS_DIR = "/Users/kongweilu/Development/Mosaico EdgeTech 2026/mosaico-experience/workspace/esp-mosaico-vibe/submodule/esp-mosaico-utils/ESP-Iris/components/esp_iris/tools"
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from iris_gateway.protocol import (
    Channel,
    ControlType,
    CrashType,
    Frame,
    FrameDecoder,
    TlvTag,
    decode_tlv,
    encode_frame,
    tlv_u32,
    tlv_u64,
    tlv_u8,
)

port_name = None
for p in sorted(glob.glob("/dev/cu.usbmodem*")):
    if "11201" not in p:
        port_name = p
        break

if not port_name:
    print("No port found")
    sys.exit(1)

print(f"Connecting to {port_name}...")
ser = serial.Serial(port=port_name, baudrate=115200, timeout=0.1, write_timeout=2.0)
ser.dtr = True
ser.rts = False

decoder = FrameDecoder()
seq = [0] * 16
session_id = 0

def send_frame(ch: int, ftype: int, payload: bytes = b"", flags: int = 0, req_id: int = 0) -> None:
    seq[int(ch)] = (seq[int(ch)] + 1) & 0xFFFFFFFF
    raw = encode_frame(
        Frame(
            channel=int(ch),
            type=int(ftype),
            flags=flags,
            session_id=session_id,
            request_id=req_id,
            sequence=seq[int(ch)],
            payload=payload,
        )
    )
    ser.write(raw)
    ser.flush()

t_start = time.time()
ready = False
metadata_received = False
coredump_bytes = bytearray()
total_coredump_size = 0
chunk_max = 1024

while time.time() - t_start < 10.0:
    chunk = ser.read(4096)
    if not chunk:
        time.sleep(0.05)
        continue
    for frame in decoder.feed(chunk):
        if frame.channel == Channel.CONTROL and frame.type == ControlType.HELLO:
            session_id = frame.session_id
            print(f"[HELLO] session={session_id}")
            send_frame(Channel.CONTROL, ControlType.HELLO_ACK)
            print("Requesting crash metadata...")
            send_frame(Channel.CRASH, CrashType.METADATA_REQUEST)
            ready = True

        elif frame.channel == Channel.CRASH:
            if frame.type == CrashType.METADATA_RESPONSE:
                metadata_received = True
                fields = decode_tlv(frame.payload)
                print("=== CRASH METADATA RESPONSE ===")
                for k, v in fields.items():
                    try:
                        tag_name = TlvTag(k).name
                    except Exception:
                        tag_name = str(k)
                    if len(v) <= 8:
                        val_num = int.from_bytes(v, "little")
                        print(f"  {tag_name} (0x{k:02x}): {val_num} (raw {v.hex()})")
                    else:
                        print(f"  {tag_name} (0x{k:02x}): {v.decode('utf-8', errors='replace')}")

                chunk_max = tlv_u32(fields, TlvTag.CORE_DUMP_CHUNK_MAX, 1024)
                total_coredump_size = tlv_u32(fields, TlvTag.CORE_DUMP_SIZE, 0)
                panic_reason = fields.get(int(TlvTag.PANIC_REASON), b"").decode("utf-8", errors="replace")
                print(f"PANIC REASON: {panic_reason}")
                print(f"Total Coredump Size: {total_coredump_size} bytes")

                if total_coredump_size > 0:
                    print("Requesting coredump chunk 0...")
                    req = struct.pack("<IHH", 0, min(chunk_max, 1024), 0)
                    send_frame(Channel.CRASH, CrashType.READ_REQUEST, req)
                else:
                    print("No coredump to read.")
                    sys.exit(0)

            elif frame.type == CrashType.READ_RESPONSE:
                ret_off, tot = struct.unpack_from("<II", frame.payload)
                data = frame.payload[8:]
                coredump_bytes.extend(data)
                print(f"Read coredump offset {ret_off}: got {len(data)} bytes ({len(coredump_bytes)}/{total_coredump_size})")

                if len(coredump_bytes) < total_coredump_size:
                    next_off = len(coredump_bytes)
                    req = struct.pack("<IHH", next_off, min(chunk_max, total_coredump_size - next_off), 0)
                    send_frame(Channel.CRASH, CrashType.READ_REQUEST, req)
                else:
                    print(f"Successfully retrieved entire coredump ({len(coredump_bytes)} bytes)!")
                    with open("firmware/build_vision/coredump.bin", "wb") as f:
                        f.write(coredump_bytes)
                    print("Saved to firmware/build_vision/coredump.bin")
                    sys.exit(0)

ser.close()
