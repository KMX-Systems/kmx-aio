# KNX Interoperability Matrix

Generated: 2026-09-09T20:45:51Z

| Profile | Peer and version | Transport | Result | Capture | Notes | Last updated |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| SEARCH / DESCRIPTION | loopback | UDP 3671 | passing |  | In-tree discovery profile validation | 2026-09-07T13:56:23Z |
| Tunnelling, no Secure | loopback | UDP | passing |  | In-tree tunnelling loopback lifecycle | 2026-09-07T13:56:23Z |
| Routing indication/control | loopback | UDP multicast | passing |  | In-tree routing codec pinned to 03/08/05 golden wire vectors | 2026-09-07T13:56:23Z |
| Client <-> in-tree server | loopback | injected UDP transport | passing |  | In-tree server lifecycle coverage | 2026-09-07T13:56:23Z |
| IP Secure | not exercised | UDP | skipped |  | No cryptography is implemented: the envelope is a placeholder on an unassigned service type and secure::provider is an unimplemented injection point. The vector suite pins that envelope, not a KNX Secure profile. | 2026-09-07T13:56:23Z |
| Data Secure | not exercised | UDP | skipped |  | No cryptography is implemented: the envelope is a placeholder on an unassigned service type and secure::provider is an unimplemented injection point. The vector suite pins that envelope, not a KNX Secure profile. | 2026-09-07T13:56:23Z |
