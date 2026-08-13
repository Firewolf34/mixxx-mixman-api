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

Only the newest pending playback/snapshot state is retained during rapid deck
changes. A stale or held lease clears the local authority tuple, stops remote
authoritative publication, and enters standby reconciliation. Mixxx never
pauses, unloads, or otherwise changes local DJ audio because remote authority
was lost.

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

## Connection test

The optional v3 session-permission test deliberately creates a unique durable
test room, registers an instance, validates actual capabilities, reads state,
claims and releases playback authority, and disconnects. The instance is not
persisted, but the room and audit history remain until normal server retention
or operator cleanup. Cleanup failure is reported as a failed test.

Native OAuth/PKCE and migration of the manually entered bearer token out of
tracked preferences remain separate follow-up work.
