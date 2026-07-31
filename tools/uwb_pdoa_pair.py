from __future__ import annotations

import argparse
import json
import re
import time
from datetime import datetime
from pathlib import Path

import serial

from bu03_at_backup import display_bytes, read_until_quiet
from bu03_pdoa_config import exchange, parse_cfg, require_ok


ADDRESS64_PATTERN = re.compile(r"[0-9A-Fa-f]{16}")
PAIRED_ADDRESS_PATTERN = re.compile(
    r'"a64"s*:s*"([0-9A-Fa-f]{16})"',
    re.IGNORECASE,
)


def discovered_addresses(item: dict[str, str]) -> list[str]:
    return [value.upper() for value in ADDRESS64_PATTERN.findall(
        item["response_display"]
    )]


def paired_addresses(item: dict[str, str]) -> list[str]:
    return [value.upper() for value in PAIRED_ADDRESS_PATTERN.findall(
        item["response_display"]
    )]


def main() -> int:
    parser = argparse.ArgumentParser(description="Pair one discovered PDOA tag")
    parser.add_argument("port")
    parser.add_argument("--tag-address", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output-dir", type=Path, default=Path("backups") / "bu04")
    parser.add_argument("--yes", action="store_true")
    args = parser.parse_args()

    if not args.yes:
        raise SystemExit("Refusing to change the PDOA pair list without --yes")

    target = args.tag_address.upper()
    if not re.fullmatch(r"[0-9A-F]{16}", target):
        raise SystemExit("--tag-address must contain exactly 16 hexadecimal digits")
    short_address = target[-4:]

    captured_at = datetime.now().astimezone()
    record: dict[str, object] = {
        "port": args.port,
        "baud": args.baud,
        "captured_at": captured_at.isoformat(timespec="seconds"),
        "target_tag_address": target,
        "target_short_address": short_address,
        "commands": [],
    }
    commands = record["commands"]
    assert isinstance(commands, list)

    with serial.Serial(args.port, args.baud, timeout=0.05) as port:
        port.dtr = False
        port.rts = False
        read_until_quiet(port, 0.5)

        cfg = exchange(port, "AT+GETCFG", 2.0)
        commands.append(cfg)
        _, role, _, _ = parse_cfg(cfg)
        if role != 1:
            raise RuntimeError(f"COM device is not a base station: Role {role}")

        dlist = exchange(port, "AT+GETDLIST", 3.0)
        commands.append(dlist)
        require_ok(dlist)
        discovered = discovered_addresses(dlist)
        record["discovered_before"] = discovered
        if target not in discovered:
            raise RuntimeError(
                f"Target {target} is not in discovered list: {discovered}"
            )

        klist = exchange(port, "AT+GETKLIST", 3.0)
        commands.append(klist)
        require_ok(klist)
        paired = paired_addresses(klist)
        record["paired_before"] = paired

        for address in paired:
            if address == target:
                continue
            item = exchange(port, f"AT+DELTAG={address}", 2.0)
            commands.append(item)
            require_ok(item)

        if target not in paired:
            item = exchange(
                port,
                f"AT+ADDTAG={target},{short_address},1,64,0",
                3.0,
            )
            commands.append(item)
            require_ok(item)

        saved = exchange(port, "AT+SAVE", 2.0)
        commands.append(saved)
        require_ok(saved)
        time.sleep(2.5)
        boot_output = read_until_quiet(port, 1.5)
        record["post_save_boot_hex"] = boot_output.hex(" ")
        record["post_save_boot_display"] = display_bytes(boot_output)

        verified = None
        for _ in range(3):
            candidate = exchange(port, "AT+GETKLIST", 3.0)
            commands.append(candidate)
            if target in paired_addresses(candidate):
                verified = candidate
                break
            time.sleep(0.5)
        if verified is None:
            raise RuntimeError("Target tag was not present after pairing restart")

    record["paired_after"] = [target]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = captured_at.strftime("%Y%m%d_%H%M%S")
    output = args.output_dir / f"pdoa_pair_{args.port.lower()}_{stamp}.json"
    output.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"saved: {output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
