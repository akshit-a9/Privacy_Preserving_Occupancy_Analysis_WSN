# Privacy-Preserving Multi-Modal WSN for Real-Time Occupancy and Flow Analysis

**Team 5 — Distributed and Networked Embedded Systems (DNES/IoT)**

---

## Abstract

This report presents the design and prototype of a privacy-preserving wireless sensor network (WSN) that estimates occupancy and flow in public campus spaces such as the mess, gym, and badminton courts, without using any imaging device. Each sensing node fuses three complementary modalities — a millimetre-wave (mmWave) radar, a vibration/shock sensor, and an acoustic sensor — on an STM32 Nucleo-F446RE microcontroller. The microcontroller computes a derived *Congestion Index* (CI) locally and forwards only this scalar, along with a class label, to a TelosB mote running TinyOS. The mote network uses the Timing-sync Protocol for Sensor Networks (TPSN) for time synchronisation and the Geographic and Energy-Aware Routing (GEAR) protocol for multi-hop delivery to a base station, which drives an LCD and a relay-based alert output. By design, no raw sensor waveform ever leaves the sensing node, so the system provides occupancy insight without exposing individual-level information. A working prototype was built around four TelosB motes and an STM32 node; the CI pipeline, the radar driver, and the classification thresholds were validated on live sensor data streamed over UART.

---

## 1. Introduction

Occupancy sensing in shared campus spaces has become a recurring demand — for safety (overcrowding in the mess during peak hours), for planning (optimising booking slots for the badminton courts), and for user convenience (knowing whether the gym is full before walking to it). The conventional answer to this problem is a camera-plus-computer-vision pipeline. Cameras, however, introduce a hard trade-off: they are accurate, but they also capture the identities, faces, and movement patterns of every person they see. On a university campus this is neither acceptable to students nor operationally desirable for the institution.

This project proposes a multi-modal, camera-free alternative: a WSN in which each node infers crowd density from the *aggregate* signatures of people (radar reflections, floor/rack vibrations, and ambient sound), rather than from identifiable visual data. Three key design commitments follow from that choice:

1. **Sensor diversity.** No single low-cost sensor is reliable across all campus environments. A mmWave radar detects presence and motion but can miss quiet stationary occupants; a vibration sensor captures footfall and equipment use but is blind to stationary crowds; an acoustic sensor senses chatter and activity but is easily spoofed by HVAC or music. Fusing all three suppresses the blind spots of any individual modality.
2. **On-node derivation.** The raw waveforms from these sensors are, in principle, still reconstructable into behavioural traces (who walked where, when, how loud they were). The system therefore *never transmits raw samples*. Instead, a single scalar congestion index is computed locally on the STM32, and only that index plus a coarse class label (Empty / Low / Medium / High) leaves the node.
3. **Energy- and geography-aware routing.** The motes are battery-powered and deployed across a building. The packet of interest — the small CI message — must survive multi-hop delivery to the base station while balancing the energy drain across motes. GEAR is used for this, on top of a TPSN time-sync layer so timestamps across the network are consistent.

The target deployment sites are the campus mess, gym, and badminton courts. The end-user-facing output is a simple LCD display at a base station, together with a relay output that can drive an external alert (for example, an "At Capacity" LED outside the gym entrance) whenever the reported CI crosses a configured threshold.

---

## 2. Methodology

### 2.1 System Architecture

The network consists of four TelosB motes and one STM32-based sensing sub-system:

- **Base Station (BS) mote** — attached to the LCD and the relay module; acts as the TPSN root and the GEAR destination.
- **Sensing mote** — co-located with the STM32 Nucleo-F446RE. Receives the CI message over UART and injects it into the WSN.
- **Two dummy motes** — deployed as intermediate forwarding nodes to demonstrate multi-hop routing and GEAR's greedy / perimeter behaviour.
- **STM32 Nucleo-F446RE** — hosts the three sensors, runs the fusion pipeline, and emits a CSV-formatted CI stream.

The overall data flow is: *sensors → STM32 (CI computation) → UART → sensing TelosB → GEAR multi-hop → BS → LCD + relay.*

> **Figure 1.** System block diagram.
> *(See: `docs/02_Circuit_Diagram_Transparent.png`.)*

### 2.2 Sensors

