# Third-party content attribution

This app bundles a small set of factory NAM amp captures, cabinet impulse
responses, and presets built from them. In-app names are generic style
descriptions; the tables below map each one to the real gear captured, its
author, source, and license.

## Amp captures (`content/models/`)

| Bundled file | Captured gear | Author | Source | License |
|---|---|---|---|---|
| `boutique chime.nam` | Bad Cat Mini Cat | Steffen Dangmann (guitarlum/VoLum) | https://github.com/guitarlum/VoLum | MIT |
| `boutique breakup.nam` | Sebago Texas Flood | Steffen Dangmann (guitarlum/VoLum) | https://github.com/guitarlum/VoLum | MIT |
| `brit crunch.nam` | Marshall JMP 2203 (1976) | Steffen Dangmann (guitarlum/VoLum) | https://github.com/guitarlum/VoLum | MIT |
| `brit master.nam` | Marshall 2204 (1982) | Steffen Dangmann (guitarlum/VoLum) | https://github.com/guitarlum/VoLum | MIT |
| `modern high gain.nam` | Diezel Herbert Mk1 | Steffen Dangmann (guitarlum/VoLum) | https://github.com/guitarlum/VoLum | MIT |
| `hot rod lead.nam` | Soldano SLO-100 | Steffen Dangmann (guitarlum/VoLum) | https://github.com/guitarlum/VoLum | MIT |
| `buzzsaw fuzz.nam` | BOSS HM-2 Waza Craft (pedal, distortion capture) | gianni-cappelletti (October Production Co.) | https://github.com/gianni-cappelletti/October-Production-Co | GPL-3.0-only |

## Cabinet impulse responses (`content/irs/`)

| Bundled file | Captured cab | Author | Source | License |
|---|---|---|---|---|
| `warm straight.wav` | "Gray Wolf" cabinet, SM57, straight-on mic position | gianni-cappelletti (October Production Co.) | https://github.com/gianni-cappelletti/October-Production-Co | GPL-3.0-only |
| `warm angled.wav` | "Gray Wolf" cabinet, SM57, angled mic position | gianni-cappelletti (October Production Co.) | https://github.com/gianni-cappelletti/October-Production-Co | GPL-3.0-only |
| `bright straight.wav` | "Silver Wolf" cabinet, SM57, straight-on mic position | gianni-cappelletti (October Production Co.) | https://github.com/gianni-cappelletti/October-Production-Co | GPL-3.0-only |
| `bright angled.wav` | "Silver Wolf" cabinet, SM57, angled mic position | gianni-cappelletti (October Production Co.) | https://github.com/gianni-cappelletti/October-Production-Co | GPL-3.0-only |

## Presets (`content/presets/`)

Every factory preset is built entirely from the models and IRs listed above;
no additional third-party content is used.

| Preset | Amp | Cab IR |
|---|---|---|
| `clean & shimmer` | `boutique chime.nam` | `warm straight.wav` |
| `blues breakup` | `boutique breakup.nam` | `warm angled.wav` |
| `classic crunch` | `brit crunch.nam` | `bright straight.wav` |
| `modern metal` | `modern high gain.nam` | `bright angled.wav` |
| `ambient` | `brit master.nam` | `warm straight.wav` |

## License texts

### MIT (guitarlum/VoLum)

Applies to: `boutique chime.nam`, `boutique breakup.nam`, `brit crunch.nam`,
`brit master.nam`, `modern high gain.nam`, `hot rod lead.nam`.

```
MIT License

Copyright (c) 2024-2026 Steffen Dangmann and VoLum contributors
Portions copyright (c) 2022-2025 Steven Atkinson and contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### GPL-3.0-only (gianni-cappelletti/October-Production-Co)

Applies to: `buzzsaw fuzz.nam`, `warm straight.wav`, `warm angled.wav`,
`bright straight.wav`, `bright angled.wav`.

Copyright 2024-2026 October Production Co. Licensed under the GNU General
Public License v3.0 only. Full license text:
https://github.com/gianni-cappelletti/October-Production-Co/blob/main/LICENSES/GPL-3.0-only.txt

Simple Guitar is itself licensed under GPL-3.0 (see the repository's own
`LICENSE` file), so this content shares the same license as the app it
ships in.
