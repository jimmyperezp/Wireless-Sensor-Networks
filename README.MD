# Wireless Sensor Networks — Jaime Pérez

<!-- 🖼️ Optional: add a banner image at extras/images/wsn-banner.png and uncomment the line below -->
<!-- <img src="https://github.com/jimmyperezp/Wireless-Sensor-Networks/blob/main/extras/images/wsn-banner.png" alt="WSN banner" width="1000"/> -->

<img src="https://github.com/HQarroum/awesome-iot/blob/master/iot-logo.png" alt="IoT" width="40" height="40"/> <img src="https://upload.wikimedia.org/wikipedia/commons/1/18/C_Programming_Language.svg" alt="C" width="40" height="40"/> <img src="https://upload.wikimedia.org/wikipedia/commons/9/9a/Visual_Studio_Code_1.35_icon.svg" alt="VS Code" width="40" height="40"/> <img src="https://upload.wikimedia.org/wikipedia/commons/3/35/Tux.svg" alt="Linux" width="40" height="40"/>

> Labs and final project of the **Wireless Sensor Networks** course (Máster en Electrónica Industrial, ETSII – UPM), developed with **Contiki-NG** on **Cookie** nodes.

<br>

### Index:

1. [Goal of the repository](#goal-of-the-repository)
2. [Repository structure](#repository-structure)
3. [Labs](#labs)
4. [Final project](#final-project)
5. [Hardware used](#hardware-used)
    - [Cookie nodes](#cookie-nodes)
6. [Software & tools](#software--tools)
    - [Contiki-NG](#contiki-ng)
    - [Development environment](#development-environment)
7. [Building and flashing](#building-and-flashing)
8. [Project status](#project-status)

<br><br>

### Goal of the repository

This repository gathers all the practical work done for the *Wireless Sensor Networks* course. It works both as a **personal log** of each lab session and as a **reference** to come back to when building the final project.

Each lab folder contains the source code used during the session, plus any notes or results worth keeping (serial outputs, screenshots, conclusions).

The main topics covered throughout the course are:

- Low-power embedded programming on wireless nodes.
- Event-driven programming with **Contiki-NG** processes and protothreads.
- Sensor acquisition and actuator control (LEDs, buttons, on-board sensors).
- Radio communication between nodes and networking protocols for constrained devices (IEEE 802.15.4, 6LoWPAN, RPL, UDP…).
- Deploying a small sensor network to solve a real-world problem.

<br><br>

### Repository structure

```
Wireless-Sensor-Networks/
├── .vscode/            # VS Code workspace settings
├── Labs/               # One folder per lab session
│   ├── Lab1/
│   ├── Lab2/
│   └── ...
├── Final project/      # 🚧 Coming soon
└── README.md
```

<br><br>

### Labs

| Lab | Topic | Status |
|:---:|---|:---:|
| [Lab 1](/Labs/Lab1) | *Introduction to Contiki-NG and the Cookie node: first process, LEDs and serial output* | ✅ |
| [Lab 2](/Labs/Lab2) | *Description pending* | ✅ |
| [Lab 3](/Labs/Lab3) | *Description pending* | 🚧 |
| ... | ... | ⏳ |

> ✏️ **Note:** update the folder links and topics so they match the actual names inside [`/Labs`](/Labs).

<br><br>

### Final project

🚧 **PENDING** — The final project will be developed together with **Adrián Riveiro Varela**.

The planned scope is **people / occupancy detection** using a wireless sensor network. Some of the ideas under consideration:

- 🅿️ **Smart parking:** detecting free parking spots with NFC (a tag on the car and a reader on each spot).
- 📚 **Library occupancy:** monitoring capacity and free study places in the school library.

This section will be updated with the final design, architecture and results once the project starts.

<br><br>

### Hardware used

#### Cookie nodes

<!-- 🖼️ Add a picture of the Cookie node at extras/images/cookie.jpg -->
<!-- <img src="https://github.com/jimmyperezp/Wireless-Sensor-Networks/blob/main/extras/images/cookie.jpg" alt="Cookie node" width="300" align="right"/> -->

The labs are carried out on **Cookie** nodes: a modular wireless sensor platform with stackable layers (processing, radio communication, sensors and power), which makes it easy to adapt each node to the needs of the application.

Each node runs **Contiki-NG** and communicates with the rest of the network through its radio layer, while the computer is used to program it and to read its output via the serial monitor.

<br><br>

### Software & tools

#### Contiki-NG

<img src="https://github.com/contiki-ng/contiki-ng.github.io/blob/master/images/contiki-ng-logo.png?raw=true" alt="Contiki-NG" width="150" align="right"/>

[Contiki-NG](https://github.com/contiki-ng/contiki-ng) is an open-source operating system for resource-constrained IoT devices. It provides:

- An **event-driven kernel** based on processes and protothreads.
- A full **low-power IPv6 stack** (6LoWPAN, RPL, CoAP, MQTT…).
- Drivers and examples for many platforms.

Useful links:

- [Contiki-NG documentation](https://docs.contiki-ng.org/)
- [Contiki-NG GitHub repository](https://github.com/contiki-ng/contiki-ng)
- [Contiki-NG examples](https://github.com/contiki-ng/contiki-ng/tree/develop/examples)

<br><br>

#### Development environment

> All the code is written and managed from **VS Code**.

<img src="https://upload.wikimedia.org/wikipedia/commons/9/9a/Visual_Studio_Code_1.35_icon.svg" alt="VS Code" width="80" align="right"/>

- **Editor:** VS Code (workspace settings included in [`.vscode`](/.vscode)).
- **Language:** C.
- **Toolchain:** Contiki-NG build system (`make`) with the cross-compiler for the node's microcontroller.
- **Serial monitor:** used to debug and check the messages printed by each node.

<br><br>

### Building and flashing

The general workflow to compile and upload a lab to a Cookie node is the following:

1. Clone this repository and make sure Contiki-NG is installed and reachable from the lab's `Makefile` (variable `CONTIKI`).

    ```bash
    git clone https://github.com/jimmyperezp/Wireless-Sensor-Networks.git
    ```

2. Go into the lab folder you want to run:

    ```bash
    cd Wireless-Sensor-Networks/Labs/Lab1
    ```

3. Compile, flash and open the serial monitor:

    ```bash
    make TARGET=<cookie_target>                 # build
    make TARGET=<cookie_target> <app>.upload    # flash the node
    make TARGET=<cookie_target> login           # serial monitor
    ```

> ✏️ Replace `<cookie_target>` and `<app>` with the target name and application used in the course.

<br><br>

### Project status:

🟢 Active — labs are being added as the course progresses.  
🚧 Final project: pending.

<br><br>

---------
Author: Jaime Pérez  
Course: Wireless Sensor Networks — Máster en Electrónica Industrial (ETSII-UPM)  
Last update: 03/11/2026
<img src="https://github.com/jimmyperezp/Programacion_de_sistemas/blob/main/logo%20escuela.png" align="right" alt="logo industriales" width="300" height="80"/>
****