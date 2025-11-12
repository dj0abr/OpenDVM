#!/usr/bin/env bash
set -e

# Root nötig
[ "$EUID" -ne 0 ] && { echo "Bitte mit sudo starten."; exit 1; }

# Boot-Pfad ermitteln (Bookworm=/boot/firmware, sonst /boot)
BOOT="/boot/firmware"; [ -d /boot ] && [ ! -d "$BOOT" ] && BOOT="/boot"
CFG="$BOOT/config.txt"
CMD="$BOOT/cmdline.txt"

# Backups (einmalig)
cp -n "$CFG" "$CFG.bak" 2>/dev/null || true
cp -n "$CMD" "$CMD.bak" 2>/dev/null || true

# UART aktiv + BT aus (generisches Overlay)
if grep -qE '^\s*enable_uart=' "$CFG"; then
  sed -i 's/^\s*enable_uart=.*/enable_uart=1/' "$CFG"
else
  printf "\n# MMDVM HAT\nenable_uart=1\n" >> "$CFG"
fi
grep -qE '^\s*dtoverlay=disable-bt' "$CFG" || printf "dtoverlay=disable-bt\n" >> "$CFG"

# Serielle Konsole aus cmdline entfernen (console=… / kgdboc=…)
sed -i -E 's/\s*console=(serial0|ttyAMA0|ttyS0),[0-9]+//g; s/\s*kgdboc=(serial0|ttyAMA0|ttyS0),[0-9]+//g' "$CMD"
# Mehrfache Spaces aufräumen
sed -i -E 's/\s+/ /g; s/^ //; s/ $//' "$CMD"

# Getty auf seriellen Ports stoppen/deaktivieren (falls aktiv)
systemctl disable --now serial-getty@ttyAMA0.service 2>/dev/null || true
systemctl disable --now serial-getty@ttyS0.service 2>/dev/null || true
systemctl disable --now serial-getty@serial0.service 2>/dev/null || true

echo "Ready. Please reboot NOW!"
