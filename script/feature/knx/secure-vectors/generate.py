#!/usr/bin/env python3
"""Generates the KNX Secure vectors the C++ tests pin SECURE_WRAPPER, TIMER_NOTIFY and Data Secure to.

The vectors come out of xknx's own code paths - the wrapper encryption SecureGroup uses,
SecureSequenceTimer.send_timer_notify, and DataSecure's securing of a cEMI frame - driven with fixed inputs in place
of a clock and a random generator. They therefore record what the interoperability peer really puts on the wire,
rather than what this project's reading of the specification says it should.

Run it by hand with the pinned xknx virtual environment, and review the diff before committing the output:

    output/interop/xknx-venv/bin/python script/feature/knx/secure-vectors/generate.py

It writes documentation/features/knx/conformance/secure-routing-vectors.tsv and data-secure-vectors.tsv.
"""

from __future__ import annotations

import asyncio
from pathlib import Path
import sys

from xknx.__version__ import __version__ as xknx_version
from xknx.cemi import CEMIFrame, CEMILData, CEMIMessageCode
from xknx.dpt import DPTArray, DPTBinary
from xknx.io.const import XKNX_SERIAL_NUMBER
from xknx.io.ip_secure import SecureSequenceTimer, _IPSecureTransportLayer
from xknx.knxip import KNXIPFrame, RoutingBusy, RoutingIndication, RoutingLostMessage
from xknx.secure.data_secure import DataSecure
from xknx.secure.data_secure_asdu import SecurityAlgorithmIdentifier, SecurityALService, SecurityControlField
from xknx.telegram import GroupAddress, IndividualAddress, Telegram
from xknx.telegram.apci import APCI, GroupValueWrite

PINNED_XKNX = "3.20.0"
REPOSITORY = Path(__file__).resolve().parents[4]
OUTPUT = REPOSITORY / "documentation/features/knx/conformance/secure-routing-vectors.tsv"
DATA_SECURE_OUTPUT = REPOSITORY / "documentation/features/knx/conformance/data-secure-vectors.tsv"

# The backbone key of the vendored keyring.knxkeys, and the key of the KNX AN159 example.
KEYRING_KEY = bytes.fromhex("96f034fccf510760cbd63da0f70d4a9d")
AN159_KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
# The first group key of the vendored DataSecure_usb.knxkeys.
GROUP_KEY = bytes.fromhex("d2effbdc88eb5c9a57a73b0aad64996f")
# The cEMI frame of the AN159 routing example: an L_Data.ind group value write.
AN159_CEMI = bytes.fromhex("2900bcd011590ade010081")
MAX_TIMER = (1 << 48) - 1

# Data Secure cases: algorithm, key, sequence number, source, destination, APDU. A fourteen-octet value makes a plain
# frame of fifteen data octets - the most a standard frame carries - so its secured form has to be an extended frame.
DATA_SECURE_CASES = [
    ("authenticated_encryption", GROUP_KEY, 1, "1.1.5", GroupAddress("1/2/3"), GroupValueWrite(DPTBinary(1))),
    ("authentication_only", GROUP_KEY, 0x0123456789AB, "1.1.5", GroupAddress("1/2/3"), GroupValueWrite(DPTBinary(0))),
    ("authenticated_encryption", AN159_KEY, MAX_TIMER, "1.0.1", GroupAddress("31/7/255"), GroupValueWrite(DPTArray(b"Data Secure 14"))),
    ("authentication_only", GROUP_KEY, 1_000_000, "1.1.5", GroupAddress("2/0/1"), GroupValueWrite(DPTArray(bytes(range(14))))),
    ("authenticated_encryption", GROUP_KEY, 42, "1.1.5", IndividualAddress("1.1.10"), GroupValueWrite(DPTArray(b"\x12\x34"))),
    ("authentication_only", GROUP_KEY, 43, "1.1.5", IndividualAddress("1.1.10"), GroupValueWrite(DPTBinary(1))),
]


