# Privacy-Preserving Multi-Modal WSN for Real-Time Occupancy and Flow Analysis

**Team 5 — Distributed and Networked Embedded Systems (DNES/IoT)**

A wireless sensor network that estimates crowd density in campus spaces — mess, gym, badminton courts — without cameras. Three sensors (millimeter-wave radar, vibration, acoustic) feed into an STM32, which computes a local congestion index and sends only that over the air. Raw data never leaves the node, so no one's movements are tracked. The network uses TelosB motes running TinyOS, with TPSN for time sync and GEAR for routing packets back to the base station.
