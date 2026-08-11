# MixMan Session Contract v2 Integration

Mixxx is the highest-priority playback client. Every session request identifies
the product with `client_kind: "mixxx"`, `source: "mixxx"`, and
`surface: "rest_library"`. This identity is separate from authorization: the
configured bearer token must still have the MixMan user scope and controller
role.

## Compatibility Handshake

Before creating a session, Mixxx reads `GET /config` and requires:

- `session_contract.version == 2`;
- `mixxx` in `client_kinds`;
- `mixxx` in `playback_capable_client_kinds`.

The Connection Test runs the same check between health and index probes. A
missing, legacy, or incompatible contract fails before Mixxx posts `/sessions`,
which prevents the old `role: "dj"` payload from reaching a v2 server.

## Playback Lease Sequence

Playback and candidate selection never race the lease claim:

1. Mixxx queues the latest playback/candidate mutation.
2. It posts `POST /sessions/{id}/playback-control/claim`.
3. It flushes queued mutations only after a successful response.
4. While a deck is playing, it renews through
   `POST /sessions/{id}/playback-control/renew` at the interval advertised by
   `/config`.
5. When playback becomes paused or loaded, it publishes that state and releases
   through `POST /sessions/{id}/playback-control/release` after the advertised
   pause grace period.

A release already in flight is allowed to finish before a new claim, avoiding a
release/play race. A `409` stops lease-backed writes and is surfaced in REST
Library diagnostics. Mixxx does not stop local deck audio merely because the
remote session lease was lost.

## Authoritative State

Mixxx publishes playback to `POST /sessions/{id}/playback` and keeps publishing
snapshots as non-authoritative compatibility telemetry. It reads candidates,
path, pressure, intents, queue, blocked state, revision, and
`playback_controller` from `GET /sessions/{id}.authoritative` and mutation
responses. MixMan owns the authoritative queue.

For a current authoritative candidate, Mixxx selects with
`selection_origin: "authoritative_candidate"`. A MixMan-backed reroll/search
track uses `selection_origin: "recommendation_reroll"` and
`allow_external_candidate: true`.

Policy, energy, color, BPM, and reroll steering use
`POST /sessions/{id}/actions` with `action_type: "policy_refresh"`. Policy
actions do not claim playback control.

## Playback States

- `playing`: a deck is actively playing;
- `paused`: active playback stopped while a current MixMan track is known;
- `loaded`: the DJ loaded a track without active playback;
- `idle`: no current MixMan track is known.

Native OAuth/PKCE remains separate follow-up work. Until implemented, bearer
token provisioning is operational configuration and must not be committed.