- **HLK-LD2420 24 GHz mmWave radar.** Emits FMCW chirps and reports both presence ("ON"/"OFF") and an estimated range to the nearest target over a 115200 baud UART (OT1 pin). This modality gives line-of-sight presence detection through thin obstructions and is insensitive to ambient light. In the prototype it is wired to the STM32's USART1_RX (PA10), and a lightweight line-based parser in [radar.c](../sensor_read/radar.c) extracts `ON`, `OFF`, and `Range N` frames.
- **Vibration / shock sensor (analog).** A piezoelectric-style sensor read through ADC channel 1 on PA1. Footfall on the floor and use of gym equipment both produce broadband mechanical transients that this modality picks up even when the radar's field of view is obstructed.
- **Acoustic sensor (analog).** An electret microphone module read through ADC channel 0 on PA0. The envelope amplitude of ambient chatter, shoe squeaks, and equipment noise provides a third, complementary estimate of how "busy" the space is.

Placing all three sensors on the same node allows the fusion to happen locally, preserving the privacy principle without a separate aggregation step on the network.

### 2.3 Circuit and Wiring

The wiring between the STM32 Nucleo-F446RE and the sensors is minimal and follows the STM32F446RE pin layout in [`docs/03_Pin Layout - STM32F446RE.jpeg`](03_Pin%20Layout%20-%20STM32F446RE.jpeg):

| Signal | MCU pin | Peripheral | Notes |
|---|---|---|---|
| Acoustic (analog) | PA0 | ADC1_IN0 | 12-bit, 480-cycle sample time |
| Vibration (analog) | PA1 | ADC1_IN1 | 12-bit, 480-cycle sample time |
| Radar OT1 (UART TX) | PA10 | USART1_RX, AF7 | 115200 8N1 |
| UART to TelosB | PA2 / PA3 | USART2 | CSV CI stream |
| 3V3 / GND | — | — | Shared rail from Nucleo |

> **Figure 2.** Circuit diagram — STM32, sensors, and UART link to the sensing TelosB mote.
> *(See: [`docs/02_Circuit_Diagram_Transparent.png`](02_Circuit_Diagram_Transparent.png).)*

### 2.4 Protocols

**TPSN — Timing-sync Protocol for Sensor Networks.** All motes require a common notion of time so that CI packets can be correlated meaningfully at the base station. TPSN is used in a simplified form: the base station is *manually* declared as the root (level 0), and level-discovery is skipped; each mote performs a two-way message exchange with its parent to estimate the propagation delay and clock offset, then adjusts its local clock accordingly. This is sufficient for the small (4-mote) deployment and keeps the synchronisation protocol lightweight.

**GEAR — Geographic and Energy-Aware Routing.** GEAR is used for forwarding CI packets from the sensing mote to the base station. Each mote is given its coordinates manually at deployment time. Routing is performed in two phases:

1. **Greedy forwarding.** At each hop, the current holder of the packet computes a cost metric (a linear combination of remaining energy and distance to the destination) for each of its neighbours and forwards to the neighbour with the lowest cost.
2. **Perimeter fallback.** If no neighbour is closer to the destination than the current node (a "void" region), the packet is routed along the perimeter of the void using the right-hand rule until greedy forwarding can resume.

Because the prototype is small and the topology controlled, this behaviour is observable by deliberately powering down one of the dummy motes.

### 2.5 Congestion Index and Classification

The core of the on-node pipeline, implemented in [occupancy.c](../sensor_read/occupancy.c), is an exponentially weighted moving average (EWMA) over each modality followed by a weighted sum:

$$\text{CI} = \alpha \cdot p_{\text{norm}} + \beta \cdot v_{\text{norm}} + \gamma \cdot a_{\text{norm}}$$

where $p$, $v$, $a$ are the presence, vibration, and acoustic EWMAs, and $(\alpha, \beta, \gamma) = (0.50, 0.30, 0.20)$ — radar presence dominates because it is the least noisy of the three in the target environments. Each input is normalised into [0, 1]:

- *Vibration and acoustic* — normalised as `(ewma − baseline) / (ADC_MAX − baseline)`, with the per-deployment baseline learned during a 30-sample (≈3 s) calibration window at startup, during which the space is assumed empty. This makes the classifier robust to environment-specific noise floors (HVAC hum, fridge compressor, etc.) without code changes.
- *Presence (radar)* — already 0/1 after parsing; its EWMA is clipped into [0, 1].

The continuous CI is then bucketed into four classes using fixed thresholds (0.15 / 0.40 / 0.70): **Empty (E), Low (L), Medium (M), High (H)**.

Critically, the STM32 emits *only* `(timestamp, class, milli-CI)` onward. The raw ADC samples and the raw radar frames are consumed and discarded inside the node.

