#!/usr/bin/env python3
"""GDB Remote Serial Protocol (RSP) client for MCS251 model qualification.

T10 infrastructure. Pure Python standard library, transport-agnostic so the
self-test can drive it with a scripted fake transport (no QEMU required).

Transport contract (duck-typed, implemented by rsp.SocketTransport and by the
fake transports in qualify.py):

    transport.write(data: bytes) -> None
    transport.read(n: int, timeout: float) -> bytes
        Blocks until at least 1 byte is available, returns between 1 and n
        bytes. Raises the builtin TimeoutError if nothing arrives within
        `timeout` seconds. (socket.timeout is an alias of TimeoutError on
        Python >= 3.10, so real sockets fit naturally.)

Protocol facts (frozen, verified against the model source, read-only):

  * QEMU target/mcs51/gdbstub.c, TARGET_MCS251 branch:
      - GDB core registers 0-31  -> R0..R31   (1 byte each)
      - GDB core registers 32-39 -> R56..R63  (1 byte each, position 56+n-32)
      - GDB core register 40     -> PSW       (1 byte)
      - GDB core register 41     -> PSW1      (1 byte)
      - GDB core register 42     -> PC        (4 bytes, big-endian;
                                               write path uses ldl_be_p)
    Hence the 'g' reply payload is exactly 42 * 1 + 4 = 46 bytes.
  * SFR physical window: 0x01000000 + (SFR address - 0x80)
    (target/mcs51/cpu.h: MCS51_SFR_PHYS_BASE = 0x01000000,
     MCS251_SFR_BASE = 0x80; hw/mcs51/stc32g.c maps the SFR region there.)
  * The extended stack pointer SPX is composed of SPH:SP
    (target/mcs51/helper.c: push/pop use get_reg(MCS251_REG_SPH, 2)),
    readable through the SFR window at SPH=0x85 and SP=0x81.

Packet framing implemented here:

  $<payload>#<csum>   where csum = sum(payload bytes) mod 256, two lowercase
                      hex digits. Payload bytes 0x24 ('$'), 0x23 ('#'),
                      0x7d ('}') and 0x2a ('*') are escaped on send as
                      '}' <byte ^ 0x20>; unescaped on receive. On receive,
                      '*'<ch> additionally means run-length encoding
                      (repeat previous byte ord(ch) - 29 times); RLE decoding
                      is opt-in because it is only valid in contexts that use
                      it (e.g. qXfer binary data), never for hex payloads
                      such as register/memory reads.
"""

import socket

# ---------------------------------------------------------------------------
# Errors
# ---------------------------------------------------------------------------


class RspError(Exception):
    """Base class for RSP client errors."""


class RspTimeout(RspError):
    """No complete response arrived within the deadline.

    Classified as TIMEOUT by qualify.py; a timeout is never a PASS.
    """


class RspProtocolError(RspError):
    """Malformed packet, checksum mismatch or error reply from the stub."""


# ---------------------------------------------------------------------------
# Packet framing (pure functions, covered by the self-test)
# ---------------------------------------------------------------------------

PACKET_START = 0x24  # '$'
PACKET_END = 0x23  # '#'
ESCAPE = 0x7D  # '}'
RUNLEN = 0x2A  # '*'

# Bytes that must be escaped inside an outgoing payload.
_MUST_ESCAPE = frozenset((PACKET_START, PACKET_END, ESCAPE, RUNLEN))


def checksum(data: bytes) -> int:
    """RSP checksum: arithmetic sum of all payload bytes, mod 256."""
    return sum(data) & 0xFF


def checksum_hex(data: bytes) -> str:
    """Two lowercase hex digits of checksum(data)."""
    return "%02x" % checksum(data)


def escape_payload(payload: bytes) -> bytes:
    """Escape '$', '#', '}' and '*' in an outgoing payload."""
    out = bytearray()
    for byte in payload:
        if byte in _MUST_ESCAPE:
            out.append(ESCAPE)
            out.append(byte ^ 0x20)
        else:
            out.append(byte)
    return bytes(out)


def unescape_payload(data: bytes) -> bytes:
    """Undo '}' escaping. A trailing lone '}' is a protocol error."""
    out = bytearray()
    i = 0
    while i < len(data):
        byte = data[i]
        if byte == ESCAPE:
            if i + 1 >= len(data):
                raise RspProtocolError("dangling escape at end of payload")
            out.append(data[i + 1] ^ 0x20)
            i += 2
        else:
            out.append(byte)
            i += 1
    return bytes(out)


