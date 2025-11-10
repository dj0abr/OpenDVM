[🇬🇧 English](README.md) | [🇩🇪 Deutsch](README.de.md)

🚧 **This project is currently under active development – check back often for updates.**

# 🛰️ OpenDVM MultiMode Repeater/Station for Debian/Linux

This project builds upon the **Digital Voice (DV) modules by G4KLX**, which are combined into a complete solution like **Pi-Star**.  
However, Pi-Star is designed **exclusively for the Raspberry Pi**.

👉 **The goal of this project** is to create a **fully functional, platform-independent multimode DV solution** that runs on **any Debian-based system** – highly reliable on real PCs or servers (PC, virtual machines, Raspberry, Odroid, Orange Pi and many more).

The current version supports **D-Star, DMR and YSF (C4FM) on MMDVM Boards** (USB Boards or HATs).

It uses the following G4KLX repositories:

- [MMDVMHost](https://github.com/g4klx/MMDVMHost)  
- [ircDDBGateway](https://github.com/g4klx/ircDDBGateway)  
- [DMRGateway](https://github.com/g4klx/DMRGateway)  
- [YSFClient](https://github.com/g4klx/YSFClients) *(used as a gateway)*  

The **entire installation** is automated through **five shell scripts** that correctly set up all components.  

Additionally, the project includes a **logfile parser** that reads all operational data from MMDVM, YSF, and DMR logs and writes them into a **MariaDB database**.  
These data serve as the backend for a modern **web dashboard** that displays the repeater or hotspot status in real time.

This project was originally developed for the MMDVM Repeater Builder board used in the DB0SL multimode repeater. It also runs with common MMDVM Raspberry Pi HATs. For other specialized hardware or use cases, adjust the configuration files as needed (see the [Modem] section in MMDVMHost.ini).

---

## 📖 Table of Contents

1. [Overview](#-overview)  
2. [Architecture](#-architecture)  
3. [Backend – Log Monitor & Database](#-backend--log-monitor--database)  
4. [Installation & Dependencies](#-installation--dependencies)  
5. [Configuration](#-configuration)  
6. [Web Frontend](#-web-frontend)  
7. [System Setup & Maintenance](#-system-setup--maintenance)  
8. [Credits & License](#-credits--license)

---

<a href="gui.png">
  <img src="gui.png" alt="Systemübersicht" width="250">
</a>

<a href="gui1.png">
  <img src="gui1.png" alt="Systemübersicht" width="250">
</a>

🔗 **Live Installation:** [digital.db0sl.de](https://digital.db0sl.de/)

## 🔍 Overview

**Main features:**

- Real-time monitoring of MMDVMHost, YSFGateway, and DMRGateway logs  
- Automatic storage of detected events in MariaDB  
- Graphical presentation via a modern web frontend  
- Central configuration through a unified `site.conf` file  
- Automatic generation of all gateway configuration files  
- Fully passwordless, secure database access  
- No frameworks, no Pi dependency – runs on any Debian system

---

## ⚙️ Architecture

<img src="flowchart.png" alt="Systemübersicht" width="200">

---

## 🧠 Backend – Log Monitor & Database

The main program continuously monitors the following log files:

- `/var/log/mmdvm/MMDVM-YYYY-MM-DD.log`  
- `/var/log/mmdvm/YSFGateway-YYYY-MM-DD.log`  
- `/var/log/mmdvm/DMRGateway-YYYY-MM-DD.log`

New entries are immediately detected, parsed, and written into the database.

### Captured Information

- TX activities and callsigns for D-Star, DMR, and System Fusion  
- Duration and BER of each transmission  
- Current mode of operation  
- Reflector status for D-Star, Fusion, and DMR  
- Automatic detection of log rotation and truncation  
- Reconnects automatically after database errors  

### Database Tables

| Table | Description |
|--------|--------------|
| `status` | Current status (mode, callsign, RF/NET, duration, BER) |
| `lastheard` | Every transmission with timestamp |
| `reflector` | Current reflector per mode |
| `config_inbox` | Configuration |

### Special Features

- Detection of interrupted transmissions (heuristic timing)  
- Callsign validation (min. 3 characters, at least 1 digit)  
- D-Star does not store DG-ID, Fusion does  
- “Watchdog expired” messages are treated as EOT  
- DMR master names (e.g., `BM_2621_Germany`) are recognized automatically  

---

### Duplex

There are two types of MMDVM HATs:

Simplex: most MMDVM boards are "simplex" boards with one or two antennas<br>
Duplex: special MMDVM boards e.g., "repeater builder" boards

The “Duplex” setting must match the hardware in use; otherwise, operation may only work in one direction. Usually "0" will be used for homebrew stations.

## 🧰 Installation & Dependencies

Installation is fully automated through **shell scripts**, which install all dependencies, programs, and configuration files.

First, download this repository from GitHub:

```bash
sudo apt update
sudo apt install git -y
git clone https://github.com/dj0abr/OpenDVM.git
cd OpenDVM
```

Now run the scripts (all with sudo) as follows:

### Installation Order

👉 **Important:**  
These scripts must be executed **in this order**.

1. **for Raspberry PI with MMDVM HAT ONLY**
   - Skip this section if you **don’t use a Raspberry Pi** or if your MMDVM is connected **via USB**. In that case, go directly to **2. Install the serial port.**
   - If you **do** have a Raspberry Pi with an **MMDVM HAT mounted directly on the GPIO header**, you need to **enable the internal serial port**. 
   Run the following command and reboot afterward:
   ```bash
   sudo ./install_raspi.sh
   sudo reboot
   ```
   - After reboot continue with **2. Install the serial port**

2. **Install the serial port**  
   - Run the script:
   ```bash
   sudo ./install_serial.sh
   ```
   - Detects your serial device (USB, onboard UART, etc.), lets you pick the correct one
   - re-run this script to switch to a different device (e.g. a new hardware). In most cases, the shown default device is correct and can be used as is.

3. **Install the MMDVM System and all Gateways**  
   - Run the script:
   ```bash
   sudo ./install.sh
   ```
   - Installs all system dependencies  
   - Prepares directories (e.g., `/var/log/mmdvm`)  
   - Sets up the MariaDB database  
   - Compiles and installs the C++ backend  
   - Installs the central DV interface **MMDVMHost**
   - Installs and configures the **System Fusion Gateway**
   - Installs and configures the **D-Star Gateway**
   - Installs and configures the **DMR Gateway**

After completion, **default configuration files** are automatically copied to `/etc`.  
They must then be adjusted to match your setup – see [Configuration](#-Configuration).

---

## 🧾 Configuration

All site and system parameters for the G4KLX modules are stored in the following configuration files:

   /etc/MMDVMHost.ini  
   /etc/ircddbgateway  
   /etc/ysfgateway  
   /etc/dmrgateway

Sample versions of these files are included in this package and must be customized to match your station or repeater setup.

The most important settings can be set configured the SETUP window:

### Steps

1. **Open the GUI in a browser:**
   ```
   Enter the board's IP address in your browser.
   Locate the “SETUP” button in the top-right corner.  
   Click “SETUP”.
   ```

2. **Edit the Configuration:**

   The minimum required settings are:

   * Your callsign
   * Your DMR ID (if required, click the link below the DMR ID field to open the DMR database)
   * Set Duplex = 0 (set to 1 ONLY if you are using a repeater board)
   * Set RX and TX frequencies. It is highly recommended to use differet RX and TX frequencies, otherwise you may experience issues with older D-Star Transceivers
   * Enter your Brandmeister password (as configured in SelfCare on the BM Dashboard)
   * Enter the Config Password (default: setuppassword). You can define your own password by editing the file save_config.php (you find it in folder: ./gui/html, after editing copy it to /var/www/html).
   * Click "SAVE"

   If you receive a green confirmation message, the configuration has been successfully stored.

You can still manually edit the generated configuration files if needed — but this is rarely necessary.

5. **Reboot:**
   ```bash
   sudo reboot
   ```
   After reboot, the system will be fully operational.

   Please allow some time for the reflectors to connect after the first reboot — this may take several minutes

---

## 🌐 Web Frontend

The web frontend displays all operational data in real time.  
Completely static – no PHP framework required, just a small `api.php` for JSON output.

### Features

- Live status: mode, callsign, duration, BER, RF/NET  
- Colored status tiles and country flags  
- Reflector status for D-Star, DMR, Fusion  
- “Last Heard” list with callsign, timestamp, duration  
- Activity chart (48h, RF/NET separated)  
- Bar statistics and 30-day heatmap  
- Responsive dark UI  
- Only external library: **Chart.js**

### Technology

- Pure Vanilla JavaScript  
- CSS grid layout  
- Updates every second via AJAX  
- Works on any webserver (nginx, Apache, lighttpd)

---

## 🧱 System Setup & Maintenance

- Database runs via Unix socket  
- Installation scripts automatically create users and permissions  

---

## 🎯 Credits & License

- Jonathan Naylor G4KLX, for his outstanding DV implementations that form the foundation of this project  
- This software is licensed under **GPL v2** and is primarily intended for amateur radio and educational use. This project includes components from G4KLX licensed under GPL v2.
Therefore, the combined work remains under GPL v2.
