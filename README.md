# DeckKit

ESP-IDF components shared by firmware that runs an Elgato Stream Deck from an ESP32-S3:
[ESPDeck](https://github.com/jangellx/ESPDeck), and the wireless bridge to Elgato's software
that's being built next.

| Component | What it is |
|---|---|
| `deckkit_deck` | The Stream Deck driver (USB host, every model with keys) and `KeyImage`, which draws the key images the firmware makes itself: text, QR codes and the pairing code, encoded as BMP or JPEG for the deck. |
| `deckkit_util` | Checks on text that comes from outside (`Text`), wrap-safe millisecond deadlines (`Timing`), and the X25519, HMAC and AES-GCM used for pairing and sessions (`Crypto`). |

The components are plain ESP-IDF and work with or without Arduino as a component.

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

To work on DeckKit and a project together, add `override_path` pointing at a local checkout
(`override_path: ../../DeckKit/components/deckkit_deck`, relative to the manifest), so the
project builds from the files on disk. Take it out before tagging a release.

## Tools

- `tools/text_test/run.sh` builds `Text.cpp` on the Mac with the address and
  undefined-behavior sanitizers and runs its tests.
- `tools/make_font.swift` regenerates `components/deckkit_deck/Font.h` from Inter.

## License

MIT. See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for the work DeckKit derives from.
