# Mirage 3D Scanning System

This repository contains hardware, software, and experimental materials for the Mirage 3D scanning system, developed as part of a bachelor’s thesis.

Mirage is a hardware-software system for acquiring RGB-D data from a camera, transmitting it to a computing node, processing it, and constructing a 3D model of the object.

The system has a distributed architecture and consists of two main parts:

- a data acquisition node
- a data processing node

The data acquisition node is built on a Raspberry Pi CM5, a Mirage V1.1 expansion board, and an Intel RealSense D415 RGB-D camera. It is used to connect the camera, launch the local interface, display the system status, control operating modes, and transmit the RGB-D stream to a PC. The node also features physical control buttons that can be used to start, stop, or reset the scanning process. Since the node was designed to be autonomous, it can operate on battery power.

The computing node is implemented on a PC. It runs MirageApp—a modified program based on InfiniTAM—which receives RGB-D data, processes the depth map, tracks the camera’s position, integrates frames into a TSDF representation, and constructs a polygonal 3D model. The result can be saved in OBJ format for further analysis or use.

The system supports two main operating modes:

- Network mode — the Intel RealSense D415 is connected to a data collection node, and the RGB-D stream is transmitted to the PC via Wi-Fi.
- USB mode — the Intel RealSense D415 is connected directly to the PC via USB. 

## Repository structure

```text
hardware/...      — schematics, PCB files and enclosure models
software/...      — software for the data acquisition node and data processing node
experiments/...   — measurements, test models and experimental results
