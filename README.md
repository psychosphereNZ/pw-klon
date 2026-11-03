# PW-Klon – Kompressor-VST3 (inspiriert vom Presswerk-Handbuch)

Eigenständige Implementierung mit JUCE 8: FF/FB/INT-Detektion, Soft Knee, Non Lin, Adapt-Release,
RMS-Window, Channel Link, M/S, DPR, Sidechain (Int:Ext, Delay/Look-ahead, Filter, Monitor),
Saturation (Pre/Post, Dynamics, Warmth), Parallel-Mix (Dry-HP, Expand), Soft Clip.
10 Werkspresets (Special Views).

## Bauen (Windows)
Voraussetzungen: Visual Studio 2022 (Workload "Desktop development with C++"), CMake, Git.

    cmake -B build
    cmake --build build --config Release

Ergebnis: `build/PWKlon_artefacts/Release/VST3/PW-Klon.vst3` -> nach `C:\Program Files\Common Files\VST3\` kopieren,
in FL Studio: Options > Manage plugins > Find more plugins.

## Automatisch über GitHub
Repository auf GitHub pushen -> Actions baut Windows- und macOS-VST3 (`.github/workflows/build.yml`),
Download unter Actions > letzter Lauf > Artifacts.
