#!/usr/bin/env python3
"""An xknx secure routing peer for the kmx-aio KNX IP Secure routing and KNX Data Secure interoperability tests.

It joins the routing group on the loopback interface with the backbone key of an ETS keyring, waits for the switch
telegram the in-tree router sends - by default a switch-on to 1/2/3 - and answers with a switch telegram of its own - by
default a switch-on to 1/2/4. It exits 0 once it has received and answered, and 2 when the telegram does not arrive in
time. Every datagram xknx handles is logged, and the log is the capture the interoperability matrix records.

xknx applies KNX Data Secure to every group the keyring holds a key for, so pointing both telegrams at such a group -
1/1/1 in keyring.knxkeys - runs Data Secure inside the secure routing; --require-data-secure then counts the in-tree
router's telegram only when it arrived secured.

Started by script/feature/knx/interop/run-secure-routing-interop.sh and run-data-secure-interop.sh with the pinned xknx
virtual environment.
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import sys

from xknx import XKNX
from xknx.__version__ import __version__ as xknx_version
from xknx.dpt import DPTBinary
from xknx.io import ConnectionConfig, ConnectionType, SecureConfig
from xknx.telegram import GroupAddress, Telegram
from xknx.telegram.apci import GroupValueWrite

# Answers are repeated for a few seconds, so that a router whose timer is still converging on this one's gets
# more than one chance at them.
ANSWERS = 10
ANSWER_INTERVAL_S = 0.5
logger = logging.getLogger("peer")


def parse_arguments() -> argparse.Namespace:
    """Read the port, keyring, telegrams and limits the run script passes."""
    parser = argparse.ArgumentParser(description="xknx KNX IP Secure routing peer")
    parser.add_argument("--port", type=int, required=True, help="the routing multicast port")
    parser.add_argument("--keyring", required=True, help="the ETS keyring holding the backbone key")
    parser.add_argument("--password", required=True, help="the keyring password")
    parser.add_argument("--timeout", type=float, default=40.0, help="seconds to wait for the in-tree router")
    parser.add_argument("--log", required=True, help="where the capture is written")
    parser.add_argument("--individual-address", default="1.1.250", help="this peer's individual address, the source of its telegrams")
    parser.add_argument("--expected-group", default="1/2/3", help="the group the in-tree router sends to")
    parser.add_argument("--expected-value", type=int, default=1, help="the switch value the in-tree router sends")
    parser.add_argument("--answer-group", default="1/2/4", help="the group this peer answers on")
    parser.add_argument("--answer-value", type=int, default=1, help="the switch value this peer answers with")
    parser.add_argument("--require-data-secure", action="store_true", help="count the router's telegram only when it arrived Data Secure")
    return parser.parse_args()


def connection(arguments: argparse.Namespace) -> ConnectionConfig:
    """Secure routing on the loopback interface, keyed from the keyring."""
    return ConnectionConfig(
        connection_type=ConnectionType.ROUTING_SECURE,
        individual_address=arguments.individual_address,
        local_ip="127.0.0.1",
        multicast_group="224.0.23.12",
        multicast_port=arguments.port,
        secure_config=SecureConfig(knxkeys_file_path=arguments.keyring, knxkeys_password=arguments.password),
    )


async def exchange(xknx: XKNX, arguments: argparse.Namespace, received: asyncio.Event) -> int:
    """Wait for the in-tree router's telegram, then answer it."""
    try:
        await asyncio.wait_for(received.wait(), timeout=arguments.timeout)
    except TimeoutError:
        logger.error("no switch %d to %s within %.0f s", arguments.expected_value, arguments.expected_group, arguments.timeout)
        return 2
    answer_group = GroupAddress(arguments.answer_group)
    for _ in range(ANSWERS):
        await xknx.telegrams.put(Telegram(destination_address=answer_group, payload=GroupValueWrite(DPTBinary(arguments.answer_value))))
        await asyncio.sleep(ANSWER_INTERVAL_S)
    await xknx.telegrams.join()
    logger.info("answered %d times to %s", ANSWERS, answer_group)
    return 0


async def run(arguments: argparse.Namespace) -> int:
    """Connect, exchange one telegram each way, and disconnect."""
    received = asyncio.Event()
    expected_group = GroupAddress(arguments.expected_group)

    def telegram_received(telegram: Telegram) -> None:
        payload = telegram.payload
        if (
            telegram.destination_address == expected_group
            and isinstance(payload, GroupValueWrite)
            and payload.value == DPTBinary(arguments.expected_value)
            and (telegram.data_secure or not arguments.require_data_secure)
        ):
            logger.info("received from the in-tree router, data_secure=%s: %s", telegram.data_secure, telegram)
            received.set()

    xknx = XKNX(connection_config=connection(arguments))
    xknx.telegram_queue.register_telegram_received_cb(telegram_received)
    await xknx.start()
    logger.info("xknx %s connected to 224.0.23.12:%d over KNX IP Secure routing", xknx_version, arguments.port)
    try:
        return await exchange(xknx, arguments, received)
    finally:
        await xknx.stop()


def main() -> int:
    """Log everything xknx does, including raw datagrams, and run the peer."""
    arguments = parse_arguments()
    logging.basicConfig(filename=arguments.log, level=logging.DEBUG, format="%(asctime)s %(name)s %(levelname)s %(message)s")
    return asyncio.run(run(arguments))


if __name__ == "__main__":
    sys.exit(main())
