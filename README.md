# DeckKit

ESP-IDF components shared by firmware that runs an Elgato Stream Deck from an ESP32-S3:
[ESPDeck](https://github.com/jangellx/ESPDeck), and the wireless bridge to Elgato's software
that's being built next.

| Component | What it is |
|---|---|
| `deckkit_deck` | The Stream Deck driver (USB host, every model with keys) and `KeyImage`, which draws the key images the firmware makes itself: text, QR codes and the pairing code, encoded as BMP or JPEG for the deck. Experimental: Mirabox's decks and their rebadges (Ajazz, Fifine, upHere, Mars Gaming, Redragon and others), in `MiraboxProtocol.h`. |
| `deckkit_util` | Checks on text that comes from outside (`Text`), wrap-safe millisecond deadlines (`Timing`), and the X25519, HMAC and AES-GCM used for pairing and sessions (`Crypto`). |
| `deckkit_wifi` | Wi-Fi on plain ESP-IDF: a station that joins the saved network and saves a new one only once it works (`WiFiStation`), and Improv Wi-Fi over serial for setting it from ESP Web Tools, improv-wifi.com in Chrome, or ESPDeck Bridge's USB Setup (`ImprovSerial`). Needs `deckkit_util`. The network is kept in plain NVS for now. |

The components are plain ESP-IDF. `deckkit_deck` and `deckkit_util` work with or without Arduino as a component; `deckkit_wifi` drives Wi-Fi itself, so it's for plain ESP-IDF projects.

## Using it

Add the components to a project's `idf_component.yml`, pinned to a tag:

```yaml
dependencies:
  deckkit_deck:
    git: https://github.com/jangellx/DeckKit.git
    path: components/deckkit_deck
    version: v1.0.0
  deckkit_util:
    git: https://github.com/jangellx/DeckKit.git
    path: components/deckkit_util
    version: v1.0.0
```

To work on DeckKit and a project together, switch the project's DeckKit entries to a local
`path:` (for example `path: ../../DeckKit/components/deckkit_deck`, relative to the manifest)
while you work, without committing it, then tag DeckKit and point `version:` at the new tag.
The component manager's `override_path` would be neater, but PlatformIO doesn't reliably
honor it.

## Tools

- `tools/text_test/run.sh` builds `Text.cpp` on the Mac with the address and
  undefined-behavior sanitizers and runs its tests.
- `tools/mirabox_test/run.sh` builds `MiraboxProtocol.cpp` on the Mac the same way and checks
  the Mirabox commands, image pieces, key reports, model table and report descriptor parsing.
- `tools/improv_test/run.sh` builds `ImprovProtocol.cpp` the same way and checks it against packets built as ESP Web Tools builds them.
- `tools/make_font.swift` regenerates `components/deckkit_deck/Font.h` from Inter.

## License

MIT. See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for the work DeckKit derives from.
