#!/usr/bin/env python3
"""qtest client for MCS251 model qualification.

T10 infrastructure. Pure Python standard library, transport-agnostic so the
self-test can drive it with a scripted fake transport (no QEMU required).

Transport contract: the same as rsp.py — write(data: bytes) and
read(n: int, timeout: float) -> bytes that raises the builtin TimeoutError
when nothing arrives in time.

Wire protocol (verified against the model source, read-only:
QEMU system/qtest.c):

  * Commands are newline-terminated ASCII lines of whitespace-separated words;
    each command answers exactly one line:
      "OK\n", "OK <value>\n", "FAIL <reason>\n" or "ERR <reason>\n".
  * set_irq_in:
      > set_irq_in QOM-PATH NAME NUM LEVEL
      < OK
    NAME may be "unnamed-gpio-in" (mapped to the unnamed GPIO list) or the
    name of a named gpio list. If the QOM path does not resolve, the stub
    answers "FAIL Unknown device" (system/qtest.c uses object_resolve_path
    and returns FAIL instead of guessing).
  * While IRQ interception is active the server may emit unsolicited
    "IRQ <direction> <num> <level>" lines; a client waiting for a command
    response must skip them.
  * Frozen QOM path of the CPU on the stc32g144k246 machine:
      /machine/soc/cpu
    (hw/mcs51/stc32g144k246.c initializes child "soc", hw/mcs51/stc32g.c
    initializes child "cpu" inside the SoC.)
  * The CPU IRQ inputs are unnamed GPIOs; input NUM equals the interrupt
    slot number for slots 0-4 (MCS251_IRQ_INT0..MCS251_IRQ_UART1 in
    target/mcs51/cpu.h). The SoC wires the timer and UART1 devices to these
    inputs; other slots can still be raised directly through set_irq_in if
    the model implements the input — a FAIL there is a model capability
    limit and must be recorded as such, never retried against a guessed
    path.
"""

# ---------------------------------------------------------------------------
# Errors
# ---------------------------------------------------------------------------


class QTestError(Exception):
    """Base class for qtest client errors."""


class QTestTimeout(QTestError):
    """No response arrived within the deadline.

    Classified as TIMEOUT by qualify.py; a timeout is never a PASS.
    """


class QTestCommandError(QTestError):
    """The stub answered FAIL or ERR."""

    def __init__(self, command: str, reason: str):
        super().__init__("qtest %r failed: %s" % (command, reason))
        self.command = command
        self.reason = reason


# ---------------------------------------------------------------------------
# Frozen interface constants (see module docstring for provenance)
# ---------------------------------------------------------------------------

QOM_CPU_PATH = "/machine/soc/cpu"
IRQ_GPIO_NAME_UNNAMED = "unnamed-gpio-in"

# Model capability note: only IRQ inputs 0-4 are wired to devices by the SoC.
DEVICE_WIRED_IRQ_INPUTS = (0, 1, 2, 3, 4)


# ---------------------------------------------------------------------------
# Line protocol helpers (pure functions, covered by the self-test)
# ---------------------------------------------------------------------------


def encode_command(*words) -> bytes:
    """Encode one command line. Words must contain no whitespace/newline."""
    for word in words:
        text = str(word)
        if any(ch.isspace() for ch in text):
            raise QTestError("command word contains whitespace: %r" % text)
        if "\n" in text or "\r" in text:
            raise QTestError("command word contains newline: %r" % text)
    return (" ".join(str(word) for word in words) + "\n").encode("ascii")


def parse_response(line: str) -> tuple:
    """Split a response line into (kind, payload).

    kind is "OK", "FAIL" or "ERR"; payload is the remainder of the line
    ("FAIL"/"ERR" keep their reason so callers can attribute the failure to
    the command they sent). Unrecognized lines raise QTestError.
    """
    text = line.strip()
    if text.startswith("OK"):
        return "OK", text[2:].strip()
    for bad in ("FAIL", "ERR"):
        if text.startswith(bad):
            return bad, text[len(bad):].strip() or bad
    raise QTestError("unrecognized qtest response: %r" % line)


