# Privacy-Preserving Multi-Sensor Crowd Sensing on the STM32 Nucleo

**Team 5, Distributed and Networked Embedded Systems (DNES/IoT)**

---

## Abstract

Public spaces on a campus, such as the mess, the gym, and the badminton courts, often get crowded at unpredictable times. Knowing how busy these places are without using cameras would help students plan their day and would also help the institution manage the space safely. This project presents a small embedded system that estimates how crowded a room is using three low cost sensors that look at sound, motion, and vibration. All three sensors are wired directly to an STM32 Nucleo F446RE board, which reads them, combines their readings into a single occupancy class, and prints the result over USART2 so it can be viewed on any serial terminal. Because no camera is used, no image of any person is ever captured or sent. A working single node prototype was built and tested with all three sensors connected, and also with one and two sensors removed at runtime, to confirm that the system stays useful even when a sensor stops responding.

---

## 1. Introduction

Most crowd sensing systems today use a camera and a computer that runs a person detector on the video feed. That works well, but it sees everything: who walks where, what they wear, and how they behave. On a university campus, students do not want to be watched while they eat or work out. The institution does not really need to see them either. All it needs is a number that tells it whether a place is empty, lightly used, busy, or full.

This project builds a small device that produces exactly that number. The idea is to use cheap sensors that capture the side effects of a crowd instead of pictures of the crowd itself. People talk, walk, and move equipment around. A microphone hears the chatter. A vibration sensor feels the footsteps. A radar sees motion in the room. None of these readings on their own can tell you who is there. Together, they can tell you how busy the place is.

The application targets three settings on the campus: the mess at meal hours, the gym during peak training time, and the badminton courts in the evenings. The output is a simple class label, one of *Empty*, *Low*, *Medium*, or *High*. This label is printed over the Nucleo's virtual COM port and shown on any serial terminal such as Tera Term or PuTTY. A small Python dashboard built with the `rich` library was also written so the same data can be read in a friendlier format on a host laptop.

The single node version of the project, described in this report, is what was built and tested end to end on the Nucleo board.

> **Figure 1.** Full hardware setup on the bench with the host laptop.
> *(Placeholder: [`docs/img/setup_with_laptop.jpg`](img/setup_with_laptop.jpg).)*

---

## 2. Methodology

### 2.1 System Overview

The system has one sensing point. The STM32 Nucleo F446RE sits at the centre of it. Three sensors are wired into the board. The board reads them on a fixed timer, runs a small classifier, and sends one printable line of text every 100 milliseconds over USART2. USART2 is connected to the on-board ST-Link, which exposes it as a virtual COM port over the same USB cable that powers the board. Anyone with a serial terminal on a laptop can plug in and see the live output.

> **Figure 2.** The same setup without the laptop, showing the Nucleo board, the three sensors, and the wiring on the breadboard.
> *(Placeholder: [`docs/img/setup_without_laptop.jpg`](img/setup_without_laptop.jpg).)*

### 2.2 Sensors

Three sensors were chosen so that each one covers a weakness of the others.

- **MAX9814 microphone amplifier with a CMA-4544PF-W capsule.** This is a small electret microphone with an automatic gain control amplifier on the same board. It outputs an analog signal that follows the loudness of the room. Chatter, footsteps on a hard floor, and the clatter of plates or weights all show up as larger swings in this signal. The Nucleo reads it on ADC channel 0 (pin PA0) and computes the root mean square value of the recent samples to get a stable estimate of how loud the room is.
- **Hi-Link HLK-LD2420 24 GHz radar.** This is a tiny millimetre wave radar that detects human motion through plastic and thin walls and works in the dark. The module sends short text frames out of its OT1 pin at 115200 baud. The Nucleo reads them on USART1 (pin PA10) and parses out two pieces of information from each line: whether a person is currently detected (`ON` or `OFF`), and the distance to the nearest person in centimetres (`Range N`). A person standing far away contributes less to the crowd count than a person standing right at the door.
- **LM393 vibration sensor.** This is a piezo style shock and tilt sensor mounted on a small comparator board. Footsteps on the floor, a chair being moved, and machines being used all produce mechanical transients that this sensor picks up. The Nucleo reads it as an analog signal on ADC channel 1 (pin PA1) and tracks the average level relative to its idle baseline.

