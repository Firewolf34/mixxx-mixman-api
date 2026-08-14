# MixMan Session Contract v3 Integration

Mixxx uses the breaking MixMan session contract v3 at `/api/v3`. It registers
as application `mixxx`, surface `rest_library`; the server issues the instance
identity, capabilities, resume token, priority, and playback authority. Mixxx
never submits legacy client identity, role, tier, priority, or capability
fields.

## Authentication and compatibility

Before registration, Mixxx reads `GET /config` and requires version 3, base
path `/api/v3`, and the exact `mixxx/rest_library` capability set. The returned
registration capabilities are checked again before session features are
enabled. In OIDC mode the configured bearer token needs the MixMan user access
and `session:playback:dj` scope. When the server is explicitly configured for
trusted-LAN authentication, an empty bearer token is intentionally omitted.
A `401` or `403` never triggers an anonymous retry.

Bearer tokens are stored only in QtKeychain. Existing plaintext preference
tokens are migrated once and then removed; failed secure-storage migration
fails closed. Tokens require HTTPS except on loopback development addresses,
and all REST endpoints, redirects, and authenticated requests are restricted to
the configured base origin. Auth-disabled HTTP remains available for an
explicitly trusted LAN deployment.

The optional Session ID preference selects a stable room. When blank, Mixxx
generates and remembers a `mixxx-<uuid>` ID before connecting. Registration is
attempted directly; `404` causes creation of that exact room followed by a
second registration. A create `409` is treated as a race or lost create
response and registration is retried.

The server-issued instance ID and rotating resume token are stored only in
QtKeychain, scoped to server, room, application, and surface. A rejected resume
is deleted and retried as a fresh instance. If secure storage is unavailable,
the current process remains usable but no plaintext resume fallback is made.
Clean shutdown and reconfiguration disconnect the instance; server expiry is
the fallback for abrupt termination.

## Playback authority and ordering

Mixxx retains the server-issued `lease_id` and positive generation. Candidate,
playback, and snapshot writes are serialized in this order and carry
`instance_id`, `lease_id`, and `lease_generation`. Renew and release carry the
same instance and lease with the field name `generation`.

Only one authoritative mutation is in flight. Rapid candidate, playback, and
snapshot changes coalesce to their newest queued state; each dispatch carries
a local sequence plus the current server-issued lease fence. Late completions
from an older sequence cannot release the current write. A stale or held lease
clears the local authority tuple, stops remote authoritative publication, and
enters standby reconciliation. Mixxx never pauses, unloads, or otherwise
changes local DJ audio because remote authority was lost.

Heartbeat timing comes from registration. Lease TTL, renew timing, and pause
grace come from the v3 contract. A final paused or loaded publication is sent
before the pause-grace release.

## Recommendation boundary

With MixMan defaults enabled, recommendations come only from the instance-bound
state route and authoritative mutation responses. Mixxx accepts the limited
candidate display projection and path steps; controller queue, pressure,
presence, intent graph, and event products are outside this surface.

Mixxx may request only `policy_refresh` from the shared actions route. It may
select only a currently advertised candidate, using the current playback lease,
`selection_origin: "authoritative_candidate"`, and
`allow_external_candidate: false`. Custom REST Library recommendation routes
remain available when MixMan defaults are disabled.

All JSON responses use a 15-second transfer timeout and a 4 MiB decompressed
body ceiling, enforced while streaming. Audio cache downloads reject an
oversized declared length before writing, enforce the configured limit across
chunked reads, and discard temporary files after short or failed writes.

## Catalog browser boundary

The separate REST Library pane reads MixMan's hydrated, cursor-paginated track
catalog on first activation and explicit refresh. Metadata stays in process
memory and a refresh replaces the displayed snapshot only after every page has
loaded successfully. Browsing, search, sort, and selection never download
audio or artwork.

Deck, preview, sampler, and AutoDJ actions download only the selected tracks to
the configured cache. Cache identity includes the normalized MixMan server URL
and remote track ID; ambiguous legacy ID-only cache entries are ignored and
left for normal pruning. Explicit deck loads take priority over ordered AutoDJ
batches, which take priority over recommendation prefetch. Replacing a
recommendation set cancels only prefetch ownership; a shared browser request
keeps the same deduplicated download alive. Catalog reads remain outside
session lease authority, while a materialized track's playback is observed by
the existing v3 session integration.

## Connection test

The optional v3 session-permission test deliberately creates a unique durable
test room, registers an instance, validates actual capabilities, reads state,
claims and releases playback authority, and disconnects. The instance is not
persisted, but the room and audit history remain until normal server retention
or operator cleanup. Cleanup failure is reported as a failed test.

Native OAuth/PKCE remains separate follow-up work.
