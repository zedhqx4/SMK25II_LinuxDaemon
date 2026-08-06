# M-VAVE SMK25II Bluetooth MIDI Bridge & Mackie Control Translator

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


## Running the Daemon
Run the compiled daemon directly from your terminal:
```
./smk25ii_bridge
```
Once running, the application will register a client named SMK25II-Bluetooth with dedicated ports (HW_Input, SMK25II-bt, DAW, SMK25II-bt-DAW, and HW_Output) ready to be patched into your audio/MIDI workflow graph.

## Installation as a System Daemon (Systemd)
To run the bridge automatically in the background as a persistent system service under your user session:

1.Copy the binary to a system path:

```
sudo cp smk25ii_bridge /usr/bin/
sudo chmod +x /usr/bin/smk25ii_bridge
```
2.Create the systemd service file:2
```
sudo nano /etc/systemd/system/smk25ii-bridge.service
```
3.Paste the following configuration (replace tu_usuario with your actual Linux username, as ALSA/PipeWire require user session access):

```
[Unit]
Description=SMK25II Bluetooth MIDI Bridge & Mackie Control Daemon
After=sound.target pipewire.target jack.target

[Service]
Type=simple
ExecStart=/usr/bin/smk25ii_bridge
Restart=always
RestartSec=2

[Install]
WantedBy=default.target
```

4.Enable and start the service:
```Bash
sudo systemctl daemon-reload
sudo systemctl enable smk25ii-bridge.service
sudo systemctl start smk25ii-bridge.service
```
Managing the Daemon
* Check status: sudo systemctl status smk25ii-bridge.service

* View live logs: journalctl -u smk25ii-bridge.service -f

* Stop service: sudo systemctl stop smk25ii-bridge.service

## How to use it in practice (DAW Setup)
I've tested it on Ardour, Reaper, and Bitwig.

For Ardour & Reaper: They handle device ports natively without needing extra virtual ports. You can just select the daemon's ports directly in your control surface settings.

For Bitwig: Since it can be a bit picky, you might want to load a virtual MIDI module first (e.g: sudo modprobe snd_virmidi midi_devs=4), add the controller as a Mackie MCU Pro, and route the daemon's output (SMK25ii-bt-DAW <> to the virtual port (e.g., VirMIDI 4-0 in my setup).

## Feedback & Notes:

Full Feedback: Once routed correctly, you get full feedback response—meaning the keyboard's lights and LEDs will respond to what's happening in your DAW.

Regular Notes: If you just want to play regular MIDI notes, connect SMK25ii-bt port directly to your DAW's standard MIDI input port.

Presets: Every factory preset works out of the box (except Preset 8 for GarageBand, and Preset 7 for Reason, which has different mapping i cant test outside linux).

It should autoconnect properly but in case it doesnt, this is the setup

* HW_Input Goes to the output node of the SMK25II.
* SMK25ii-bt-> Midi notes go into the regular midi input node of the DAW.
* SMK25ii-bt-DAW (Input) connects to the DAW output to RECEIVE MCP/MCU feedback (PAD LIGHTS!!!).
* SMK25ii-bt-DAW (output) connects to the DAW input to SEND MCP/MCU commands.
* HW_Output_MCP Goes into the SMK25II Input note of the SMK25II.

## NOTES

The comments in the source code and variable names are written in Spanish XD. I tried to make the source as clean and easy to follow as possible..... for myself.