Putting all three sensors on the same node means the fusion can happen locally and the raw signals never leave the device.

### 2.3 Circuit and Wiring

The three sensors share the 3.3 V and ground rails of the Nucleo board. Their signal lines go to the pins listed below.

| Signal | MCU pin | Peripheral | Notes |
|---|---|---|---|
| Microphone (MAX9814 OUT) | PA0 | ADC1_IN0 | 12 bit, 480 cycle sample time |
| Vibration (LM393 OUT) | PA1 | ADC1_IN1 | 12 bit, 480 cycle sample time |
| Radar (LD2420 OT1) | PA10 | USART1_RX, AF7 | 115200 8N1 |
| Output to host | PA2 / PA3 | USART2 over ST-Link VCP | 115200 8N1 |
| Power | 3V3 / GND | shared rail | from Nucleo |

> **Figure 3.** Circuit diagram of the full setup.
> *(See: [`docs/02_Circuit_Diagram_Transparent.png`](02_Circuit_Diagram_Transparent.png).)*

### 2.4 Protocols Used

The board uses three standard on-chip facilities to talk to the outside world.

1. **ADC with DMA.** The two analog sensors are sampled by the on-chip 12 bit ADC. A DMA channel writes every new pair of conversions straight into a small array in memory and triggers an interrupt on completion. The interrupt handler accumulates the squared microphone reading and the raw vibration reading, so the main loop can later compute their averages without ever having to wait on an ADC conversion.
2. **USART1 with receive interrupt.** The radar's text output is read one byte at a time using the HAL receive interrupt. A small line assembly routine collects characters until a carriage return or newline arrives, then hands the completed line to a parser that turns it into the presence flag and the range value.
3. **USART2 over the Virtual COM Port.** The fused result is printed on the Nucleo's USART2, which is wired through the ST-Link debugger and shows up as a virtual COM port on the host laptop. Any serial terminal can read this stream at 115200 baud.

The earlier version of the project also planned a small wireless mote network on top of these, with separate protocols for time synchronisation and multi hop routing, but the mote hardware was withdrawn from the scope of the project so those parts were not implemented in the final system.

### 2.5 General Method

The board does the same short cycle every 100 milliseconds.

1. **Sample.** New microphone and vibration values arrive automatically through DMA. New radar lines are picked up by the USART1 interrupt and parsed.
2. **Process.** The microphone value is reduced to a root mean square level. The vibration value is converted into a deviation from its idle baseline. The radar gives a presence flag and the average range over the recent samples.
3. **Classify each sensor.** Each of the three readings is compared against a small set of thresholds tuned in the lab and assigned a level from 0 (Empty) to 3 (High). The radar uses inverted thresholds because a closer person means a busier space.
4. **Detect disconnections.** A microphone that reads near zero, a vibration sensor stuck at the rail, or a radar that has not produced any range value all count as disconnected. Disconnected sensors are dropped from the fusion step instead of poisoning it.
5. **Fuse.** The remaining sensor levels are combined using a weighted mean. The radar carries a slightly higher weight because it is the most direct measure of presence. The mean is rounded to the nearest integer and turned back into one of the four labels.
6. **Emit.** A single line of text is printed on USART2 with the raw values, the per sensor levels, and the final fused label. If two sensors are disconnected the line carries a warning. If all three are disconnected the line says so explicitly.

> **Figure 4.** Decision flow for one sample cycle.
> *(See: [`notebooks/final_decision_flowchart.png`](../notebooks/final_decision_flowchart.png), generated from [`notebooks/2_final_uml.ipynb`](../notebooks/2_final_uml.ipynb).)*