def apply_rle(data: bytes) -> bytes:
    """Decode '*'<ch> run-length encoding (repeat prev byte ord(ch)-29)."""
    out = bytearray()
    i = 0
    while i < len(data):
        byte = data[i]
        if byte == RUNLEN:
            if i + 1 >= len(data):
                raise RspProtocolError("dangling run-length marker")
            if i < 1:
                raise RspProtocolError("run-length marker with no predecessor")
            count = data[i + 1] - 29
            if count < 1:
                raise RspProtocolError("run-length count < 1")
            if not out:
                raise RspProtocolError("run-length marker with empty prefix")
            out.extend(out[-1:] * count)
            i += 2
        else:
            out.append(byte)
            i += 1
    return bytes(out)


def encode_packet(payload: bytes) -> bytes:
    """Full wire packet for a payload, including escapes and checksum."""
    wire = escape_payload(payload)
    return b"$" + wire + b"#" + checksum_hex(wire).encode("ascii")


def parse_packet(wire: bytes) -> bytes:
    """Parse a complete wire packet '$<escaped>#<csum>' into its payload.

    The checksum is verified over the escaped payload as transmitted.
    Returns the payload with escapes removed. Raises RspProtocolError on
    framing or checksum errors.
    """
    if len(wire) < 4 or wire[0:1] != b"$":
        raise RspProtocolError("packet does not start with '$'")
    hash_pos = wire.rindex(b"#") if b"#" in wire else -1
    if hash_pos < 0:
        raise RspProtocolError("packet has no '#' terminator")
    framed = wire[1:hash_pos]
    csum_field = wire[hash_pos + 1:]
    if len(csum_field) != 2:
        raise RspProtocolError("checksum field is not two digits")
    try:
        expected = int(csum_field.decode("ascii"), 16)
    except (UnicodeDecodeError, ValueError):
        raise RspProtocolError("checksum field is not hex: %r" % csum_field)
    actual = checksum(framed)
    if expected != actual:
        raise RspProtocolError(
            "checksum mismatch: field %02x, computed %02x" % (expected, actual)
        )
    return unescape_payload(framed)


# ---------------------------------------------------------------------------
# Frozen register layout (verified against target/mcs51/gdbstub.c)
# ---------------------------------------------------------------------------

GDB_REG_PC = 42


def _gdb_register_table():
    table = []
    for n in range(0, 32):
        table.append((n, "R%d" % n, 1))
    for n in range(0, 8):
        table.append((32 + n, "R%d" % (56 + n), 1))
    table.append((40, "PSW", 1))
    table.append((41, "PSW1", 1))
    table.append((GDB_REG_PC, "PC", 4))
    return tuple(table)


GDB_REGISTERS = _gdb_register_table()
GDB_REG_REPLY_SIZE = sum(width for _, _, width in GDB_REGISTERS)  # 46
GDB_REG_NAME_TO_INDEX = {name: index for index, name, _ in GDB_REGISTERS}


def _decode_hex_reply(reply: bytes, size: int) -> bytes:
    """Decode exactly two ASCII hex digits per register byte, as RSP sends."""
    if len(reply) != size * 2:
        raise RspProtocolError(
            "register reply is %d hex characters, expected %d"
            % (len(reply), size * 2)
        )
    if any(byte not in b"0123456789abcdefABCDEF" for byte in reply):
        raise RspProtocolError("register reply contains non-hex characters")
    return bytes.fromhex(reply.decode("ascii"))


def decode_registers(reply: bytes) -> dict:
    """Decode an ASCII-hex 'g' reply into {name: value}.

    Every register is transmitted big-endian (target byte order, MSB-first);
    for the 1-byte registers big-endian is trivially the byte itself. The
    reply length must match the frozen table exactly, otherwise the register
    boundaries would be guesses and we refuse the data.
    """
    reply = _decode_hex_reply(reply, GDB_REG_REPLY_SIZE)
    values = {}
    offset = 0
    for _, name, width in GDB_REGISTERS:
        chunk = reply[offset:offset + width]
        values[name] = int.from_bytes(chunk, byteorder="big", signed=False)
        offset += width
    return values


def encode_registers(values: dict) -> bytes:
    """Inverse of decode_registers: build a 'g'-style reply from {name: value}."""
    out = bytearray()
    for _, name, width in GDB_REGISTERS:
        if name not in values:
            raise RspProtocolError("missing register %s for encode" % name)
        value = values[name]
        if not 0 <= value < (1 << (8 * width)):
            raise RspProtocolError(
                "value 0x%x does not fit %s (%d bytes)" % (value, name, width)
            )
        out.extend(value.to_bytes(width, byteorder="big"))
    return bytes(out).hex().encode("ascii")