class FixedWrapperLayer(_IPSecureTransportLayer):
    """xknx's wrapper encryption, with the timer value and message tag fixed instead of drawn."""

    __slots__ = ("_key", "_message_tag", "_timer_value", "session_id")

    def __init__(self, key: bytes, timer_value: int, message_tag: bytes) -> None:
        """Fix the key, timer value and message tag."""
        self._key = key
        self._timer_value = timer_value
        self._message_tag = message_tag
        self.session_id = 0

    def get_sequence_information(self) -> bytes:
        """Return the fixed timer value."""
        return self._timer_value.to_bytes(6, "big")

    def get_message_tag(self) -> bytes:
        """Return the fixed message tag."""
        return self._message_tag


class FixedClockTimer(SecureSequenceTimer):
    """xknx's routing timer with a monotonic clock that stands still at zero."""

    __slots__ = ()

    def _monotonic_ms(self) -> int:
        """Return a clock that never moves, so the timer value is the clock difference alone."""
        return 0


def wrapper_rows() -> list[tuple[str, ...]]:
    """Wrap routing frames the way SecureGroup does, and check each opens again through xknx."""
    cases = [
        (AN159_KEY, 0xC0C1C2C3C4C5, bytes.fromhex("affe"), RoutingIndication(raw_cemi=AN159_CEMI)),
        (KEYRING_KEY, 0, bytes.fromhex("0000"), RoutingIndication(raw_cemi=AN159_CEMI)),
        (KEYRING_KEY, 3_600_000, bytes.fromhex("1234"), RoutingBusy(wait_time=100)),
        (KEYRING_KEY, MAX_TIMER, bytes.fromhex("ffff"), RoutingLostMessage(lost_messages=3)),
    ]
    rows = []
    for key, timer_value, message_tag, body in cases:
        layer = FixedWrapperLayer(key, timer_value, message_tag)
        plain = KNXIPFrame.init_from_body(body).to_knx()
        wire = layer.encrypt_frame(KNXIPFrame.init_from_body(body)).to_knx()
        parsed, _ = KNXIPFrame.from_knx(wire)
        if layer.decrypt_frame(parsed).to_knx() != plain:
            raise AssertionError("xknx cannot open the wrapper it just sealed")
        rows.append(("wrapper", key.hex(), f"{timer_value:012x}", XKNX_SERIAL_NUMBER.hex(), message_tag.hex(), plain.hex(), wire.hex()))
    return rows


async def timer_notify_rows() -> list[tuple[str, ...]]:
    """Build TIMER_NOTIFYs the way SecureSequenceTimer does, and check each verifies through xknx."""
    cases = [
        (KEYRING_KEY, 0, XKNX_SERIAL_NUMBER, bytes.fromhex("0001")),
        (KEYRING_KEY, 3_600_000, bytes.fromhex("00fa12345678"), bytes.fromhex("1234")),
        (AN159_KEY, 0xC0C1C2C3C4C5, bytes.fromhex("00fa12345678"), bytes.fromhex("affe")),
        (KEYRING_KEY, MAX_TIMER, XKNX_SERIAL_NUMBER, bytes.fromhex("ffff")),
    ]
    rows = []
    for key, timer_value, serial_number, message_tag in cases:
        sent: list[KNXIPFrame] = []
        timer = FixedClockTimer(backbone_key=key, latency_ms=1000, transport_send=lambda frame, _addr: sent.append(frame))
        timer._clock_difference = timer_value  # noqa: SLF001 - fixes the timer value xknx reads
        timer.send_timer_notify(message_tag=message_tag, serial_number=serial_number)
        wire = sent[0].to_knx()
        parsed, _ = KNXIPFrame.from_knx(wire)
        timer.verify_timer_notify_mac(parsed.body)  # raises when the MAC does not verify
        rows.append(("timer_notify", key.hex(), f"{timer_value:012x}", serial_number.hex(), message_tag.hex(), "", wire.hex()))
    return rows


def security_control(name: str) -> SecurityControlField:
    """The security control field of S-A_Data under the named algorithm."""
    algorithm = (
        SecurityAlgorithmIdentifier.CCM_ENCRYPTION if name == "authenticated_encryption" else SecurityAlgorithmIdentifier.CCM_AUTHENTICATION
    )
    return SecurityControlField(algorithm=algorithm, service=SecurityALService.S_A_DATA, system_broadcast=False, tool_access=False)