### 2.6 General Operating Method

The STM32 firmware in [main.c](../sensor_read/main.c) runs a single cyclic loop at a 100 ms sample period:

1. **Sample** — read PA0 and PA1 via ADC1, drain the USART1 radar buffer.
2. **Fuse** — call `Occupancy_Update()` to update EWMAs and compute CI + class.
3. **Emit** — transmit one CSV line over USART2 to the sensing TelosB mote.

The TelosB network layer then tags the message with a TPSN-synchronised timestamp, and GEAR forwards it hop-by-hop to the base station, which logs the packet, updates the LCD, and triggers the relay if the reported class is *High*.

> **Figure 3.** Full decision flowchart — TPSN sync, sensor polling, CI computation, GEAR forwarding, BS alert.
> *(See: [`notebooks/wsn_flowchart.png`](../notebooks/wsn_flowchart.png).)*

---

## 3. Results

The prototype was assembled and brought online on the STM32 node and the four-mote TelosB network. The following results were obtained from bench-testing in a controlled room, with motes placed at known coordinates.

**Sensor pipeline.** The STM32 node successfully boots, initialises the ADC, the radar UART, and the occupancy classifier, and begins streaming CSV output of the form:

```
# CSV: t_ms,acoustic,vibration,presence,range,ci_milli,class
12300,412,95,1,63,523,M
12400,418,102,1,62,537,M
12500,405,88,0,-1,310,L
```

During the 3-second calibration window the class is forced to *Empty* regardless of the input; after calibration the acoustic and vibration baselines were learned correctly (≈400 and ≈90 ADC counts respectively in the test environment).

**Multi-modal suppression of false positives.** With the radar alone, a single person sitting still in the field of view was correctly flagged; but a passing vehicle outside the window also raised the presence flag briefly. In the fused CI, the lack of correlated vibration and acoustic energy kept the CI below the *Low* threshold, correctly rejecting the vehicle as a transient.

**Classification stability.** The EWMA (λ = 0.20) kept the emitted class stable under sub-second sensor glitches — for example, a single "OFF" frame from the radar did not flip the class away from *Medium* once it had been established, which avoids flicker on the LCD.

**Networking.** TPSN achieved consistent timestamps across the four motes (base station, sensing mote, and two relays). CI packets originated at the sensing mote reached the base station via either of the two dummy motes depending on which neighbour GEAR selected; disabling one dummy mote mid-run forced GEAR into its perimeter fallback path and the packets continued to arrive at the BS, demonstrating the void-bypass behaviour.

**Base-station output.** The LCD correctly displayed the current class (`E/L/M/H`) and the milli-CI value received from the sensing node. The relay fired when the class transitioned into *High* and released once the class fell back to *Medium* or below.

**Privacy audit.** A serial capture of the TelosB radio traffic confirmed that no raw ADC sample and no raw radar frame ever left the sensing node — the only payload on the air was the `(timestamp, class, milli-CI)` tuple. The privacy design goal is therefore met at the network-boundary level.

---

## 4. Conclusion

This project demonstrates that useful occupancy and flow information can be extracted from a campus space *without* visual sensing and *without* exporting the raw signatures of individual people. Three complementary low-cost sensors — mmWave radar, vibration, and acoustic — were fused on an STM32 into a single derived Congestion Index, which was then delivered to a base station over a TinyOS mote network using TPSN time synchronisation and GEAR multi-hop routing. The working four-mote prototype produced stable, classifiable occupancy estimates in our test environment, survived radio topology changes via GEAR's perimeter fallback, and never put raw sensor data on the air.

The main limitations observed are (a) the EWMA/threshold constants (α, β, γ and the class cut-offs) are hand-tuned placeholders and would need per-deployment recalibration for the mess, gym, and badminton courts separately; (b) the baseline-learning phase at startup is blocking, which means the classifier is effectively blind for the first three seconds; and (c) the current GEAR implementation uses static coordinates, which limits adaptability if motes are relocated. Future work will address all three: automated calibration from a short logged run using the Python dashboard in [`terminal/`](../terminal/), an online baseline-drift estimator that does not require a cold-start empty-room assumption, and mote coordinates distributed at runtime via a small beacon protocol. Nevertheless, the prototype as it stands validates the central thesis of the project — that privacy-preserving, multi-modal WSN-based occupancy sensing is both feasible and practically useful on low-cost embedded hardware.

---

*Repository:* `07_DNES_Project` — see [`README.md`](../README.md) for hardware setup and build instructions.
