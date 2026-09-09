#!/usr/bin/env python3
"""Check linked images and exercise the QEMU-only reporting profile."""
import argparse
import re
import selectors
import subprocess
import tempfile
import time
from pathlib import Path


def load_hex(path):
    memory = {}
    base = 0
    for line in path.read_text().splitlines():
        b = bytes.fromhex(line[1:])
        assert len(b) == b[0] + 5 and sum(b) % 256 == 0
        address, kind, data = int.from_bytes(b[1:3], 'big'), b[3], b[4:-1]
        if kind == 4:
            base = int.from_bytes(data, 'big') << 16
        elif kind == 0:
            for i, value in enumerate(data):
                assert base + address + i not in memory
                memory[base + address + i] = value
        else:
            assert kind == 1
    return memory


def write_hex(path, memory):
    def record(address, kind, data):
        b = bytes([len(data)]) + address.to_bytes(2, 'big') + bytes([kind]) + data
        return ':' + (b + bytes([-sum(b) & 255])).hex().upper()
    lines = [record(0, 4, b'\x00\xff')]
    addresses = sorted(memory)
    for a in addresses:
        lines.append(record(a & 65535, 0, bytes([memory[a]])))
    lines.append(record(0, 1, b''))
    path.write_text('\n'.join(lines) + '\n')


def run(qemu, image):
    p = subprocess.Popen([qemu, '-M', 'stc32g144k246', '-bios', str(image),
                          '-nographic', '-monitor', 'none', '-serial', 'stdio'],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    data = b''
    last_send = 0.0
    selector = selectors.DefaultSelector()
    selector.register(p.stdout, selectors.EVENT_READ)
    try:
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            for key, _ in selector.select(.1):
                data += key.fileobj.read1(4096)
            if (b'send g\r\n' in data and b'HWF2 RUN' not in data
                    and time.monotonic() - last_send > .1):
                p.stdin.write(b'g')
                p.stdin.flush()
                last_send = time.monotonic()
            if len(re.findall(rb'W@01F0=[^\r]*\r\n', data)) >= 3:
                return data.decode()
        raise AssertionError('missing repeated reports: ' + repr(data[-1500:]))
    finally:
        p.terminate()
        p.wait(timeout=3)
        selector.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('hardware', type=Path)
    ap.add_argument('simulation', type=Path)
    ap.add_argument('--qemu', default='/home/liu/build-qemu/qemu-system-mcs251')
    args = ap.parse_args()
    for directory in (args.hardware, args.simulation):
        memory = load_hex(directory / 'hwframe.hex')
        symbols = {name: int(value, 16) for value, name in re.findall(
            r'C:\s+([0-9A-F]+)\s+(_\w+)', (directory / 'hwframe.map').read_text())}
        assert min(memory) == 0xff0000 and max(memory) <= 0xffffff
        assert bytes(memory[0xff0000+i] for i in range(3)) == b'\x02\x02\x00'
        assert bytes(memory[0xff000b+i] for i in range(4)) == b'\x8a' + symbols['_hwf_isr'].to_bytes(3, 'big')
        assert memory[symbols['_hwf_reti']] == 0x32
    # The hand-written module has identical addresses in both profiles.
    original = load_hex(args.simulation / 'hwframe.hex')
    with tempfile.TemporaryDirectory(prefix='hwf2-check-') as tmp:
        for case in ('normal', 'timeout', 'sp-mismatch'):
            memory = original.copy()
            if case == 'timeout':
                matches = [a for a in range(0xff0200, symbols['_hwf_wait'])
                           if bytes(memory.get(a+i, 0) for i in range(3)) == b'\x75\xa8\x82']
                assert len(matches) == 1
                memory[matches[0]+2] = 0
            elif case == 'sp-mismatch':
                # Redirect RETI to the return-side sampler without popping.
                a = symbols['_hwf_reti'] - 3
                assert bytes(memory[a+i] for i in range(3)) == b'\x75\x99\x49'
                for i, b in enumerate(b'\x8a' + symbols['_hwf_wait_end'].to_bytes(3, 'big')):
                    memory[a+i] = b
            image = Path(tmp) / (case + '.hex')
            write_hex(image, memory)
            text = run(args.qemu, image)
            reports = re.findall(r'HWF2 STATUS=[^\r]+', text)
            windows = re.findall(r'W@01F0=([^\r]+)', text)
            assert len(set(reports)) == 1 and len(set(windows)) == 1
            if case == 'timeout':
                assert 'STATUS=TIMEOUT B=01FE I=0000 R=01FE N=----' in text
                assert windows[0] == 'UNAVAILABLE' and 'I RETURN' not in text
            else:
                expected = 'CAPTURED' if case == 'normal' else 'SP_MISMATCH'
                assert 'STATUS=' + expected in text
                if case == 'normal':
                    assert 'I RETURN' in text
                else:
                    assert 'R=0202' in text
                assert 'I=0202' in text and 'N=0004' in text
                values = bytes.fromhex(windows[0])
                assert len(values) == 32
                assert values[:15] == b'\xa5' * 15 and values[19:] == b'\xa5' * 13
                pc = (values[16] << 16) | (values[18] << 8) | values[17]
                assert symbols['_hwf_wait'] <= pc <= symbols['_hwf_wait_end']
            print(case + ': PASS; ' + reports[0])
    print('HEX checksums, vectors, repeated snapshot stability: PASS')


if __name__ == '__main__':
    main()
