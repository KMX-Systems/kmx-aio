#!/usr/bin/env python3
"""Seeds a KNX fuzz corpus from the generated vectors, so fuzzing starts from frames real peers send.

    python3 source/fuzz/knx/seed_corpus.py <target> <corpus directory>

datagram seeds with every SECURE_WRAPPER and TIMER_NOTIFY datagram and every wrapped plain datagram of
secure-routing-vectors.tsv, and with the handshake frames of a KNX IP Secure session. reassembler seeds with those
datagrams run together into one stream, behind the octet the target reads its read size from. data_secure seeds with the
plain and secured cEMI frames of data-secure-vectors.tsv. Started by script/feature/knx/run-fuzz.sh.
"""

from __future__ import annotations

from pathlib import Path
import sys

CONFORMANCE = Path(__file__).resolve().parents[3] / "documentation/features/knx/conformance"

# A SESSION_REQUEST, a SESSION_RESPONSE and a SESSION_STATUS, as xknx's session fixture puts them on the wire.
SESSION_FRAMES = [
    "0610095100460802000000000000" + "0aa227b4fd7a32319ba9960ac036ce0e5c4507b5ae55161f1078b1dcfb3cb631",
    "0610095200380001" + "bdf099909923143ef0a5de0b3be3687bc5bd3cf5f9e6f901699cd870ec1ff824" + "a922505aaeb5f4a5e8ef0f73dc8d9c73",
    "0610095400080000",
]


def rows(name: str, columns: int) -> list[list[str]]:
    """Reads the rows of one vector table."""
    table = []
    for line in (CONFORMANCE / name).read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if not line.startswith("#") and len(fields) == columns and fields[0] != "kind":
            table.append(fields)
    return table


def datagrams() -> list[bytes]:
    """Every datagram the routing vectors and the session fixture hold."""
    seeds = [bytes.fromhex(frame) for frame in SESSION_FRAMES]
    for fields in rows("secure-routing-vectors.tsv", 7):
        seeds.append(bytes.fromhex(fields[6]))
        if fields[5]:
            seeds.append(bytes.fromhex(fields[5]))
    return seeds


def seeds_for(target: str) -> list[bytes]:
    """The seeds of one target."""
    if target == "datagram":
        return datagrams()
    if target == "reassembler":
        stream = b"".join(datagrams())
        return [bytes([cut]) + stream for cut in (0, 5, 13, 63)]
    if target == "data_secure":
        return [bytes.fromhex(fields[column]) for fields in rows("data-secure-vectors.tsv", 6) for column in (4, 5)]
    raise SystemExit(f"no vector seeds for fuzz target {target}")


def main() -> int:
    """Writes one file per seed into the corpus directory."""
    target, corpus = sys.argv[1], Path(sys.argv[2])
    corpus.mkdir(parents=True, exist_ok=True)
    seeds = seeds_for(target)
    for index, seed in enumerate(seeds):
        (corpus / f"seed-{index:02d}").write_bytes(seed)
    print(f"seeded {len(seeds)} inputs into {corpus}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
