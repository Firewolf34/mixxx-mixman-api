# MixMan Authoritative Session Integration Notes

## Mixxx Authority Model

Mixxx is the DJ console and final playback authority. MixMan should treat Mixxx playback updates as the source of truth for what is loaded or playing, then rebuild candidates, path, and queue guidance from that state.

Mixxx identifies session mutations as role `dj`, source `mixxx`, and surface `rest_library`.

## Implemented Mixxx Behavior

- Mixxx publishes playback to `POST /sessions/{id}/playback`.
- Mixxx keeps publishing the legacy snapshot as compatibility telemetry.
- Mixxx reads authoritative candidates and path from `GET /sessions/{id}` and mutation responses.
- Mixxx treats `authoritative.queue` as MixMan-owned and does not locally manage it.
- Mixxx claims control in the background before DJ-sensitive mutations and reports conflicts as diagnostics.
- Mixxx posts `POST /sessions/{id}/candidates/{track_id}/select` when the DJ loads a candidate or reroll result.
- Mixxx posts `POST /sessions/{id}/actions` with `action_type: policy_refresh` for policy, energy, color, BPM, and reroll steering.

## Candidate Selection

For tracks in the current authoritative candidate set, Mixxx sends:

```json
{
  "selection_origin": "authoritative_candidate"
}
```

For DJ reroll/search tracks that came from MixMan recommendation results but are not in `authoritative.candidates`, Mixxx sends:

```json
{
  "selection_origin": "recommendation_reroll",
  "allow_external_candidate": true,
  "metadata": {
    "reason": "DJ loaded reroll result"
  }
}
```

MixMan validates the production track ID, commits it as the intended next track, and rebuilds authoritative queue, path, and candidates from that selected anchor. Controller conflicts may still return `409`; Mixxx reports those as diagnostics/backoff.

## Fuzzy Steering Payloads

Mixxx uses `POST /sessions/{id}/actions` for current path future targets:

```json
{
  "action_type": "policy_refresh",
  "policy_preset": "explore",
  "target_color": "#33AAFF",
  "target_energy": 0.72,
  "target_bpm": 128,
  "reroll_constraints": {
    "mode": "fuzzy",
    "limit": 8
  }
}
```

Mixxx only includes enabled target fields. Policy refresh responses are parsed as authoritative session state and can update candidates, path, queue diagnostics, controller diagnostics, pressure, intents, blocked state, and revision.

## Playback State Semantics

Mixxx publishes:

- `playing` when a deck is actively playing
- `loaded` when the DJ loads a deck without playback
- `idle` when no current track is known

MixMan should rebuild guidance from `playing` and `loaded` state. Table selection or reroll browsing alone is not playback state.

## UI Expectations

Mixxx surfaces authoritative diagnostics for controller, pressure, intents, beacons, blocked state, and revision. These are informational for the DJ; they are not queue-management controls.
