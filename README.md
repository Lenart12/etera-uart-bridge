# Etera uart gpio expander

## Project Description

This project is an expander board designed to provide control for valve motors, eight relays, and the ability to measure multiple temperature sensors using the one-wire protocol. The board is implemented using KiCad PCB design software and the PCB files can be found in the `pcb` folder. The firmware for the board is developed using PlatformIO and can be found in the `pio-eub-firmware` folder. Additionally, a Python library for interfacing with the expander board is available in the `pyetera-uart-bridge` folder.

More information about the expander board, including usage instructions and examples, can be found in the [pyetera-uart-bridge/README.md](./pyetera-uart-bridge/README.md) file.


## Hardware Description

The expander board is designed to provide versatile control capabilities for various applications. It features 8 GPIO pins that can be utilized for controlling relays or other general-purpose tasks. Additionally, the board is equipped with the ability to control 4 motors, incorporating optocouplers to ensure protection and isolation of the motor control circuits.

For temperature monitoring, the board supports one-wire temperature sensors. These sensors are enhanced with an active pull-up mechanism to ensure reliable communication over long distances. Furthermore, the board includes a FET driver to improve the slew rate, which helps in reducing electromagnetic interference and improving signal integrity over long wire distances. For this purpose the standard Arduino OneWire library was modified into [OneWireFet](./pio-eub-firmware/lib/OneWireFet) which can support mastering the 1-wire bus with two uC pins. 

#### Appendix A. Improved CPU Bus Interface

![Appendix A. Improved CPU Bus Interface](https://www.analog.com/en/_/media/analog/en/landing-pages/technical-articles/guidelines-for-reliable-long-line-1wire-networks/148fig06.gif?rev=-1)

> For long line applications, modifications are necessary. Appendix A shows a variant of the microprocessor port-pin attachments, i e., a FET driver with slew-rate control and a 1kΩ pullup resistor. A radius of up to 200m and a weight of up to 200m can be reliably supported using this interface.

For more detailed guidelines on setting up reliable long-line one-wire networks, you can refer to this [technical article](https://www.analog.com/en/resources/technical-articles/guidelines-for-reliable-long-line-1wire-networks.html).

## Schematic

![Schematic](https://i.imgur.com/GOBnLDJ.png)

## Circuit

![Circuit](https://i.imgur.com/JnAUPYg.png)

## Home assistant MQTT device

![MQTT](images/home-assistant-mqtt.png)

## Hardware

![Installed PCBs](images/custom-etera-expaner.jpg)
