from __future__ import annotations

import argparse
import struct
import time
from functools import reduce
from operator import xor

import serial


FRAME_SIZE = 31


def valid_frame(frame: bytes) -> bool:
    return (
        len(frame) == FRAME_SIZE
        and frame[0] == 0x2A
        and frame[1] == 0x1B
        and frame[30] == 0x23
        and reduce(xor, frame[2:29], 0) == frame[29]
    )


def extract_frames(data: bytes) -> list[bytes]:
    frames: list[bytes] = []
    offset = 0
    while offset + FRAME_SIZE <= len(data):
        if data[offset:offset + 2] == b'\x2a\x1b':
            frame = data[offset:offset + FRAME_SIZE]
            if valid_frame(frame):
                frames.append(frame)
                offset += FRAME_SIZE
                continue
        offset += 1
    return frames


def main() -> int:
    parser = argparse.ArgumentParser(description='只读检查BU04 PDOA Hex数据口')
    parser.add_argument('port', help='例如COM25')
    parser.add_argument('--seconds', type=float, default=2.0)
    parser.add_argument('--baud', type=int, default=115200)
    args = parser.parse_args()

    captured = bytearray()
    with serial.Serial(args.port, args.baud, timeout=0.1) as port:
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            captured.extend(port.read(512))

    frames = extract_frames(captured)
    print(f'bytes={len(captured)} valid_frames={len(frames)}')
    if not frames:
        return 1

    frame = frames[-1]
    sequence = frame[2]
    address = struct.unpack_from('<H', frame, 3)[0]
    angle_deg = struct.unpack_from('<i', frame, 5)[0]
    distance_cm = struct.unpack_from('<I', frame, 9)[0]
    print(
        f'last: seq={sequence} tag=0x{address:04X} '
        f'distance={distance_cm}cm angle={angle_deg:+d}deg'
    )
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