---

## 3. Results

The full system was assembled on a small breadboard next to the Nucleo and tested in a quiet room with one person walking in and out. The microphone was placed near the wall to catch chatter, the vibration sensor was taped to the underside of the table, and the radar was pointed at the doorway.

**All three sensors connected.** When the room was empty the line showed values close to zero on every channel and the fused label stayed at *Empty*. When one person walked in and stood near the radar, the radar level rose to *Medium* and the fused label tracked it. With the person also speaking and tapping the table, all three sensors agreed on *Medium* or *High*. The serial output was clean and arrived at a steady ten lines per second.

> **Figure 5.** Tera Term capture with all three sensors connected.
> *(Placeholder: [`docs/img/teraterm_all.png`](img/teraterm_all.png).)*

> **Figure 6.** The same data shown live in the Python `rich` dashboard.
> *(Placeholder: [`docs/img/dashboard_all.png`](img/dashboard_all.png).)*

**One sensor removed.** Pulling the vibration sensor off the breadboard caused its raw value to settle near the rail. The board recognised this within a few cycles, marked the vibration column with `-` instead of a level, and continued to fuse the remaining two sensors. The fused label stayed close to what it would have been with all three sensors, because the microphone and the radar between them already covered the room.

> **Figure 7.** Output with one sensor removed, in Tera Term and in the dashboard.
> *(Placeholders: [`docs/img/teraterm_one_removed.png`](img/teraterm_one_removed.png) and [`docs/img/dashboard_one_removed.png`](img/dashboard_one_removed.png).)*

**Two sensors removed.** Removing the microphone as well left only the radar in the loop. The line now carried the warning text `[WARNING: 2 sensors disconnected]` after the result. The fused label was driven entirely by the radar and reacted promptly to the person stepping into and out of its field of view.

> **Figure 8.** Output with two sensors removed.
> *(Placeholders: [`docs/img/teraterm_two_removed.png`](img/teraterm_two_removed.png) and [`docs/img/dashboard_two_removed.png`](img/dashboard_two_removed.png).)*

**All sensors removed.** With every sensor unplugged, the firmware printed `All sensors disconnected` once per cycle. The dashboard reflected this state and stopped reporting an occupancy label, which is the safest behaviour for a downstream user.

> **Figure 9.** Output with all sensors disconnected.
> *(Placeholders: [`docs/img/teraterm_all_removed.png`](img/teraterm_all_removed.png) and [`docs/img/dashboard_all_removed.png`](img/dashboard_all_removed.png).)*

The fusion step is what makes this graceful failure possible. Because the weights are renormalised over only the connected sensors, the system keeps producing useful output for as long as at least one sensor is alive, and it never silently presents a stale or wrong value when everything is gone.

---

## 4. Conclusion

A working single node crowd sensing prototype was built around the STM32 Nucleo F446RE and three low cost sensors. The board reads a microphone, a vibration sensor, and a 24 GHz radar, fuses them into one of four occupancy classes, and prints the result over its virtual COM port for any serial terminal to read. No camera is used and no raw waveform leaves the board. The classifier handles sensor disconnections without breaking, falling back to whichever sensors are still alive and clearly marking the situation when none are left.

The wireless networking layer that was part of the earlier design, along with the time synchronisation and routing protocols planned for it, was not implemented in the final version because the underlying mote hardware was withdrawn from the scope of the project. The end to end pipeline of sensing, processing, classifying, and reporting is fully functional on the Nucleo as required, and is shown in the figures above.

The main limitations of the current prototype are that the per sensor thresholds were tuned by hand in the test room and would need to be re-measured for each deployment site, and that the baseline for the vibration channel is fixed in the firmware instead of being learned automatically. A short calibration step at startup and a small configuration menu over the serial port would address both of these in a follow-up version.

---

*Repository:* `crowdy`. See [`README.md`](../README.md) for hardware setup and build instructions.
