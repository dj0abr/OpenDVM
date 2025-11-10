[🇬🇧 English](README.md) | [🇩🇪 Deutsch](README.de.md)

# 🛰️ OpenDVM Multimode Repeater/Station für Debian/Linux und MMDVM Boards

Dieses Projekt basiert auf den **Digital Voice (DV) Modulen von G4KLX**, die zu einer kompletten Lösung wie **Pi-Star** kombiniert werden.  
Allerdings ist Pi-Star **ausschließlich für den Raspberry Pi** konzipiert.

👉 **Ziel dieses Projekts** ist es, eine **voll funktionsfähige, plattformunabhängige Multimode-DV-Lösung** zu schaffen, die auf **jedem Debian-basierten System** läuft – stabil und zuverlässig auf echter Hardware oder virtuellen Maschinen (PC, VM, Raspberry, Odroid, Orange Pi und viele mehr).

Die aktuelle Version unterstützt **D-Star, DMR und YSF (C4FM)** auf **MMDVM-Boards** (USB-Boards oder HATs).

Folgende G4KLX-Repositories werden verwendet:

- [MMDVMHost](https://github.com/g4klx/MMDVMHost)  
- [ircDDBGateway](https://github.com/g4klx/ircDDBGateway)  
- [DMRGateway](https://github.com/g4klx/DMRGateway)  
- [YSFClient](https://github.com/g4klx/YSFClients) *(als Gateway genutzt)*  

Die **gesamte Installation** erfolgt automatisiert über **fünf Shell-Skripte**, die alle Komponenten korrekt einrichten.  

Zusätzlich enthält das Projekt einen **Logfile-Parser**, der Betriebsdaten aus MMDVM-, YSF- und DMR-Logs liest und in eine **MariaDB-Datenbank** schreibt.  
Diese Daten dienen als Backend für ein modernes **Web-Dashboard**, das den Repeater- oder Hotspot-Status in Echtzeit anzeigt.

Das Projekt wurde ursprünglich für das MMDVM Repeater Builder Board des DB0SL-Multimode-Repeaters entwickelt. Es läuft aber auch mit gängigen MMDVM Raspberry Pi HATs. Für andere Hardware oder spezielle Anwendungen können die Konfigurationsdateien angepasst werden (siehe Abschnitt [Modem] in MMDVMHost.ini).

---

## 📖 Inhaltsverzeichnis

1. [Übersicht](#-übersicht)  
2. [Architektur](#-architektur)  
3. [Backend – Log-Monitor & Datenbank](#-backend--log-monitor--datenbank)  
4. [Installation & Abhängigkeiten](#-installation--abhängigkeiten)  
5. [Konfiguration](#-konfiguration)  
6. [Web-Frontend](#-web-frontend)  
7. [Systemeinrichtung & Wartung](#-systemeinrichtung--wartung)  
8. [Danksagung & Lizenz](#-danksagung--lizenz)

---

<a href="gui.png">
  <img src="gui.png" alt="Systemübersicht" width="250">
</a>

<a href="gui1.png">
  <img src="gui1.png" alt="Systemübersicht" width="250">
</a>

🔗 **Live-Installation:** [digital.db0sl.de](https://digital.db0sl.de/)

## 🔍 Übersicht

**Hauptfunktionen:**

- Echtzeitüberwachung der Logs von MMDVMHost, YSFGateway und DMRGateway  
- Automatische Speicherung erkannter Ereignisse in MariaDB  
- Grafische Darstellung über ein modernes Web-Frontend  
- Zentrale Konfiguration über eine einheitliche `site.conf`  
- Automatische Erstellung aller Gateway-Konfigurationsdateien  
- Vollständig passwortloser, sicherer Datenbankzugriff  
- Keine Frameworks, keine Pi-Abhängigkeit – läuft auf jedem Debian-System

---

## ⚙️ Architektur

<img src="flowchart.png" alt="Systemübersicht" width="200">

---

## 🧠 Backend – Log-Monitor & Datenbank

Das Hauptprogramm überwacht kontinuierlich folgende Logdateien:

- `/var/log/mmdvm/MMDVM-YYYY-MM-DD.log`  
- `/var/log/mmdvm/YSFGateway-YYYY-MM-DD.log`  
- `/var/log/mmdvm/DMRGateway-YYYY-MM-DD.log`

Neue Einträge werden sofort erkannt, ausgewertet und in die Datenbank geschrieben.

### Erfasste Informationen

- TX-Aktivitäten und Rufzeichen für D-Star, DMR und System Fusion  
- Dauer und BER jeder Übertragung  
- Aktueller Betriebsmodus  
- Reflektorstatus für D-Star, Fusion und DMR  
- Automatische Erkennung von Logrotation und Trunkierung  
- Automatischer Reconnect nach Datenbankfehlern  

### Datenbanktabellen

| Tabelle | Beschreibung |
|--------|--------------|
| `status` | Aktueller Status (Modus, Rufzeichen, RF/NET, Dauer, BER) |
| `lastheard` | Jede Übertragung mit Zeitstempel |
| `reflector` | Aktueller Reflektor pro Modus |
| `config_inbox` | Konfiguration |

### Besondere Merkmale

- Erkennung unterbrochener Übertragungen (heuristische Zeitmessung)  
- Rufzeichenprüfung (min. 3 Zeichen, mindestens 1 Ziffer)  
- D-Star speichert keine DG-ID, Fusion schon  
- „Watchdog expired“-Meldungen gelten als EOT  
- DMR-Masternamen (z. B. `BM_2621_Germany`) werden automatisch erkannt  

---

### Duplex

Es gibt zwei Arten von MMDVM-HATs:

Simplex: die meisten MMDVM-Boards sind „Simplex“-Boards mit einer oder zwei Antennen  
Duplex: spezielle MMDVM-Boards, z. B. „Repeater Builder“-Boards

Die Einstellung „Duplex“ muss zur verwendeten Hardware passen, sonst funktioniert der Betrieb nur in eine Richtung. Meist wird „0“ für Eigenbau-Stationen verwendet.

---

## 🧰 Installation & Abhängigkeiten

Die Installation erfolgt vollständig automatisiert über **Shell-Skripte**, die alle Abhängigkeiten, Programme und Konfigurationsdateien installieren.

Zuerst das Repository von GitHub herunterladen:

```bash
sudo apt update
sudo apt install git -y
git clone https://github.com/dj0abr/OpenDVM.git
cd OpenDVM
```

Jetzt die Skripte (alle mit sudo) in folgender Reihenfolge ausführen:

### Installationsreihenfolge

👉 **Wichtig:**  
Diese Skripte müssen **in dieser Reihenfolge** ausgeführt werden.

1. **NUR für Raspberry PI mit MMDVM HAT**
   - Überspringe diesen Abschnitt, wenn du **keinen Raspberry Pi** verwendest oder dein MMDVM **per USB** angeschlossen ist. In diesem Fall fahre direkt fort mit **2. Serielle Schnittstelle installieren**.
   - Wenn du einen **Raspberry Pi mit direkt aufgestecktem MMDVM HAT** besitzt, musst du die **interne serielle Schnittstelle aktivieren**.
   Führe dazu folgendes Skript aus und starte anschließend neu:
   ```bash
   sudo ./install_raspi.sh
   sudo reboot
   ```
   - Nach dem Neustart fahre fort mit **2. Serielle Schnittstelle installieren**.

2. **Serielle Schnittstelle installieren**  
   - Skript ausführen:
   ```bash
   sudo ./install_serial.sh
   ```
   - Erkennt dein serielles Gerät (USB, Onboard-UART usw.), lässt dich das richtige auswählen  
   - Kann erneut ausgeführt werden, um ein anderes Gerät zu wählen (z. B. neue Hardware). In den meisten Fällen kann das angezeigte Default-Device einfach mit ENTER übernommen werden.

3. **MMDVM-System und alle Gateways installieren**  
   ```bash
   sudo ./install.sh
   ```
   - Installiert alle Systemabhängigkeiten  
   - Bereitet Verzeichnisse vor (z. B. `/var/log/mmdvm`)  
   - Richtet die MariaDB-Datenbank ein  
   - Kompiliert und installiert das C++-Backend  
   - Installiert das zentrale DV-Interface **MMDVMHost**
   - Installiert und konfiguriert das **System Fusion Gateway**
   - Installiert und konfiguriert das **D-Star Gateway**
   - Installiert und konfiguriert das **DMR Gateway**

Nach Abschluss werden **Standardkonfigurationsdateien** automatisch nach `/etc` kopiert.  
Diese müssen anschließend an die eigene Umgebung angepasst werden – siehe [Konfiguration](#-konfiguration).

---

## 🧾 Konfiguration

Alle standort- und systembezogenen Parameter für die G4KLX-Module werden in folgenden Dateien gespeichert:

   /etc/MMDVMHost.ini  
   /etc/ircddbgateway  
   /etc/ysfgateway  
   /etc/dmrgateway

Beispieldateien sind im Paket enthalten und müssen für die eigene Station angepasst werden.

Die wichtigsten Einstellungen können im SETUP Fenster vorgenommen werden:

### Schritte

1. **GUI im Browser öffnen:**
   ```
   IP-Adresse des Boards im Browser eingeben.  
   Den „SETUP“-Button oben rechts suchen.  
   „SETUP“ anklicken.
   ```

2. **Konfiguration bearbeiten:**

   Die mindestens erforderlichen Einstellungen sind:

   * Dein Rufzeichen  
   * Deine DMR-ID (falls nötig, auf den Link unter dem DMR-ID-Feld klicken, um die DMR-Datenbank zu öffnen)  
   * Duplex = 0 (nur auf 1 setzen, wenn du ein Repeater-Board nutzt)  
   * RX- und TX-Frequenzen einstellen. Es wird dringend empfohlen, **unterschiedliche RX- und TX-Frequenzen** zu verwenden, sonst können Probleme mit älteren D-Star-Geräten auftreten.  
   * Dein Brandmeister-Passwort eingeben (wie in *SelfCare* auf dem BM-Dashboard gesetzt).  
   * Das Konfigurationspasswort eingeben (Standard: `setuppassword`). Eigenes Passwort kann in `save_config.php` (zu finden im Verzeichnis `./gui/html`, muss nach dem Editieren nach /var/www/html kopiert werden) gesetzt werden.  
   * Auf **SAVE** klicken.

   Wenn eine grüne Bestätigungsmeldung erscheint, wurde die Konfiguration erfolgreich gespeichert.

Die generierten Konfigurationsdateien können bei Bedarf manuell angepasst werden – in der Regel ist das aber nicht nötig.

5. **Neustart:**
   ```bash
   sudo reboot
   ```
   Nach dem Neustart ist das System betriebsbereit.

   Bitte gib den Reflektoren nach dem ersten Neustart etwas Zeit, um sich zu verbinden – das kann einige Minuten dauern.

---

## 🌐 Web-Frontend

Das Web-Frontend zeigt alle Betriebsdaten in Echtzeit an.  
Komplett statisch – kein PHP-Framework erforderlich, nur eine kleine `api.php` für JSON-Ausgaben.

### Funktionen

- Live-Status: Modus, Rufzeichen, Dauer, BER, RF/NET  
- Farbige Statusfelder und Länderflaggen  
- Reflektorstatus für D-Star, DMR, Fusion  
- „Last Heard“-Liste mit Rufzeichen, Zeitstempel, Dauer  
- Aktivitätsdiagramm (48h, RF/NET getrennt)  
- Balkenstatistiken und 30-Tage-Heatmap  
- Responsives dunkles UI  
- Einzige externe Bibliothek: **Chart.js**

### Technologie

- Reines Vanilla JavaScript  
- CSS Grid Layout  
- Sekündliche Aktualisierung per AJAX  
- Funktioniert mit jedem Webserver (nginx, Apache, lighttpd)

---

## 🧱 Systemeinrichtung & Wartung

- Datenbank läuft über Unix-Socket  
- Installationsskripte erstellen Benutzer und Rechte automatisch  

---

## 🎯 Danksagung & Lizenz

- Jonathan Naylor G4KLX für seine herausragenden DV-Implementierungen, die die Grundlage dieses Projekts bilden  
- Diese Software steht unter der **GPL v2** und ist in erster Linie für den Amateurfunk und Bildungszwecke gedacht.  
  Dieses Projekt enthält Komponenten von G4KLX, die unter GPL v2 lizenziert sind.  
  Daher bleibt das Gesamtwerk unter GPL v2.