def check_data_secure(key: bytes, secured: bytes, plain: bytes, sequence: int) -> None:
    """Open a secured frame through xknx, and check that it gives back the plain one."""
    data = CEMIFrame.from_knx(secured).data
    secure_apdu = data.payload
    address_fields = data.src_addr.to_knx() + data.dst_addr.to_knx()
    plain_apdu = secure_apdu.secured_data.get_plain_apdu(
        key=key, scf=secure_apdu.scf, address_fields_raw=address_fields, frame_flags=data.flags, tpci=data.tpci
    )
    if APCI.from_knx(plain_apdu).to_knx() != CEMIFrame.from_knx(plain).data.payload.to_knx():
        raise AssertionError("xknx cannot open the Data Secure APDU it just secured")
    if isinstance(data.dst_addr, GroupAddress):
        # The receive path a group telegram takes through xknx, sender table and all.
        receiver = DataSecure(
            group_key_table={data.dst_addr: key}, individual_address_table={data.src_addr: sequence - 1}, last_sequence_number_sending=1
        )
        opened = CEMIFrame(code=CEMIMessageCode.L_DATA_IND, data=receiver.received_cemi(data)).to_knx()
        if opened != plain:
            raise AssertionError("xknx's receive path does not give back the plain frame")


def data_secure_rows() -> list[tuple[str, ...]]:
    """Secure L_Data.ind frames the way DataSecure does, and check each opens again through xknx."""
    rows = []
    for name, key, sequence, source, destination, payload in DATA_SECURE_CASES:
        telegram = Telegram(destination_address=destination, source_address=IndividualAddress(source), payload=payload)
        plain = CEMIFrame(code=CEMIMessageCode.L_DATA_IND, data=CEMILData.init_from_telegram(telegram)).to_knx()
        sender = DataSecure(group_key_table={}, individual_address_table={}, last_sequence_number_sending=sequence)
        secured_data = sender._secure_data_cemi(  # noqa: SLF001 - secures point-to-point frames too, for the codec vectors
            key=key, scf=security_control(name), cemi_data=CEMILData.init_from_telegram(telegram)
        )
        secured = CEMIFrame(code=CEMIMessageCode.L_DATA_IND, data=secured_data).to_knx()
        check_data_secure(key, secured, plain, sequence)
        rows.append(("data_secure", name, key.hex(), f"{sequence:012x}", plain.hex(), secured.hex()))
    return rows


def write_table(path: Path, header: list[str], rows: list[tuple[str, ...]]) -> None:
    """Write one vector table."""
    lines = header + ["\t".join(row) for row in rows]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {len(rows)} vectors to {path.relative_to(REPOSITORY)}")


def main() -> int:
    """Write the vectors, refusing to run against an xknx other than the pinned one."""
    if xknx_version != PINNED_XKNX:
        print(f"xknx {xknx_version} is installed; the vectors are pinned to {PINNED_XKNX}", file=sys.stderr)
        return 1
    write_table(
        OUTPUT,
        [
            f"# KNX IP Secure routing vectors from xknx {xknx_version}, written by script/feature/knx/secure-vectors/generate.py.",
            "# Regenerate rather than edit, and review the diff. Octets are lower-case hexadecimal.",
            "kind\tkey\ttimer_value\tserial_number\tmessage_tag\tplain\twire",
        ],
        wrapper_rows() + asyncio.run(timer_notify_rows()),
    )
    write_table(
        DATA_SECURE_OUTPUT,
        [
            f"# KNX Data Secure vectors from xknx {xknx_version}, written by script/feature/knx/secure-vectors/generate.py.",
            "# Regenerate rather than edit, and review the diff. Octets are lower-case hexadecimal; frames are cEMI L_Data.ind.",
            "kind\talgorithm\tkey\tsequence\tplain\tsecured",
        ],
        data_secure_rows(),
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
