# Mirage 3D Scanning System

This repository contains hardware, software and experimental materials for the Mirage 3D scanning system, developed as part of a bachelor's thesis.

Mirage is a hardware-software system for acquiring RGB-D data from a camera, transmitting it to a computing node, processing it and constructing a 3D model of the object.

## Architecture

The system has a distributed architecture and consists of two main nodes:

### Data Acquisition Node
Built on a Raspberry Pi CM5, a Mirage V1.1 expansion board and an Intel RealSense D415 RGB-D camera. Used to connect the camera, launch the local interface, display the system status, control operating modes and transmit the RGB-D stream to a PC. Features physical control buttons to start, stop or reset the scanning process. Designed to operate on battery power.

### Computing Node
Implemented on a PC. Runs MirageApp, a modified program based on InfiniTAM, which receives RGB-D data, processes the depth map, tracks the camera position, integrates frames into a TSDF representation and constructs a polygonal 3D model. The result can be saved in OBJ format.

## Operating Modes

- **Network mode** — the Intel RealSense D415 is connected to the data acquisition node and the RGB-D stream is transmitted to the PC via Wi-Fi.
- **USB mode** — the Intel RealSense D415 is connected directly to the PC via USB.

## Repository Structure

```
hardware/...      — schematics, PCB files and enclosure models
software/...      — software for the data acquisition node and data processing node
experiments/...   — measurements, test models and experimental results
```
