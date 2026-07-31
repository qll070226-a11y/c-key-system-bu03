from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path

import serial


READ_ONLY_COMMANDS = (
    "AT+GETVER",
    "AT+GETCFG",
    "AT+GETDEV",
    "AT+GETMCUMODE",
    "AT+PDOAGETCFG",
)


def read_until_quiet(port: serial.Serial, timeout_s: float, quiet_s: float = 0.15) -> bytes:
    deadline = time.monotonic() + timeout_s
    last_data = time.monotonic()
    data = bytearray()
    while time.monotonic() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            data.extend(chunk)
            last_data = time.monotonic()
        elif data and time.monotonic() - last_data >= quiet_s:
            break
    return bytes(data)


def display_bytes(data: bytes) -> str:
    parts: list[str] = []
    for value in data:
        if value in (9, 10, 13) or 32 <= value <= 126:
            parts.append(chr(value))
        else:
            parts.append(f"\\x{value:02X}")
    return "".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description="Back up BU03 read-only AT responses")
    parser.add_argument("port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=1.2)
    parser.add_argument("--output-dir", type=Path, default=Path("backups") / "bu03")
    args = parser.parse_args()

    captured_at = datetime.now().astimezone()
    record: dict[str, object] = {
        "port": args.port,
        "baud": args.baud,
        "captured_at": captured_at.isoformat(timespec="seconds"),
        "commands": [],
    }

    with serial.Serial(args.port, args.baud, timeout=0.05) as port:
        port.dtr = False
        port.rts = False
        initial = read_until_quiet(port, 0.5)
        record["initial_rx_hex"] = initial.hex(" ")
        record["initial_rx_display"] = display_bytes(initial)

        commands = record["commands"]
        assert isinstance(commands, list)
        for command in READ_ONLY_COMMANDS:
            port.reset_input_buffer()
            port.write((command + "\r\n").encode("ascii"))
            port.flush()
            response = read_until_quiet(port, args.timeout)
            item = {
                "command": command,
                "response_hex": response.hex(" "),
                "response_display": display_bytes(response),
            }
            commands.append(item)
            print(f"[{args.port}] {command}")
            print(item["response_display"] or "<no response>")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = captured_at.strftime("%Y%m%d_%H%M%S")
    output = args.output_dir / f"{args.port.lower()}_{stamp}.json"
    output.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"saved: {output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
