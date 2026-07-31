from __future__ import annotations

import argparse
import json
import re
import time
from datetime import datetime
from pathlib import Path

import serial

from bu03_at_backup import display_bytes, read_until_quiet


CFG_PATTERN = re.compile(r"ID:(\d+),\s*Role:(\d+),\s*CH:(\d+),\s*Rate:(\d+)")


def exchange(port: serial.Serial, command: str, timeout_s: float = 1.5) -> dict[str, str]:
    port.reset_input_buffer()
    port.write((command + "\r\n").encode("ascii"))
    port.flush()
    response = read_until_quiet(port, timeout_s)
    display = display_bytes(response)
    print(f"[{port.port}] {command}")
    print(display or "<no response>")
    return {
        "command": command,
        "response_hex": response.hex(" "),
        "response_display": display,
    }


def require_ok(item: dict[str, str]) -> None:
    if "OK" not in item["response_display"]:
        raise RuntimeError(f"{item['command']} did not return OK")


def parse_cfg(item: dict[str, str]) -> tuple[int, int, int, int]:
    match = CFG_PATTERN.search(item["response_display"])
    if match is None:
        raise RuntimeError("Unable to parse AT+GETCFG response")
    return tuple(int(value) for value in match.groups())


def main() -> int:
    parser = argparse.ArgumentParser(description="Switch one backed-up BU03-Kit to PDOA")
    parser.add_argument("port")
    parser.add_argument("--role", choices=("base", "tag"), required=True)
    parser.add_argument("--expect-id", type=int, required=True)
    parser.add_argument("--expect-role", type=int, choices=(0, 1), required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output-dir", type=Path, default=Path("backups") / "bu03")
    parser.add_argument("--yes", action="store_true")
    args = parser.parse_args()

    if not args.yes:
        raise SystemExit("Refusing to modify BU03 without --yes")

    captured_at = datetime.now().astimezone()
    record: dict[str, object] = {
        "port": args.port,
        "baud": args.baud,
        "captured_at": captured_at.isoformat(timespec="seconds"),
        "requested_pdoa_role": args.role,
        "commands": [],
    }
    commands = record["commands"]
    assert isinstance(commands, list)

    with serial.Serial(args.port, args.baud, timeout=0.05) as port:
        port.dtr = False
        port.rts = False
        read_until_quiet(port, 0.5)

        before = exchange(port, "AT+GETCFG")
        commands.append(before)
        current_id, current_role, _, _ = parse_cfg(before)
        if current_id != args.expect_id or current_role != args.expect_role:
            raise RuntimeError(
                f"Identity mismatch: expected ID {args.expect_id}/Role {args.expect_role}, "
                f"got ID {current_id}/Role {current_role}"
            )

        role_value = 1 if args.role == "base" else 0
        for command in (
            "AT+SETUWBMODE=1",
            f"AT+SETCFG=0,{role_value},1,1",
            "AT+SAVE",
        ):
            item = exchange(port, command, 2.0)
            commands.append(item)
            require_ok(item)
            time.sleep(0.2)

        # AT+SAVE restarts the module. Drain its startup text before querying it.
        time.sleep(2.5)
        boot_output = read_until_quiet(port, 1.5)
        record["post_save_boot_hex"] = boot_output.hex(" ")
        record["post_save_boot_display"] = display_bytes(boot_output)

        after = None
        parsed_cfg = None
        for _ in range(3):
            candidate = exchange(port, "AT+GETCFG", 2.0)
            commands.append(candidate)
            try:
                parsed_cfg = parse_cfg(candidate)
                after = candidate
                break
            except RuntimeError:
                time.sleep(0.5)
        if after is None or parsed_cfg is None:
            raise RuntimeError("Unable to read configuration after module restart")

        new_id, new_role, new_channel, new_rate = parsed_cfg
        if (new_id, new_role, new_channel, new_rate) != (0, role_value, 1, 1):
            raise RuntimeError(
                "Verification mismatch after PDOA configuration: "
                f"{new_id},{new_role},{new_channel},{new_rate}"
            )

        pdoa = exchange(port, "AT+PDOAGETCFG")
        commands.append(pdoa)
        require_ok(pdoa)

    record["verified_cfg"] = {
        "id": new_id,
        "role": new_role,
        "channel": new_channel,
        "rate": new_rate,
    }
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = captured_at.strftime("%Y%m%d_%H%M%S")
    output = args.output_dir / f"pdoa_{args.role}_{args.port.lower()}_{stamp}.json"
    output.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"saved: {output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