# ---------------------------------------------------------------------------
# Client
# ---------------------------------------------------------------------------


class QTestClient:
    """Line-oriented qtest client over an arbitrary transport."""

    def __init__(self, transport, default_timeout: float = 10.0):
        self._transport = transport
        self._default_timeout = default_timeout
        self._inbuf = bytearray()
        self._async_lines = []  # unsolicited "IRQ ..." lines, newest last

    def close(self) -> None:
        self._transport.close()

    # -- low-level ----------------------------------------------------------

    def _readline(self, timeout: float) -> str:
        while True:
            newline = self._inbuf.find(b"\n")
            if newline >= 0:
                line = bytes(self._inbuf[:newline])
                del self._inbuf[:newline + 1]
                return line.decode("ascii", errors="replace")
            try:
                chunk = self._transport.read(4096, timeout)
            except TimeoutError:
                raise QTestTimeout(
                    "no qtest response within %.3fs" % timeout
                ) from None
            if not chunk:
                raise QTestError("qtest transport closed")
            self._inbuf.extend(chunk)

    def command(self, *words, timeout: float = None) -> str:
        """Send one command, wait for its response, return the OK payload.

        Unsolicited "IRQ ..." lines are skipped (queued for inspection).
        FAIL/ERR answers raise QTestCommandError. A missing answer raises
        QTestTimeout, which classify_result() maps to TIMEOUT — never PASS.
        """
        timeout = self._default_timeout if timeout is None else timeout
        self._transport.write(encode_command(*words))
        while True:
            line = self._readline(timeout)
            if line.startswith("IRQ "):
                self._async_lines.append(line)
                continue
            kind, payload = parse_response(line)
            if kind == "OK":
                return payload
            raise QTestCommandError(
                " ".join(str(word) for word in words), payload
            )

    def async_lines(self):
        """Async notification lines seen so far (for evidence logging)."""
        return tuple(self._async_lines)

    # -- command wrappers ----------------------------------------------------

    def set_irq_in(self, qom_path: str, name: str, num: int, level: int,
                   timeout: float = None) -> None:
        """Forcibly set one input GPIO of a QOM object to `level`.

        Raises QTestCommandError if the path does not resolve ("FAIL Unknown
        device") or the input is not implemented. Callers must report that
        instead of guessing alternative paths.
        """
        self.command(
            "set_irq_in", qom_path, name, int(num), int(level),
            timeout=timeout,
        )

    def assert_irq_input_exists(self, qom_path: str, num: int,
                                timeout: float = None) -> None:
        """Confirm a CPU IRQ input path exists before relying on it.

        Per the T10 card, the object path must be confirmed up front and a
        failure must never be answered by guessing another path. The probe
        uses set_irq_in with the neutral level 0 (deasserted), so on success
        it leaves the line in the idle state; on "FAIL Unknown device" the
        error propagates and the case must stop.
        """
        self.set_irq_in(
            qom_path, IRQ_GPIO_NAME_UNNAMED, num, 0, timeout=timeout
        )

    def raise_irq(self, qom_path: str, num: int, timeout: float = None) -> None:
        """Assert one unnamed CPU IRQ input (level 1)."""
        self.set_irq_in(qom_path, IRQ_GPIO_NAME_UNNAMED, num, 1, timeout=timeout)

    def lower_irq(self, qom_path: str, num: int, timeout: float = None) -> None:
        """Deassert one unnamed CPU IRQ input (level 0)."""
        self.set_irq_in(qom_path, IRQ_GPIO_NAME_UNNAMED, num, 0, timeout=timeout)

    def read_memory(self, addr: int, size: int, timeout: float = None) -> int:
        """qtest 'readl' style host-endian read, returned as an int."""
        payload = self.command("readl", "0x%x" % addr, timeout=timeout)
        return int(payload, 0)
