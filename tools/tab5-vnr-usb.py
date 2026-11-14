# @file tab5-vnr-usb.py
# @author 煲仔卤煮(ZealerLu)
# @date 2026-11-11
#
# @copyright Copyright (c) 2026
#
"""List or download TAB5 recordings in the recorder's USB export mode."""

import argparse
import base64
from pathlib import Path
import re
import sys
import time
import zlib

import serial


def frames(port, timeout=8):
    deadline = time.monotonic() + timeout
    buffer = bytearray()
    while time.monotonic() < deadline:
        data = port.read(port.in_waiting or 1)
        if not data:
            continue
        buffer.extend(data)
        while (end := buffer.find(b'\n')) >= 0:
            line = bytes(buffer[:end]).rstrip(b'\r')
            del buffer[:end + 1]
            if line.startswith(b'VNUSB '):
                deadline = time.monotonic() + timeout
                yield line[6:]
        if len(buffer) > 4096:
            buffer.clear()
    raise TimeoutError('No reply. Enable USB export under TAB5 recording settings.')


def list_files(port):
    port.write(b'\nVNUSB LIST\n')
    for frame in frames(port):
        if frame == b'LIST_END':
            return
        if frame.startswith(b'FILE '):
            print(frame[5:].decode('ascii'))
        elif frame.startswith(b'ERR '):
            raise OSError(frame[4:].decode('ascii'))


def download(port, name, output):
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,8}\.VNR', name, re.IGNORECASE | re.ASCII):
        raise ValueError('Use a VNR filename from the list command.')
    if output.exists():
        raise FileExistsError(output)
    partial = output.with_name(output.name + '.part')
    received = crc = 0
    expected = None
    with partial.open('xb') as file:
        try:
            port.write(f'\nVNUSB GET {name}\n'.encode('ascii'))
            for frame in frames(port):
                if frame.startswith(b'BEGIN ') and expected is None:
                    expected = int(frame[6:])
                elif frame.startswith(b'DATA ') and expected is not None:
                    block = base64.b64decode(frame[5:], validate=True)
                    received += len(block)
                    if received > expected:
                        raise ValueError('File length exceeded the expected size.')
                    crc = zlib.crc32(block, crc)
                    file.write(block)
                elif frame.startswith(b'END '):
                    if received != expected or crc != int(frame[4:], 16):
                        raise ValueError('Download length or CRC32 mismatch.')
                    break
                elif frame.startswith(b'ERR '):
                    raise OSError(frame[4:].decode('ascii'))
            else:
                raise OSError('Incomplete transfer.')
        except BaseException:
            file.close()
            partial.unlink()
            raise
    if output.exists():
        raise FileExistsError(f'{output} already exists. Verified download is at {partial}.')
    partial.rename(output)
    print(f'{output} ({received} bytes, CRC32 {crc:08X})')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', help='TAB5 USB Serial/JTAG port, e.g. COM10')
    command = parser.add_subparsers(dest='command', required=True)
    command.add_parser('list')
    get = command.add_parser('get')
    get.add_argument('name')
    get.add_argument('-o', '--output', type=Path)
    args = parser.parse_args()
    port = serial.Serial(baudrate=115200, timeout=.25, write_timeout=2)
    port.dtr = port.rts = False
    port.port = args.port
    try:
        port.open()
        port.reset_input_buffer()
        if args.command == 'list':
            list_files(port)
        else:
            download(port, args.name, args.output or Path(args.name))
    except (OSError, ValueError, serial.SerialException) as error:
        print(error, file=sys.stderr)
        return 1
    finally:
        port.close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
