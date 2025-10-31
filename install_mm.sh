#!/usr/bin/env bash

# This script installs G4KLX's MMDVMHost software on x86 Debian.
# The goal is to have a system that is more stable than a Raspberry Pi running Pi-Star.

echo "PART 1"
echo "======"
echo "[*] Installing system requirements and MMDVMHost..."

# Add safety to this script
# Exit on error
# Exit on unset variables
# Check exit status of piped commands
set -euo pipefail

# Set default file mask
umask 022

echo "[*] Checking root..."
[ "$(id -u)" -eq 0 ] || { echo "Please run as root"; exit 1; }

# --- Serial device check (handled by install_serial.sh) -----------------------
echo "[*] Checking for /dev/mmdvm ..."
if [[ ! -e /dev/mmdvm ]]; then
  echo "[-] /dev/mmdvm not found."
  echo "    Please run ./install_serial.sh first to create a persistent symlink."
  exit 1
fi

echo "[*] Creating service user 'mmdvm'..."
if ! id mmdvm >/dev/null 2>&1; then
  adduser --system --group --no-create-home --shell /usr/sbin/nologin mmdvm
fi
usermod -aG dialout mmdvm || true

echo "[*] Installing dependencies..."
# Update package lists
apt-get update
# Install non-interactively
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  git build-essential cmake libusb-1.0-0-dev \
  libasound2-dev libfftw3-dev libgps-dev \
  libwxgtk3.2-dev \
  logrotate curl ca-certificates libmariadb-dev mariadb-server apache2 php libapache2-mod-php php php-mysql wget

apt-get autoremove -y || true
apt-get clean || true

echo "[*] Removing ModemManager (if present)..."
# "|| true" ensures success even if nothing was removed
DEBIAN_FRONTEND=noninteractive apt-get purge -y modemmanager || true

echo "[*] Creating required directories..."
# Log directory
mkdir -p /var/log/mmdvm
chown -R mmdvm:mmdvm /var/log/mmdvm
chmod 0755 /var/log/mmdvm

# Directory for various files (reflector tables, etc.)
install -d -m 755 /usr/local/etc

echo "[*] Downloading G4KLX's MMDVMHost..."
# Create a unique temporary directory
TMP_BUILD="$(mktemp -d)"
# Delete $TMP_BUILD when the script exits
trap 'rm -rf "$TMP_BUILD"' EXIT
# Enter $TMP_BUILD
pushd "$TMP_BUILD" >/dev/null
# Download MMDVMHost from GitHub
git clone --depth=1 https://github.com/dj0abr/MMDVMHost.git
# Compile using all available CPU threads
J=$(command -v nproc >/dev/null && nproc || echo 1)
make -C MMDVMHost -j"$J"
# Copy the built binary to /usr/local/bin
install -m 755 MMDVMHost/MMDVMHost /usr/local/bin/
# Leave $TMP_BUILD
popd >/dev/null

echo "[*] Installing default configuration file (must be located in ./configs)..."
[ -f configs/MMDVMHost.ini.sample ] || { echo "Error: configs/MMDVMHost.ini.sample is missing"; exit 1; }
install -m 644 configs/MMDVMHost.ini.sample /etc/MMDVMHost.ini

echo "[*] Installing systemd service file (must be located in ./systemd)..."
[ -f systemd/mmdvmhost.service ] || { echo "Error: systemd/mmdvmhost.service is missing"; exit 1; }
install -m 644 systemd/mmdvmhost.service /etc/systemd/system/mmdvmhost.service

echo "[*] Installing host files (reflector lists)..."
if [ -d hosts ]; then
  cp -a hosts/* /usr/local/etc/ || true
fi

echo "[*] Install user table and RSSI table ..."
wget -O /usr/local/etc/DMRIds.dat https://database.radioid.net/static/user.csv
wget -O /usr/local/etc/RSSI.dat https://raw.githubusercontent.com/g4klx/MMDVMHost/master/RSSI.dat
chown mmdvm:mmdvm /usr/local/etc/*.dat

echo "[*] Setting up log rotation (default file must be located in ./configs)..."
[ -f configs/mmdvm-logrotate ] || { echo "Error: configs/mmdvm-logrotate is missing"; exit 1; }
install -m 644 configs/mmdvm-logrotate /etc/logrotate.d/mmdvm

echo "[*] Setup Config Parser ..."
cd configs
./compile.sh
cd ..

echo "[*] Setup GUI ..."
cp -r gui/html/* /var/www/html
cd gui
./compile.sh
cd ..
[ -f gui/mmdvm-status.service ] || { echo "Error: gui/mmdvm-status.service is missing"; exit 1; }
install -m 644 gui/mmdvm-status.service /etc/systemd/system/mmdvm-status.service

SQL_COMMANDS="
-- Datenbank anlegen, falls noch nicht vorhanden
CREATE DATABASE IF NOT EXISTS mmdvmdb
  CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
-- C++-Dienst (läuft als Linux-User mmdvm)
CREATE USER IF NOT EXISTS 'mmdvm'@'localhost' IDENTIFIED VIA unix_socket;
GRANT ALL PRIVILEGES ON mmdvmdb.* TO 'mmdvm'@'localhost';
-- PHP (läuft als www-data)
CREATE USER IF NOT EXISTS 'www-data'@'localhost' IDENTIFIED VIA unix_socket;
GRANT SELECT ON mmdvmdb.* TO 'www-data'@'localhost';
FLUSH PRIVILEGES;
"
echo "→ initializing MariaDB for MMDVM..."
mysql -u root --protocol=socket -e "${SQL_COMMANDS}"
echo "[*] Done."

echo "[*] Enabling the MMDVMHost service..."
systemctl daemon-reload
systemctl enable mmdvmhost
systemctl enable mmdvm-status.service

echo ""
echo "reboot, or check manually:"
echo "---> To start the service manually, run:  sudo systemctl restart mmdvmhost"
echo "     To check the service status:         sudo systemctl status mmdvmhost"
echo "     To view error logs:                  sudo journalctl -u mmdvmhost.service"
echo "---> To start the service manually, run:  sudo systemctl restart mmdvm-status"
echo "     To check the service status:         sudo systemctl status mmdvm-status"
echo "     To view error logs:                  sudo journalctl -u mmdvm-status.service"
echo ""
echo "[*] Installation completed successfully!"