def decode_single_register(reply: bytes, width: int) -> int:
    """Decode an ASCII-hex 'p' reply of the given width, big-endian."""
    raw = _decode_hex_reply(reply, width)
    return int.from_bytes(raw, byteorder="big", signed=False)


# ---------------------------------------------------------------------------
# SFR window (frozen model interface)
# ---------------------------------------------------------------------------

SFR_PHYS_BASE = 0x01000000
SFR_BASE = 0x80


def sfr_phys_addr(sfr_addr: int) -> int:
    """Physical address of an SFR in the model memory map."""
    if not SFR_BASE <= sfr_addr <= 0xFF:
        raise RspProtocolError("SFR address 0x%02x outside 0x80..0xFF" % sfr_addr)
    return SFR_PHYS_BASE + sfr_addr - SFR_BASE


# SFR addresses used by the qualification harness (target/mcs51/cpu.h enum).
SFR_SP = 0x81
SFR_DPL = 0x82
SFR_DPH = 0x83
SFR_DPXL = 0x84
SFR_SPH = 0x85
SFR_IE = 0xA8
SFR_IP = 0xB8
SFR_PSW = 0xD0
SFR_PSW1 = 0xD1
SFR_DPS = 0xE3


# ---------------------------------------------------------------------------
# Transports
# ---------------------------------------------------------------------------


class SocketTransport:
    """TCP or unix-socket transport matching the contract in the module doc."""

    def __init__(self, sock: socket.socket):
        self._sock = sock

    @classmethod
    def connect_tcp(cls, host: str, port: int, timeout: float = 10.0):
        sock = socket.create_connection((host, port), timeout=timeout)
        sock.settimeout(timeout)
        return cls(sock)

    @classmethod
    def connect_unix(cls, path: str, timeout: float = 10.0):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect(path)
        return cls(sock)

    def write(self, data: bytes) -> None:
        self._sock.sendall(data)

    def read(self, n: int, timeout: float) -> bytes:
        self._sock.settimeout(timeout)
        try:
            return self._sock.recv(n)
        except socket.timeout:
            raise TimeoutError("socket read timed out after %.3fs" % timeout)

    def close(self) -> None:
        try:
            self._sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------
# Client
# ---------------------------------------------------------------------------

MAX_ACK_RESENDS = 3


