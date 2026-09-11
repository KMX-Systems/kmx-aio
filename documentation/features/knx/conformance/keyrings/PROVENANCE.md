# ETS keyring fixtures

These five `.knxkeys` files are ETS 5.7 keyring exports, copied unmodified from the xknx project so the keyring
reader is checked against documents ETS actually wrote rather than against ones written to match the reader.

| File | Password | What it exercises |
| :--- | :--- | :--- |
| `keyring.knxkeys` | `pwd` | Full project export: backbone, ten interfaces, a Data Secure group key, three devices; passwords padded to two blocks |
| `testcase.knxkeys` | `password` | Byte order mark, a 48-bit device sequence number, passwords padded to two blocks |
| `special_chars_secure_tunnel.knxkeys` | `test` | UTF-8 in a signed attribute, CR LF line endings, no backbone |
| `DataSecure_only_one_interface.knxkeys` | `test` | Interface without user id or passwords, group senders, three group keys |
| `DataSecure_usb.knxkeys` | `test` | A USB interface |

## Source

- Project: xknx, <https://github.com/XKNX/xknx>
- Release tag: `3.20.0`, commit `e68c024e561dbc486c55dc15d401d070250bfba5`
- Path: `test/secure_tests/resources/`
- Licence: MIT; the licence text is in `LICENSE-xknx` beside this file, as the licence requires.

## How the expected values were established

The values asserted in `source/library-test/src/kmx/aio/knx/keyring_test.cpp` come from two independent
sources that agree:

1. the assertions in xknx's own `test/secure_tests/keyring_test.py` (backbone keys, tunnel passwords, the
   `commissioning` management password and `authenticationcode` authentication);
2. a separate recomputation in Python with `hashlib`, `xml.sax` and `cryptography`, written from the format
   description rather than from xknx's code, which also produced the values xknx does not assert (group keys,
   tool keys, device passwords) and confirmed every signature.

The synthetic ciphertexts in the negative tests were produced the same way, under password `test` and
`Created="2026-01-01T00:00:00"`.
