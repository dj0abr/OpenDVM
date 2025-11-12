#!/usr/bin/env bash
# RPi 5: GPIO-UART für HAT auf GPIO 14/15 einrichten
set -e

# Root nötig
if [ "$EUID" -ne 0 ]; then
  echo "start wih sudo"
  exit 1
fi

# Prüfen, ob wirklich ein Raspberry Pi 5
if ! grep -q "Raspberry Pi 5" /proc/device-tree/model 2>/dev/null; then
  echo "This script runs on Raspberry 5 ONLY."
  exit 1
fi

# Boot-Pfad (Bookworm: /boot/firmware)
BOOT="/boot/firmware"
if [ -d /boot ] && [ ! -d "$BOOT" ]; then
  BOOT="/boot"
fi
CFG="$BOOT/config.txt"
CMD="$BOOT/cmdline.txt"

# Backups anlegen (nur wenn noch nicht vorhanden)
cp -n "$CFG" "$CFG.bak" 2>/dev/null || true
cp -n "$CMD" "$CMD.bak" 2>/dev/null || true

echo "-> config.txt: activate UART"
if grep -qE '^\s*enable_uart=' "$CFG"; then
  sed -i 's/^\s*enable_uart=.*/enable_uart=1/' "$CFG"
else
  printf "\n# HAT UART\nenable_uart=1\n" >> "$CFG"
fi

# Vorhandene uart0-Overlays bereinigen, dann korrekt setzen
echo "-> config.txt: connect UART0 to GPIO 14/15"
sed -i -E '/^\s*dtoverlay=uart0(-pi5)?(,.*)?\s*$/d' "$CFG"
sed -i -E '/^\s*dtoverlay=uart0(,.*)?\s*$/d' "$CFG"
printf "dtoverlay=uart0-pi5\n" >> "$CFG"
# Alternativ explizit pins setzen (würde das obige ersetzen):
# printf "dtoverlay=uart0,txd0_pin=14,rxd0_pin=15\n" >> "$CFG"

# Serielle Konsole aus cmdline entfernen (serial0 / ttyAMA* / ttyS*)
echo "-> cmdline.txt: remove serial console"
sed -i -E 's/\s*console=(serial0|ttyAMA[0-9]+|ttyS[0-9]+),[0-9]+//g; s/\s*kgdboc=(serial0|ttyAMA[0-9]+|ttyS[0-9]+),[0-9]+//g' "$CMD"
# Whitespace säubern
sed -i -E 's/\s+/ /g; s/^ //; s/ $//' "$CMD"

# Gettys stoppen/deaktivieren, falls aktiv
echo "-> systemd: deactivate gettys on serial ports"
systemctl disable --now serial-getty@ttyAMA0.service 2>/dev/null || true   # GPIO-UART
systemctl disable --now serial-getty@serial0.service 2>/dev/null || true
systemctl disable --now serial-getty@ttyAMA10.service 2>/dev/null || true  # Debug-UART (Pi 5)

echo
echo "Ready, Please reboot NOW"
