#!/usr/bin/env bash

# This script installs G4KLX's DMRGateway software on x86 Debian.
# The goal is to have a system that is more stable than a Raspberry Pi running Pi-Star.

echo "PART 4"
echo "======"
echo "[*] Installing DMRGateway (system requirements have been installed during Part 1 - mmdvmhost) ..."

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

echo "[*] Downloading G4KLX's DMRGateway ..."
# Create a unique temporary directory
TMP_BUILD="$(mktemp -d)"
# Delete $TMP_BUILD when the script exits
trap 'rm -rf "$TMP_BUILD"' EXIT
# Enter $TMP_BUILD
pushd "$TMP_BUILD" >/dev/null
# Download YSFGateway from GitHub
git clone --depth=1 https://github.com/dj0abr/DMRGateway.git
# Compile using all available CPU threads
J=$(command -v nproc >/dev/null && nproc || echo 1)
make -C DMRGateway -j"$J"
# Copy the built binary to /usr/local/bin
sudo make -C DMRGateway install
# Leave $TMP_BUILD
popd >/dev/null

echo "[*] Installing default configuration file (must be located in ./configs)..."
[ -f configs/dmrgateway.sample ] || { echo "Error: configs/dmrgateway.sample is missing"; exit 1; }
install -m 644 configs/dmrgateway.sample /etc/dmrgateway

echo "[*] Installing systemd service file (must be located in ./systemd)..." 
[ -f systemd/dmrgateway.service ] || { echo "Error: systemd/dmrgateway.service is missing"; exit 1; }
install -m 644 systemd/dmrgateway.service /etc/systemd/system/dmrgateway.service

echo "[*] Enabling the DMRGateway service..."
systemctl daemon-reload
systemctl enable dmrgateway

echo ""
echo "---> To start the service manually, run:  sudo systemctl restart dmrgateway"
echo "     To check the service status:         sudo systemctl status dmrgateway"
echo "     To view error logs:                  sudo journalctl -u dmrgateway.service"
echo ""
echo "[*] Installation completed successfully!"
