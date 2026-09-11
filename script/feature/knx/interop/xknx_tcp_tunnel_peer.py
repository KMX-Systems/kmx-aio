#!/usr/bin/env python3
"""An xknx TCP tunnelling client for the kmx-aio tunnelling interoperability tests: plain, KNX IP Secure, or Data Secure.

It opens a tunnel over TCP to the in-tree server on the loopback interface, sends a switch-on - by default to 1/2/3 - and
waits for the switch telegram the in-tree server answers with - by default a switch-on to 1/2/4. It exits 0 once the
answer has arrived and the tunnel is closed, 2 when the answer does not arrive in time, and 3 when a secure server's
description does not say that tunnelling requires KNX IP Secure. Every frame xknx handles is logged, and the log is the
capture the interoperability matrix records.

Given --keyring, the tunnel is KNX IP Secure. xknx reads the server's description over UDP, as it does before any secure
tunnel whose credentials come from a keyring, then opens a session with the credentials of the keyring's tunnel at
--tunnel.

Given --data-secure-keyring instead, the tunnel is plain and xknx applies KNX Data Secure to every group that keyring
holds a key for; --require-data-secure then counts the answer only when it arrived secured.

Started by script/feature/knx/interop/run-tcp-tunnelling-interop.sh, run-secure-tunnelling-interop.sh and
run-data-secure-interop.sh with the pinned xknx virtual environment.
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
from xknx.io.self_description import request_description
from xknx.telegram import GroupAddress, IndividualAddress, Telegram
from xknx.telegram.apci import GroupValueWrite

logger = logging.getLogger("peer")


def parse_arguments() -> argparse.Namespace:
    """Read the port, limits, keyrings and telegrams the run script passes."""
    parser = argparse.ArgumentParser(description="xknx KNXnet/IP TCP tunnelling client")
    parser.add_argument("--port", type=int, required=True, help="the in-tree server's TCP port")
    parser.add_argument("--timeout", type=float, default=40.0, help="seconds to wait for the in-tree server")
    parser.add_argument("--log", required=True, help="where the capture is written")
    parser.add_argument("--keyring", help="an ETS keyring: tunnel over KNX IP Secure with the credentials of one of its tunnels")
    parser.add_argument("--keyring-password", default="password", help="the keyring's password")
    parser.add_argument("--tunnel", default="1.0.1", help="the individual address of the keyring's tunnel to use")
    parser.add_argument("--data-secure-keyring", help="an ETS keyring: apply KNX Data Secure with its group keys over a plain tunnel")
    parser.add_argument("--data-secure-password", default="pwd", help="the Data Secure keyring's password")
    parser.add_argument("--request-group", default="1/2/3", help="the group the switch-on goes to")
    parser.add_argument("--answer-group", default="1/2/4", help="the group the in-tree server answers on")
    parser.add_argument("--answer-value", type=int, default=1, help="the switch value the in-tree server answers with")
    parser.add_argument("--require-data-secure", action="store_true", help="count the answer only when it arrived Data Secure")
    return parser.parse_args()


def connection(arguments: argparse.Namespace) -> ConnectionConfig:
    """A tunnel over TCP to the loopback interface, with no reconnect to hide a failure."""
    if arguments.keyring is not None:
        return ConnectionConfig(
            connection_type=ConnectionType.TUNNELING_TCP_SECURE,
            gateway_ip="127.0.0.1",
            gateway_port=arguments.port,
            individual_address=IndividualAddress(arguments.tunnel),
            secure_config=SecureConfig(knxkeys_file_path=arguments.keyring, knxkeys_password=arguments.keyring_password),
            auto_reconnect=False,
        )
    secure_config = None
    if arguments.data_secure_keyring is not None:
        secure_config = SecureConfig(knxkeys_file_path=arguments.data_secure_keyring, knxkeys_password=arguments.data_secure_password)
    return ConnectionConfig(
        connection_type=ConnectionType.TUNNELING_TCP,
        gateway_ip="127.0.0.1",
        gateway_port=arguments.port,
        secure_config=secure_config,
        auto_reconnect=False,
    )


async def tunnelling_secured(arguments: argparse.Namespace) -> bool:
    """Read the server's description, and whether it says that tunnelling requires KNX IP Secure."""
    descriptor = await request_description(gateway_ip="127.0.0.1", gateway_port=arguments.port)
    logger.info("description of 127.0.0.1:%d:\n%s", arguments.port, descriptor)
    return bool(descriptor.tunnelling_requires_secure)


async def exchange(xknx: XKNX, arguments: argparse.Namespace, answered: asyncio.Event) -> int:
    """Send the switch-on, then wait for the in-tree server's answer."""
    request_group = GroupAddress(arguments.request_group)
    await xknx.telegrams.put(Telegram(destination_address=request_group, payload=GroupValueWrite(DPTBinary(1))))
    await xknx.telegrams.join()
    logger.info("sent a switch-on to %s", request_group)
    try:
        await asyncio.wait_for(answered.wait(), timeout=arguments.timeout)
    except TimeoutError:
        logger.error("no switch %d to %s within %.0f s", arguments.answer_value, arguments.answer_group, arguments.timeout)
        return 2
    return 0


async def run(arguments: argparse.Namespace) -> int:
    """Connect, exchange one telegram each way, and disconnect."""
    answered = asyncio.Event()
    answer_group = GroupAddress(arguments.answer_group)

    def telegram_received(telegram: Telegram) -> None:
        payload = telegram.payload
        if (
            telegram.destination_address == answer_group
            and isinstance(payload, GroupValueWrite)
            and payload.value == DPTBinary(arguments.answer_value)
            and (telegram.data_secure or not arguments.require_data_secure)
        ):
            logger.info("received from the in-tree server, data_secure=%s: %s", telegram.data_secure, telegram)
            answered.set()

    if arguments.keyring is not None and not await tunnelling_secured(arguments):
        logger.error("the server's description does not say that tunnelling requires KNX IP Secure")
        return 3

    xknx = XKNX(connection_config=connection(arguments))
    xknx.telegram_queue.register_telegram_received_cb(telegram_received)
    await xknx.start()
    secured = "over KNX IP Secure " if arguments.keyring is not None else ""
    logger.info("xknx %s tunnelling %sover TCP to 127.0.0.1:%d", xknx_version, secured, arguments.port)
    try:
        return await exchange(xknx, arguments, answered)
    finally:
        await xknx.stop()
        logger.info("tunnel closed")


def main() -> int:
    """Log everything xknx does, including raw frames, and run the peer."""
    arguments = parse_arguments()
    logging.basicConfig(filename=arguments.log, level=logging.DEBUG, format="%(asctime)s %(name)s %(levelname)s %(message)s")
    return asyncio.run(run(arguments))


if __name__ == "__main__":
    sys.exit(main())
