from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path

import serial

from bu03_at_backup import display_bytes, read_until_quiet
from bu03_pdoa_config import exchange, parse_cfg, require_ok


def main() -> int:
    parser = argparse.ArgumentParser(description="Restore one BU03-Kit as a TWR anchor")
    parser.add_argument("port")
    parser.add_argument("--anchor-id", type=int, required=True)
    parser.add_argument("--expect-id", type=int, required=True)
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
        "requested_twr_anchor_id": args.anchor_id,
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
        if current_id != args.expect_id or current_role != 1:
            raise RuntimeError(
                f"Identity mismatch: expected ID {args.expect_id}/Role 1, "
                f"got ID {current_id}/Role {current_role}"
            )

        for command in (
            "AT+SETUWBMODE=0",
            f"AT+SETCFG={args.anchor_id},1,1,1",
            "AT+SAVE",
        ):
            item = exchange(port, command, 2.0)
            commands.append(item)
            require_ok(item)
            time.sleep(0.2)

        time.sleep(2.5)
        boot_output = read_until_quiet(port, 1.5)
        record["post_save_boot_hex"] = boot_output.hex(" ")
        record["post_save_boot_display"] = display_bytes(boot_output)

        parsed_cfg = None
        for _ in range(3):
            after = exchange(port, "AT+GETCFG", 2.0)
            commands.append(after)
            try:
                parsed_cfg = parse_cfg(after)
                break
            except RuntimeError:
                time.sleep(0.5)
        if parsed_cfg is None:
            raise RuntimeError("Unable to read configuration after module restart")

        new_id, new_role, new_channel, new_rate = parsed_cfg
        if (new_id, new_role, new_channel, new_rate) != (
            args.anchor_id,
            1,
            1,
            1,
        ):
            raise RuntimeError(
                "Verification mismatch after TWR restore: "
                f"{new_id},{new_role},{new_channel},{new_rate}"
            )

    record["verified_cfg"] = {
        "id": new_id,
        "role": new_role,
        "channel": new_channel,
        "rate": new_rate,
    }
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = captured_at.strftime("%Y%m%d_%H%M%S")
    output = args.output_dir / f"twr_anchor{args.anchor_id}_{args.port.lower()}_{stamp}.json"
    output.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"saved: {output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