class RspClient:
    """Framing-aware RSP client over an arbitrary transport."""

    def __init__(self, transport, default_timeout: float = 10.0):
        self._transport = transport
        self._default_timeout = default_timeout

    def close(self) -> None:
        self._transport.close()

    # -- low-level ----------------------------------------------------------

    def _send_ack(self, ack: bytes) -> None:
        self._transport.write(ack)

    def _recv_one_byte(self, timeout: float) -> int:
        data = self._transport.read(1, timeout)
        if not data:
            raise RspProtocolError("transport returned EOF")
        return data[0]

    def send_packet(self, payload: bytes, timeout: float = None) -> None:
        """Send one packet and wait for the '+' acknowledgement."""
        timeout = self._default_timeout if timeout is None else timeout
        packet = encode_packet(payload)
        for _ in range(MAX_ACK_RESENDS + 1):
            self._transport.write(packet)
            try:
                byte = self._recv_one_byte(timeout)
            except TimeoutError:
                raise RspTimeout(
                    "no acknowledgement within %.3fs" % timeout
                ) from None
            if byte == 0x2B:  # '+'
                return
            if byte == 0x2D:  # '-': stub rejected the checksum, retransmit
                continue
            raise RspProtocolError(
                "expected ack, got 0x%02x" % byte
            )
        raise RspProtocolError("stub kept rejecting the packet with '-'")

    def recv_packet(self, timeout: float = None) -> bytes:
        """Receive one packet. Skips acks and '%' notifications.

        Raises RspTimeout if the deadline passes without a complete packet.
        On a checksum error, NAKs the stub and raises RspProtocolError.
        """
        timeout = self._default_timeout if timeout is None else timeout
        while True:
            start_byte = self._recv_byte_within(timeout)
            if start_byte in (0x2B, 0x2D):  # stray acks between packets
                continue
            if start_byte == 0x25:  # '%': async notification, discard it
                self._discard_notification(timeout)
                continue
            if start_byte != PACKET_START:
                raise RspProtocolError(
                    "expected packet start '$', got 0x%02x" % start_byte
                )
            framed, csum_field = self._read_framed_payload(timeout)
            try:
                payload = parse_packet(
                    b"$" + framed + b"#" + csum_field
                )
            except RspProtocolError:
                self._send_ack(b"-")
                raise
            self._send_ack(b"+")
            return payload

    def _recv_byte_within(self, timeout: float) -> int:
        try:
            return self._recv_one_byte(timeout)
        except TimeoutError:
            raise RspTimeout("no packet within %.3fs" % timeout) from None

    def _read_framed_payload(self, timeout: float):
        """Read payload bytes until the unescaped '#', then the 2-digit csum."""
        framed = bytearray()
        escaped = False
        while True:
            byte = self._recv_byte_within(timeout)
            if escaped:
                framed.append(byte)
                escaped = False
                continue
            if byte == ESCAPE:
                framed.append(byte)
                escaped = True
                continue
            if byte == PACKET_END:
                break
            framed.append(byte)
        csum = bytearray()
        while len(csum) < 2:
            csum.append(self._recv_byte_within(timeout))
        return bytes(framed), bytes(csum)

    def _discard_notification(self, timeout: float) -> None:
        """Discard a '%' notification up to and including '#'+2 hex digits."""
        self._read_framed_payload(timeout)

    # -- request/reply ------------------------------------------------------

    def command(self, payload: bytes, timeout: float = None) -> bytes:
        """Send a command packet and return the decoded reply payload."""
        timeout = self._default_timeout if timeout is None else timeout
        self.send_packet(payload, timeout=timeout)
        return self.recv_packet(timeout=timeout)

    # -- protocol convenience -----------------------------------------------

    def interrupt_target(self) -> None:
        """Send the out-of-band interrupt byte 0x03 (no packet framing)."""
        self._transport.write(b"\x03")

    def halt_reason(self, timeout: float = None) -> str:
        reply = self.command(b"?", timeout=timeout)
        return reply.decode("ascii", errors="replace")

    def read_all_registers(self, timeout: float = None) -> dict:
        reply = self.command(b"g", timeout=timeout)
        if reply.startswith(b"E"):
            raise RspProtocolError(
                "'g' rejected by stub: %s" % reply.decode("ascii", "replace")
            )
        return decode_registers(reply)

    def read_register(self, index: int, timeout: float = None) -> int:
        width = self._register_width(index)
        reply = self.command(
            b"p%x" % index, timeout=timeout
        )
        if reply.startswith(b"E"):
            raise RspProtocolError(
                "'p' rejected by stub: %s" % reply.decode("ascii", "replace")
            )
        return decode_single_register(reply, width)

    @staticmethod
    def _register_width(index: int) -> int:
        for reg_index, _, width in GDB_REGISTERS:
            if reg_index == index:
                return width
        raise RspProtocolError("unknown GDB register index %d" % index)

    def read_memory(self, addr: int, length: int, timeout: float = None) -> bytes:
        if length <= 0:
            raise RspProtocolError("read_memory length must be positive")
        reply = self.command(
            b"m%x,%x" % (addr, length), timeout=timeout
        )
        if reply.startswith(b"E"):
            raise RspProtocolError(
                "memory read at 0x%x rejected: %s"
                % (addr, reply.decode("ascii", "replace"))
            )
        try:
            return bytes.fromhex(reply.decode("ascii"))
        except (UnicodeDecodeError, ValueError):
            raise RspProtocolError(
                "memory reply is not hex: %r" % reply
            ) from None

    def read_sfr(self, sfr_addr: int, timeout: float = None) -> int:
        """Read one SFR byte through the frozen physical window."""
        data = self.read_memory(sfr_phys_addr(sfr_addr), 1, timeout=timeout)
        return data[0]

    def read_spx(self, timeout: float = None) -> int:
        """Read the architectural SPX as SPH:SP through the SFR window."""
        sph = self.read_sfr(SFR_SPH, timeout=timeout)
        sp = self.read_sfr(SFR_SP, timeout=timeout)
        return (sph << 8) | sp

    def continue_async(self, timeout: float = None) -> None:
        """Send 'c'; the stop reply is collected later via wait_stop()."""
        self.send_packet(b"c", timeout=timeout)

    def step(self, timeout: float = None) -> str:
        """Single-step and return the stop reply text (e.g. 'S05'/'T05...')."""
        return self.command(b"s", timeout=timeout).decode("ascii", "replace")

    def wait_stop(self, timeout: float = None) -> str:
        """Wait for a stop reply ('S..'/'T..') after continue_async()."""
        timeout = self._default_timeout if timeout is None else timeout
        while True:
            reply = self.recv_packet(timeout=timeout)
            text = reply.decode("ascii", errors="replace")
            if text[:1] in ("S", "T"):
                return text
            # Anything else while running (e.g. an 'O' console packet) is
            # not a stop; keep waiting inside the same deadline.

    def detach(self, timeout: float = None) -> None:
        try:
            self.command(b"D", timeout=timeout)
        except RspError:
            pass
