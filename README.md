# SMK25II Bluetooth MIDI Bridge & Mackie Control Translator

A robust, bidirectional ALSA sequencer daemon written in C designed to bridge, translate, and route MIDI streams between a physical Bluetooth keyboard (specifically tailored for devices like the SMK25II) and a DAW (Digital Audio Workstation) under Linux (compatible with PipeWire/JACK via ALSA sequencer architecture).

## Features

* **Dual-Path Architecture (Parallel Processing):**
  * **Path 1 (Hardware $\rightarrow$ DAW):** Automatically captures raw input from the physical keyboard, forwards standard musical notes cleanly, and translates proprietary SysEx messages into high-resolution 14-bit pitch bend or Mackie Control (MCU) protocol commands.
  * **Path 2 (DAW $\rightarrow$ Hardware):** Implements an Omni-Pass feedback loop, transparently routing feedback and return streams from the DAW back to the physical hardware.
* **Dynamic Hot-Plugging & Auto-Connection:** Continuously scans the ALSA subsystem to locate, bind, and establish bidirectional subscriptions with the target MIDI controller automatically, handling disconnections gracefully.
* **Node Graph Integration:** Configures explicit ALSA port capabilities and flags (`SND_SEQ_PORT_TYPE_HARDWARE`, `SND_SEQ_PORT_TYPE_SYNTH`, and application types) so that ports render correctly in graphical patch bays like `qpwgraph` and PipeWire patchers.
* **Non-Blocking Asynchronous Loop:** Utilizes ALSA's non-coupled duplex modes and `poll()` system calls for low-latency, lightweight performance.

## Requirements

* **OS:** Linux (Arch Linux / generic distributions running PipeWire-ALSA or ALSA sequencer)
* **Libraries:** `libasound2` (ALSA development libraries)
* **Compiler:** GCC or any standard C99 compatible compiler

## Compilation

Compile the source code using `gcc` and link against the ALSA library:

```bash
gcc -O3 smk25ii_bridge.c -lasound -o smk25ii_bridge
```


## Usage
Run the compiled daemon directly from your terminal:
```bash
Bash
./smk25ii_bridge
```
Once running, the application will register a client named SMK25II-Bluetooth with dedicated ports (HW_Input, SMK25II-bt, DAW, SMK25II-bt-DAW, and HW_Output) ready to be patched into your audio/MIDI workflow graph.
