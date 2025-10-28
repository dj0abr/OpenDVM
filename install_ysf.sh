#!/usr/bin/env bash

# This script installs G4KLX's YSFGateway software on x86 Debian.
# The goal is to have a system that is more stable than a Raspberry Pi running Pi-Star.

echo "PART 2"
echo "======"
echo "[*] Installing YSFGateway (system requirements have been installed during Part 1 - mmdvmhost) ..."

# Add safety to this script
# Exit on error
# Exit on unset variables
# Check exit status of piped commands
set -euo pipefail

# Set default file mask
umask 022

echo "[*] Checking root..."
[ "$(id -u)" -eq 0 ] || { echo "Please run as root"; exit 1; }

# check if PART1 has been installed
if ! id mmdvm >/dev/null 2>&1; then
  echo "Error: user 'mmdvm' missing. Run Part-1: install_mm.sh first."
  exit 1
fi

echo "[*] Downloading G4KLX's YSFGateway ..."
# Create a unique temporary directory
TMP_BUILD="$(mktemp -d)"
# Delete $TMP_BUILD when the script exits
trap 'rm -rf "$TMP_BUILD"' EXIT
# Enter $TMP_BUILD
pushd "$TMP_BUILD" >/dev/null
# Download YSFGateway from GitHub
git clone --depth=1 https://github.com/dj0abr/YSFClients.git
# Compile using all available CPU threads
J=$(command -v nproc >/dev/null && nproc || echo 1)
make -C YSFClients -j"$J"
# Copy the built binary to /usr/local/bin
install -m 755 YSFClients/YSFGateway/YSFGateway /usr/local/bin/
# Leave $TMP_BUILD
popd >/dev/null

echo "[*] Installing default configuration file (must be located in ./configs)..."
[ -f configs/ysfgateway.sample ] || { echo "Error: configs/ysfgateway.sample is missing"; exit 1; }
install -m 644 -D configs/ysfgateway.sample /etc/ysfgateway

echo "[*] Installing systemd service file (must be located in ./systemd)..." 
[ -f systemd/ysfgateway.service ] || { echo "Error: systemd/ysfgateway.service is missing"; exit 1; }
install -m 644 systemd/ysfgateway.service /etc/systemd/system/ysfgateway.service

echo "[*] Enabling the YSFGateway service..."
systemctl daemon-reload
systemctl enable ysfgateway

echo ""
echo "---> To start the service manually, run:  sudo systemctl restart ysfgateway"
echo "     To check the service status:         sudo systemctl status ysfgateway"
echo "     To view error logs:                  sudo journalctl -u ysfgateway.service"
echo ""
echo "[*] Installation completed successfully!"
