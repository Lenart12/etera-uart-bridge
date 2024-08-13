# Etera uart gpio expander

## Project Description

This project is an expander board designed to provide control for valve motors, eight relays, and the ability to measure multiple temperature sensors using the one-wire protocol. The board is implemented using KiCad PCB design software and the PCB files can be found in the `pcb` folder. The firmware for the board is developed using PlatformIO and can be found in the `pio-eub-firmware` folder. Additionally, a Python library for interfacing with the expander board is available in the `pyetera-uart-bridge` folder.

More information about the expander board, including usage instructions and examples, can be found in the [pyetera-uart-bridge/README.md](./pyetera-uart-bridge/README.md) file.

## Schematic

![Schematic](https://i.imgur.com/GOBnLDJ.png)

## Circuit

![Circuit](https://i.imgur.com/JnAUPYg.png)

## Home assistant MQTT device

[MQTT](images/home-assistant-mqtt.png)

## Hardware

[Installed PCBs](images/custom-etera-expaner.jpg)
