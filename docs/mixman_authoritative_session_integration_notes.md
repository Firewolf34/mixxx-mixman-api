# MixMan Authoritative Session Integration Notes

## Mixxx Authority Model

Mixxx is the DJ console and final playback authority. MixMan should treat Mixxx playback updates as the source of truth for what is loaded or playing, then rebuild candidates, path, and queue guidance from that state.

Mixxx now identifies session mutations as role `dj`, source `mixxx`, and surface `rest_library`.

## Implemented Mixxx Behavior

- Mixxx publishes playback to `POST /sessions/{id}/playback`.
- Mixxx keeps publishing the legacy snapshot as compatibility telemetry.
- Mixxx reads authoritative candidates and path from `GET /sessions/{id}` and mutation responses.
- Mixxx treats `authoritative.queue` as MixMan-owned and does not locally manage it.
- Mixxx claims control in the background before DJ-sensitive mutations and reports conflicts as diagnostics.
- Mixxx posts `POST /sessions/{id}/candidates/{track_id}/select` when the DJ loads a candidate or reroll result.

## Contract Gaps For MixMan

### Candidate Selection For DJ Rerolls

Mixxx can show reroll/search candidates from the recommendation API when the DJ wants alternatives outside the current Sector Jump plan. These tracks may not be present in `authoritative.candidates`.

Current risk: `POST /sessions/{id}/candidates/{track_id}/select` can return `409` if the track is not in the latest authoritative candidate set. Mixxx treats this as non-fatal and will still publish playback once the DJ loads or plays the track.

Preferred MixMan behavior: provide a sanctioned way for the DJ to express “this MixMan-backed track is my intended next track” even when it came from reroll/search results.

### Fuzzy Steering Payloads

The breaking-changes document says `POST /sessions/{id}/actions` supports pressure, crew, policy, and sector controls, but only documents pressure and sector examples.

Mixxx needs documented authoritative action fields for:

- Policy preset
- Target energy
- Target color
- Target BPM
- Fuzzy/reroll candidate refresh constraints

Until those fields are documented, Mixxx uses the existing recommendation path API for reroll/search mode and does not invent undocumented action payloads.

### Playback State Semantics

Mixxx publishes:

- `playing` when a deck is actively playing
- `loaded` when the DJ loads a deck without playback
- `idle` when no current track is known

MixMan should rebuild guidance from `playing` and `loaded` state. Table selection or reroll browsing alone is not playback state.

## UI Expectations

Mixxx surfaces authoritative diagnostics for controller, pressure, intents, beacons, blocked state, and revision. These are informational for the DJ; they are not queue-management controls.
