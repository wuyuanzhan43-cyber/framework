#!/usr/bin/env python3
"""Hardware-in-the-loop protocol smoke test.

Usage:
    python tests/hil/test_protocol.py --port COM7
    python tests/hil/test_protocol.py --port /dev/ttyUSB0 --baud 115200

Requires pyserial. The device must run the protocol-mode firmware
(APP_UART_HELLO_TEST=0) and be wired through a USB-UART adapter.
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: python -m pip install pyserial")

MAX_PAYLOAD = 32
CMD_PING = 0x01
CMD_LED_SET = 0x02
CMD_STATUS = 0x03
CMD_STATS = 0x04
EVENT_STATUS = 0x90
REPLY = 0x80


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(command, sequence, payload=b""):
    body = bytes([len(payload), sequence, command]) + payload
    crc = crc16(body)
    return b"\xA5\x5A" + body + bytes([crc & 0xFF, crc >> 8])


class FrameReader:
    def __init__(self, port):
        self.port = port
        self.buffer = bytearray()

    def read_frame(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            frame = self._take_frame()
            if frame is not None:
                return frame
            chunk = self.port.read(1)
            if chunk:
                self.buffer.extend(chunk)
        return None

    def _take_frame(self):
        while True:
            index = self.buffer.find(b"\xA5\x5A")
            if index < 0:
                if len(self.buffer) > 1:
                    del self.buffer[:-1]
                return None
            if index:
                del self.buffer[:index]
            if len(self.buffer) < 3:
                return None
            length = self.buffer[2]
            if length > MAX_PAYLOAD:
                del self.buffer[:1]
                continue
            total = length + 7
            if len(self.buffer) < total:
                return None
            frame = bytes(self.buffer[:total])
            del self.buffer[:total]
            body = frame[2:total - 2]
            received = frame[total - 2] | (frame[total - 1] << 8)
            if crc16(body) != received:
                continue
            return frame[3], frame[4], frame[5:5 + length]


def wait_reply(reader, command, sequence, timeout=2.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        frame = reader.read_frame(deadline - time.monotonic())
        if frame is None:
            break
        seq, cmd, payload = frame
        if cmd == EVENT_STATUS:
            continue  # autonomous report, not a reply
        if cmd == (command | REPLY) and seq == sequence:
            return payload
    raise AssertionError("no reply for cmd 0x%02X seq %u" % (command, sequence))


def expect(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--stats", action="store_true",
                        help="also test CMD_STATS (requires current firmware)")
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.1) as port:
        reader = FrameReader(port)
        port.reset_input_buffer()

        print("1. waiting for autonomous 0x90 report...")
        frame = reader.read_frame(args.timeout)
        expect(frame is not None, "no autonomous report; check wiring/baud/mode")
        seq, cmd, payload = frame
        expect(cmd == EVENT_STATUS, "expected 0x90, got 0x%02X" % cmd)
        expect(len(payload) == 3 and payload[0] == 0, "bad report payload")
        print("   ok (seq=%u known=%u state=%u)" % (seq, payload[1], payload[2]))

        print("2. PING")
        port.write(encode(CMD_PING, 1))
        payload = wait_reply(reader, CMD_PING, 1)
        expect(payload[:1] == b"\x00" and payload[1:] == b"PONG", "bad PING reply")

        print("3. LED on, STATUS, LED off, STATUS")
        port.write(encode(CMD_LED_SET, 2, b"\x01"))
        payload = wait_reply(reader, CMD_LED_SET, 2)
        expect(payload[0] == 0, "LED on rejected")
        port.write(encode(CMD_STATUS, 3))
        payload = wait_reply(reader, CMD_STATUS, 3)
        expect(payload == b"\x00\x01\x01", "STATUS after LED on: %r" % payload)
        port.write(encode(CMD_LED_SET, 4, b"\x00"))
        payload = wait_reply(reader, CMD_LED_SET, 4)
        expect(payload[0] == 0, "LED off rejected")
        port.write(encode(CMD_STATUS, 5))
        payload = wait_reply(reader, CMD_STATUS, 5)
        expect(payload == b"\x00\x01\x00", "STATUS after LED off: %r" % payload)

        print("4. unknown command")
        port.write(encode(0x7F, 6))
        payload = wait_reply(reader, 0x7F, 6)
        expect(payload[0] == 2, "unknown command status: %r" % payload)

        print("5. bad LED payload")
        port.write(encode(CMD_LED_SET, 7, b"\x02"))
        payload = wait_reply(reader, CMD_LED_SET, 7)
        expect(payload[0] == 1, "bad argument status: %r" % payload)

        print("6. noise recovery")
        port.write(b"\x00\xA5\x00")
        port.write(encode(CMD_PING, 8))
        payload = wait_reply(reader, CMD_PING, 8)
        expect(payload[:1] == b"\x00" and payload[1:] == b"PONG", "noise recovery failed")

        print("7. fragmented frame")
        frame = encode(CMD_PING, 9)
        port.write(frame[:3])
        time.sleep(0.05)
        port.write(frame[3:])
        payload = wait_reply(reader, CMD_PING, 9)
        expect(payload[:1] == b"\x00" and payload[1:] == b"PONG", "fragmented frame failed")

        if args.stats:
            print("8. STATS")
            port.write(encode(CMD_STATS, 10))
            payload = wait_reply(reader, CMD_STATS, 10)
            expect(len(payload) == 14 and payload[0] == 0,
                   "bad STATS reply: %r" % payload)
            frames = int.from_bytes(payload[1:5], "little")
            io_errors = int.from_bytes(payload[5:9], "little")
            partials = int.from_bytes(payload[9:13], "little")
            expect(frames >= 9, "STATS frames too small: %u" % frames)
            print("   ok (frames=%u io_errors=%u partial_timeouts=%u report_seq=%u)"
                  % (frames, io_errors, partials, payload[13]))

    print("PASS HIL protocol smoke test")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as error:
        print("FAIL:", error)
        sys.exit(1)
    except serial.SerialException as error:
        print("SERIAL ERROR:", error)
        sys.exit(2)
