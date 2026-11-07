# Skiff Synth para Ableton Move (Schwung)

Los motores de sonido de [Skiff](https://github.com/illuminus5/skiff) (illuminus5), el firmware groovebox para el
M-VAVE FM-1 basado en [Felucca](https://github.com/hugelton/Felucca) (Hügelton Instruments), convertidos en un
**sound generator de [Schwung](https://github.com/charlesvestal/schwung)** para Ableton Move.

> Independiente de M-VAVE, Hügelton Instruments, illuminus5, Ableton y el proyecto Schwung, y sin afiliación con
> ninguno de ellos. Skiff Synth es una versión modificada de Skiff (ver "Licencia").

## Qué hace

- **14 motores**, en el orden de Skiff: ANALOG, FM6, PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, PHYS, NOISE,
  SLICE, DRUM y DRUM-X.
- **Los presets de fábrica de Skiff, tal cual** (150 en esta versión), en una sola lista: "ANALOG SAW LEAD",
  "FM6 TINE EP"…
- **El sonido es el de Skiff, bit a bit.** El código de sonido de Skiff no se ha tocado. Los tests pasan su suite
  de regresión, que renderiza cada motor × cada preset y compara el hash con el de Skiff.
- Una instancia es una pista de Skiff. El Move pone el secuenciador y el Signal Chain, así que el secuenciador de
  64 pasos, la canción y la interfaz de Skiff no están.
- El MIDI entra por el MIDI IN de Skiff: notas, pitch bend, CC1, CC11, aftertouch y sustain, como en el FM-1.
- Se pueden usar **hasta 8 instancias a la vez**, independientes entre sí (ver "Cómo está hecho").
- Los samples de los motores SAMPLE, GRAIN y SLICE son los que trae Skiff: piano, flauta y saxo (CC0, Versilian
  Studios) y los breaks BREAK y FUNK (GPL). Cargar tus propios samples llega en la siguiente versión.

## Páginas de knobs

| Página | Knobs 1-8 |
| --- | --- |
| Skiff (raíz) | Los 8 parámetros del motor (EDIT 1 y EDIT 2 de Skiff). Los nombres cambian con el motor |
| Env | Attack, Decay, Sustain, Release, Env → Cutoff, Env → Pitch, Env → Shape, Level |
| LFO | Rate, Wave, → Pitch, → Cutoff, → Shape, → Amp, Fade, Phase |
| FX | Dist, Chorus, Delay, Reverb (envíos), Delay Time, Delay Feedback, Reverb Size, Reverb Type |
| Voice | Voice (POLY/MONO/LEG/UNI), Glide, Detune, Pan, Transpose, Glide Mode, Priority, Spread |

El preset se elige en el navegador de presets de la página raíz y el motor en su lista de parámetros. Al cambiar de
motor se carga su primer preset. Los nombres y rangos de cada parámetro son los de Skiff. El delay y la reverb son
propios de cada instancia.

## Compilar y probar

```sh
./scripts/test.sh      # suite de regresión de Skiff + tests del plugin en el ordenador
./scripts/build.sh     # dsp.so para el Move (aarch64) -> dist/skiff-module.tar.gz
./scripts/install.sh   # lo copia al Move por SSH (move.local)
```

`build.sh` usa `aarch64-linux-gnu-gcc` si lo tienes, el `cc` nativo en una máquina aarch64 o, si no, Docker
(`scripts/Dockerfile`). La CI compila y prueba en el runner arm64 de GitHub.

## Cómo está hecho

- `vendor/skiff/`: el código de Skiff sin cambios (commit en `UPSTREAM_COMMIT`), sus tests de regresión, los
  generadores de tablas y samples (`tools/`), los samples fuente (`assets/`) y los headers ya generados (`gen/`).
- `src/dsp/skiff_core.c`: incluye el código de sonido de Skiff igual que su propio build de tests
  (`tests/hostsim.c`) y ofrece init, MIDI, render, presets y parámetros de la pista 1.
- Skiff guarda todo su estado en variables `static`, así que da un solo sinte por proceso. Por eso
  `skiff_core.c` se compila **8 veces** (`-DSKC_ID=0..7`), cada copia con sus propios `static`, y `objcopy`
  deja locales todos los símbolos de cada copia menos sus puntos de entrada. Cada instancia del Move toma una copia
  libre. Una novena instancia no se crea.
- `src/dsp/skiff_plugin.c`: la API v2 de Schwung (`ui_hierarchy`, `chain_params`, presets y `state`).
- Coste medido: 8 instancias con 8 notas cada una usan el 2,8 % de un bloque de 128 muestras en un PC x86, y
  el 15,8 % en aarch64 **emulado** con qemu. En el Move hay que medirlo.

Regenerar los headers (solo si actualizas Skiff): `python3 vendor/skiff/tools/gen_tables.py vendor/skiff/gen/felucca_tables.h`,
y lo mismo con `gen_fm6_patches.py` → `felucca_fm6.h` y `gen_samples.py` → `felucca_samples.h`.

## Licencia

**GPL-3.0-only** (`LICENSE`), como Skiff y Felucca. Skiff Synth es una versión modificada de Skiff 2.2
(© 2026 illuminus5) y de Felucca (© 2026 Leo Kuroshita, Hügelton Instruments). Algunas partes conservan su licencia:
DaisySP y Rings (MIT), msfa de Dexed (Apache-2.0) y los samples de Versilian Studios (CC0). El detalle está en
`vendor/skiff/LICENSING.md` y `vendor/skiff/LICENSES/`, que también van dentro del paquete del módulo.

Tus propios samples no van nunca en este repositorio ni en sus releases.
