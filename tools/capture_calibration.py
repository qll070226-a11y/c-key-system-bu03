import argparse
import math
import re
import statistics
import time

import serial


LINE_PATTERN = re.compile(
    r'UWB ok=\d+ bad=\d+ mask=0x([0-9A-Fa-f]+) A0=(\d+) A1=(\d+) mm'
)


def summarize(name, values, expected_mm):
    median = statistics.median(values)
    mean = statistics.mean(values)
    stdev = statistics.stdev(values) if len(values) > 1 else 0.0
    print(
        f'{name}: mean={mean:.2f} median={median:.2f} '
        f'stdev={stdev:.2f} min={min(values)} max={max(values)} '
        f'correction={expected_mm - median:+.2f} mm'
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('port')
    parser.add_argument('--seconds', type=float, default=40.0)
    parser.add_argument('--forward-mm', type=float, default=2000.0)
    parser.add_argument('--half-baseline-mm', type=float, default=240.0)
    args = parser.parse_args()

    expected_mm = math.hypot(args.forward_mm, args.half_baseline_mm)
    data = bytearray()
    with serial.Serial(args.port, 115200, timeout=0.15) as port:
        port.dtr = False
        port.rts = True
        time.sleep(0.12)
        port.rts = False
        deadline = time.time() + args.seconds
        while time.time() < deadline:
            data.extend(port.read(4096))

    text = data.decode('utf-8', 'replace')
    pairs = []
    for mask_text, a0_text, a1_text in LINE_PATTERN.findall(text):
        if int(mask_text, 16) & 0x03 == 0x03:
            pairs.append((int(a0_text), int(a1_text)))

    print(f'expected={expected_mm:.2f} mm samples={len(pairs)}')
    if not pairs:
        raise SystemExit('No valid dual-anchor telemetry was captured')
    print('pairs=' + ','.join(f'{a0}/{a1}' for a0, a1 in pairs))
    summarize('A0', [pair[0] for pair in pairs], expected_mm)
    summarize('A1', [pair[1] for pair in pairs], expected_mm)


if __name__ == '__main__':
    main()
