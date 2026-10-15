# Guitar Scale Tune

Autotune for guitar solos. Detects the note you play and snaps it to the selected key and scale
(23 scales: major, minor, pentatonics, blues, all modes, harmonic/melodic minor, exotic scales and more).

**Controls:** Key, Scale, Retune Speed (0 ms = hard snap), Correction Amount, Mix.

## Build
Push to GitHub. Actions builds VST3 + Standalone for Windows, macOS and Linux
(Actions tab, then the run, then Artifacts). Tag `v1.0.0` to publish a Release.

Local build: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`

## Install
- Windows: copy the `.vst3` folder to `C:\Program Files\Common Files\VST3`
- macOS: `~/Library/Audio/Plug-Ins/VST3` then `xattr -dr com.apple.quarantine "Guitar Scale Tune.vst3"` (unsigned build)
- Linux: `~/.vst3`

## Notes
- Monophonic: single-note lines and solos. Chords are not supported.
- Latency about 15 ms (reported to the DAW). Use a clean, DI or lightly overdriven signal for best tracking.
- Webpage: `docs/index.html` (enable GitHub Pages on the `docs` folder).
- JUCE is GPLv3 / commercial: a closed-source release needs a JUCE licence.
