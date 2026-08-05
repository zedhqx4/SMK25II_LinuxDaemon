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
```
./smk25ii_bridge
```
Once running, the application will register a client named SMK25II-Bluetooth with dedicated ports (HW_Input, SMK25II-bt, DAW, SMK25II-bt-DAW, and HW_Output) ready to be patched into your audio/MIDI workflow graph.

## Installation as a System Daemon (Systemd)
To run the bridge automatically in the background as a persistent system service under your user session:

1.Copy the binary to a system path:

```
sudo cp smk25ii_bridge /usr/local/bin/
sudo chmod +x /usr/local/bin/smk25ii_bridge
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
User=tu_usuario
ExecStart=/usr/local/bin/smk25ii_bridge
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
* **Check status: sudo systemctl status smk25ii-bridge.service

* **View live logs: journalctl -u smk25ii-bridge.service -f

* **Stop service: sudo systemctl stop smk25ii-bridge.service
