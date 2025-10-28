#!/usr/bin/env bash

# This script installs G4KLX's ircDDBgateway software on x86 Debian.
# The goal is to have a system that is more stable than a Raspberry Pi running Pi-Star.

echo "PART 3"
echo "======"
echo "[*] Installing ircDDBgateway (system requirements have been installed during Part 1 - mmdvmhost) ..."

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

echo "[*] Create log directory ..."
install -d -m 755 /var/log/ircddbgateway
chown -R mmdvm:mmdvm /var/log/ircddbgateway

echo "[*] Create required directory ..."
mkdir -p /var/run/opendv
chown mmdvm:mmdvm /var/run/opendv
chmod 644 /var/run/opendv

echo "[*] Downloading G4KLX's ircDDBgateway ..."
# Create a unique temporary directory
TMP_BUILD="$(mktemp -d)"
# Delete $TMP_BUILD when the script exits
trap 'rm -rf "$TMP_BUILD"' EXIT
# Enter $TMP_BUILD
pushd "$TMP_BUILD" >/dev/null
# Download ircDDBgateway from GitHub
git clone https://github.com/dj0abr/ircDDBGateway.git
# Compile using all available CPU threads
J=$(command -v nproc >/dev/null && nproc || echo 1)
make -C ircDDBGateway -j"$J"
# Copy the built binary to /usr/local/bin
make -C ircDDBGateway install
install -m 755 /usr/bin/ircddbgatewayd /usr/local/bin/ || true
# Leave $TMP_BUILD
popd >/dev/null

echo "[*] Installing default configuration file (must be located in ./configs)..."
[ -f configs/ircddbgateway.sample ] || { echo "Error: configs/ircddbgateway.sample is missing"; exit 1; }
install -m 644 -D configs/ircddbgateway.sample /etc/ircddbgateway

echo "[*] Installing Host list (must be located in ./hosts)..."
[ -f hosts/CCS_Hosts.txt ] || { echo "Error: hosts/CCS_Hosts.txt is missing"; exit 1; }
install -d -m 755 /usr/share/ircddbgateway
chown -R mmdvm:mmdvm /usr/share/ircddbgateway
install -m 644 hosts/CCS_Hosts.txt /usr/share/ircddbgateway/CCS_Hosts.txt

echo "[*] Installing systemd service file (must be located in ./systemd)..." 
[ -f systemd/ircddbgateway.service ] || { echo "Error: systemd/ircddbgateway.service is missing"; exit 1; } 
install -m 644 systemd/ircddbgateway.service /etc/systemd/system/ircddbgateway.service

echo "[*] Setting up log rotation (default file must be located in ./configs)..."
[ -f configs/ircddb-logrotate ] || { echo "Error: configs/ircddb-logrotate is missing"; exit 1; }
install -m 644 configs/ircddb-logrotate /etc/logrotate.d/ircddb

echo "[*] Enabling the ircddbgateway service..."
systemctl daemon-reload
systemctl enable ircddbgateway

echo ""
echo "---> To start the service manually, run:  sudo systemctl restart ircddbgateway"
echo "     To check the service status:         sudo systemctl status ircddbgateway"
echo "     To view error logs:                  sudo journalctl -u ircddbgateway.service -f"
echo ""
echo "[*] Installation completed successfully!"
