# Privacy-Preserving Multi-Modal WSN for Real-Time Occupancy and Flow Analysis

**Team 5 — Distributed and Networked Embedded Systems (DNES/IoT)**

A wireless sensor network that estimates crowd density in campus spaces — mess, gym, badminton courts — without cameras. Three sensors (millimetre-wave radar, vibration, acoustic) feed into an STM32 Nucleo-F446RE, which computes a local *Congestion Index* and forwards only that scalar over the air. Raw waveforms never leave the node. The mote network uses TelosB devices running TinyOS, with TPSN for time sync and GEAR for multi-hop routing back to a base station that drives an LCD and relay output.

## Folder Structure

```
crowdy/
├── crowdy/         STM32 firmware (CubeMX + Keil MDK-ARM project)
│   ├── Core/         Application sources — main, ADC, USART, DMA, GPIO, IT handlers
│   │   ├── Inc/      Public headers
│   │   └── Src/      Implementation
│   ├── Drivers/      Vendor HAL and CMSIS for STM32F4xx
│   ├── MDK-ARM/      Keil µVision project, linker script, startup, build outputs
│   ├── crowdy.ioc    STM32CubeMX configuration
│   └── .mxproject    CubeMX generation manifest
│
├── tools/          Host-side utilities
│   └── monitor.py    Terminal dashboard for the live UART2 sensor stream (rich + pyserial)
│
├── notebooks/      Design and analysis notebooks
│   ├── 1_uml.ipynb       UML / system diagrams
│   └── wsn_flowchart.png Network data-flow diagram
│
├── docs/           Reports, datasheets, and reference material
│   ├── 01_Initial_Report.{docx,pdf}
│   ├── 02_Circuit_Diagram_Transparent.png
│   ├── 03_Nucleo User Manual.pdf
│   ├── 03_Pin Layout - STM32F446RE.jpeg
│   ├── 04_Final_Report_Draft.md
│   ├── HLK-LD2420 Protocol Document.pdf
│   └── HLK-LD2420-Product-Manual V1.2.pdf
│
├── .gitignore
└── README.md
```

## Quick Start

### Firmware (Keil µVision MDK-ARM)

1. Install Keil MDK-ARM (Community/Pro) with the **STM32F4xx device pack** (`Keil::STM32F4xx_DFP`) and the **ARM Compiler 6** toolchain.
2. Open the project by double-clicking [crowdy/MDK-ARM/crowdy.uvprojx](crowdy/MDK-ARM/crowdy.uvprojx) (or *File → Open Project…* inside µVision).
3. Connect the Nucleo-F446RE over USB. Confirm *Project → Options for Target → Debug* is set to **ST-Link Debugger**.
4. Build with **F7** (*Project → Build Target*).
5. Flash with **F8** (*Flash → Download*). The board resets and starts streaming the CSV CI feed on USART2 via the ST-Link VCP.

To regenerate the HAL/peripheral init code, open [crowdy/crowdy.ioc](crowdy/crowdy.ioc) in STM32CubeMX and re-export with the MDK-ARM toolchain selected.

### Terminal UI (host-side monitor)

With the Nucleo connected via ST-Link VCP:

```bash
# from the repo root
python -m venv .venv
source .venv/Scripts/activate     # Windows: .venv\Scripts\activate
pip install pyserial rich

python tools/monitor.py --port COM5            # Windows
python tools/monitor.py --port /dev/ttyACM0    # Linux / macOS
```

The dashboard shows live raw sensor values, per-sensor connection status, and the fused occupancy class (Empty / Low / Medium / High). Find the correct port under *Device Manager → Ports (COM & LPT)* on Windows, or with `ls /dev/tty.usbmodem*` on macOS / `dmesg | tail` on Linux.

Use `--replay <capture.log>` for offline playback of a saved UART capture, and `--baud <rate>` to override the default 115200.

## Documentation

The full system design, protocol choices (TPSN, GEAR), sensor fusion pipeline, and validation results are documented in [docs/04_Final_Report_Draft.md](docs/04_Final_Report_Draft.md).
