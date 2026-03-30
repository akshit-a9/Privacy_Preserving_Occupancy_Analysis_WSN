# Privacy-Preserving Multi-Modal WSN for Real-Time Occupancy and Flow Analysis

**Team 5 — Distributed and Networked Embedded Systems (DNES/IoT)**

## Description

This project implements a Wireless Sensor Network (WSN) deployed across campus public spaces (mess, gym, badminton courts) to estimate crowd density and pedestrian flow without the use of cameras. Three sensor modalities — millimeter-wave radar, vibration/shock, and acoustic — are fused locally on an STM32 Nucleo microcontroller to produce a weighted **Congestion Index (CI)**. Only the CI is transmitted wirelessly to a base station via a TelosB mote network, ensuring user privacy since no raw sensor data ever leaves the sensing node. At the base station, the CI is logged, displayed on an LCD, and used to trigger a relay alert when congestion exceeds a configured threshold. The network uses **TPSN** for clock synchronisation and **GEAR** for geographic, energy-aware multi-hop routing.

## Hardware

| Component | Quantity | Role |
|-----------|----------|------|
| TelosB mote | 4 | 1 base station, 1 primary sensing mote, 2 dummy motes |
| STM32 Nucleo-F446RE | 1 | Sensor interface and CI computation; connected to sensing TelosB via UART |
| Millimeter-wave radar module | 1 | People-counting / motion detection |
| Vibration / shock sensor | 1 | Footfall proxy |
| Acoustic / sound sensor | 1 | Ambient noise proxy |
| LCD screen module | 1 | CI display at base station |
| Relay module | 1 | Congestion alert output |

## Repository Structure

```
/
├── .gitignore              # Ignores build artifacts, OS files, etc.
├── README.md               # This file
├── notebooks/
│   └── 1_uml.ipynb         # UML/activity diagram generation (PlantUML)
├── tinyos/                 # TinyOS/nesC source files (added in subsequent commits)
├── stm32/                  # STM32 C firmware (added in subsequent commits)
└── docs/                   # Report PDFs and figures (added in subsequent commits)
```

## Setup Instructions

### 1. Python Environment

Create and activate a virtual environment, then install dependencies:

```bash
python -m venv .venv
# Windows
.venv\Scripts\activate
# macOS / Linux
source .venv/bin/activate

pip install plantuml Pillow jupyter
```

### 2. Java (required for PlantUML)

PlantUML requires **Java JDK 8 or later** to render diagrams locally. If you prefer not to install Java, the notebook uses the PlantUML public server as a fallback — no local Java needed in that mode.

- Download: https://adoptium.net
- Verify installation: `java -version`

### 3. Running the Notebook

```bash
jupyter notebook notebooks/1_uml.ipynb
```

Run all cells in order. The first code cell installs Python dependencies (run once). The final render cell generates `notebooks/wsn_flowchart.png` and displays the diagram inline.

## Firmware (Coming Soon)

- **TinyOS/nesC** source for the TelosB motes (TPSN + GEAR implementation) will be added to `tinyos/` in a subsequent commit.
- **STM32 C firmware** (sensor polling, CI computation, UART output) will be added to `stm32/` in a subsequent commit.
