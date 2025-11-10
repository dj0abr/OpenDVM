#!/usr/bin/env bash

# (c) DJ0ABR
# Unified installer for G4KLX stack on Debian x86_64 (MMDVMHost, YSFGateway, ircDDBGateway, DMRGateway)
# Goals: idempotent, fail-fast, minimal duplication, small functions, pauses between blocks for debugging.
# Usage: sudo ./install.sh [--no-pause]

set -euo pipefail
set -o errtrace
umask 022

# Better diagnostics on failure
trap 'code=$?; cmd=${BASH_COMMAND:-unknown}; echo "[ERROR] Exit $code while running: $cmd" 1>&2; exit $code' ERR

# ----------------------------- helpers --------------------------------------
log() { printf "[+] %s\n" "$*"; }
warn() { printf "[!] %s\n" "$*" 1>&2; }
die() { printf "[-] %s\n" "$*" 1>&2; exit 1; }

PAUSE=0
DEBUG=0
for arg in "$@"; do
  [[ "$arg" == "--no-pause" ]] && PAUSE=0
  [[ "$arg" == "--debug" ]] && DEBUG=1
done
[[ $DEBUG -eq 1 ]] && set -x

pause_block() {
  (( PAUSE == 0 )) && return 0
  read -r -p $'Press ENTER to continue this install block (Ctrl+C to abort)...\n' _
}

need_root() { [[ $(id -u) -eq 0 ]] || die "Please run as root"; }

jobs_detect() { command -v nproc >/dev/null && nproc || echo 1; }

# Copy file only if content differs; then set mode/owner if provided
install_file() {
  local src="$1" dst="$2" mode="${3:-}" owner="${4:-}"
  [[ -f "$src" ]] || die "Missing file: $src"
  install -d -m 755 "$(dirname "$dst")"
  if [[ -f "$dst" ]] && cmp -s "$src" "$dst"; then
    :
  else
    install -m 644 "$src" "$dst"
  fi
  if [[ -n "$mode" ]]; then
    chmod "$mode" "$dst"
  fi
  if [[ -n "$owner" ]]; then
    chown "$owner" "$dst"
  fi
}

# calculate make jobs according to available ram
# required for raspberry pi with only 1GB ram
calc_jobs() {
  local desired="${1:-$(nproc)}"
  (( desired < 1 )) && desired=1

  local mem_kb
  if [[ -r /sys/fs/cgroup/memory.max ]]; then
    local max
    max=$(< /sys/fs/cgroup/memory.max)
    if [[ "$max" != "max" ]]; then
      mem_kb=$(( max / 1024 ))
    fi
  fi
  if [[ -z "${mem_kb:-}" && -r /sys/fs/cgroup/memory/memory.limit_in_bytes ]]; then
    mem_kb=$(( $(< /sys/fs/cgroup/memory/memory.limit_in_bytes) / 1024 ))
  fi
  if [[ -z "${mem_kb:-}" ]]; then
    mem_kb=$(awk '/MemTotal:/ {print $2}' /proc/meminfo)
  fi

  if   (( mem_kb <= 1048576 )); then
    echo 1
  elif (( mem_kb <= 2097152 )); then
    (( desired > 2 )) && echo 2 || echo "$desired"
  else
    echo "$desired"
  fi
}

# Generic git clone + make build inside temp dir; optional install step
build_from_git() {
  local repo="$1" make_dir="$2" make_path="$3" install_cmd="$4"
  local tmp; tmp="$(mktemp -d)"
  log "Cloning $repo into $tmp"
  pushd "$tmp" >/dev/null
  git clone --depth=1 "$repo"

  jobs="$(calc_jobs "${JOBS_DESIRED:-$(nproc)}")"
  echo "Building in $make_dir with -j${jobs}"
  make -C "$make_dir" -j"$jobs" ${make_path:+-f "$make_path"}

  #make -C "$make_dir" -j"$jobs" ${make_path:+-f "$make_path"}
  if [[ -n "$install_cmd" ]]; then
    log "Running install step"
    eval "$install_cmd"
  fi
  popd >/dev/null
  rm -rf "$tmp" || true
}
apt_update_once() {
  if [[ ! -f /var/lib/apt/periodic/update-success-stamp ]] || \
     find /var/lib/apt/periodic/update-success-stamp -mmin +60 >/dev/null 2>&1; then
    apt-get update
  fi
}

apt_install_batch() {
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "$@"
}

# --------------------------- base preparation -------------------------------
base_packages() {
  apt_update_once
  apt_install_batch \
    git build-essential cmake libusb-1.0-0-dev \
    libasound2-dev libfftw3-dev libgps-dev \
    libwxgtk3.2-dev logrotate curl ca-certificates \
    libmariadb-dev libmariadb-dev-compat mariadb-server \
    apache2 php libapache2-mod-php php-mysql wget
  apt-get autoremove -y || true
  apt-get clean || true
}

remove_modemmanager() {
  DEBIAN_FRONTEND=noninteractive apt-get purge -y modemmanager || true
}

ensure_user_mmdvm() {
  if ! id mmdvm >/dev/null 2>&1; then
    adduser --system --group --no-create-home --shell /usr/sbin/nologin mmdvm
  fi
  usermod -aG dialout mmdvm || true
  local sysctl; sysctl=$(command -v systemctl)
  local file="/etc/sudoers.d/mmdvm-opendvm"
  cat >"$file" <<EOF
Cmnd_Alias MMDVM_RESTARTS = \\
  $sysctl restart mmdvmhost.service, \\
  $sysctl restart ysfgateway.service, \\
  $sysctl restart dmrgateway.service, \\
  $sysctl restart ircddbgateway.service, \\
  $sysctl restart mmdvm-status.service, \\
  $sysctl restart mmdvm-DVconfig.service
mmdvm ALL=(root) NOPASSWD: MMDVM_RESTARTS
EOF
  chmod 440 "$file"
}

ensure_dirs() {
  install -d -m 755 /var/log/mmdvm
  chown -R mmdvm:mmdvm /var/log/mmdvm
  install -d -m 755 /usr/local/etc
  install -d -m 755 /var/log/ircddbgateway
  chown -R mmdvm:mmdvm /var/log/ircddbgateway
  install -d -m 755 /var/run/opendv
  chown mmdvm:mmdvm /var/run/opendv
  chmod 755 /var/run/opendv
}

check_serial_symlink() {
  if [[ ! -e /dev/mmdvm ]]; then
    die "/dev/mmdvm not found. Run ./scripts/install_serial.sh first."
  fi
}

# ------------------------------ components ----------------------------------
install_mmdvmhost() {
  log "Installing MMDVMHost..."
  build_from_git \
    "https://github.com/dj0abr/MMDVMHost.git" \
    "MMDVMHost" "" \
    "install -m 755 MMDVMHost/MMDVMHost /usr/local/bin/"
  install_file "configs/MMDVMHost.ini.sample" "/etc/MMDVMHost.ini" 664 "root:mmdvm"
  install_file "systemd/mmdvmhost.service" "/etc/systemd/system/mmdvmhost.service" 644
  log "MMDVMHost installed."
}

install_ysfgateway() {
  log "Installing YSFGateway..."
  log "Installing YSFGateway..."
  build_from_git \
    "https://github.com/dj0abr/YSFClients.git" \
    "YSFClients" "" \
    "install -m 755 YSFClients/YSFGateway/YSFGateway /usr/local/bin/"
  install_file "configs/ysfgateway.sample" "/etc/ysfgateway" 664 "root:mmdvm"
  install_file "systemd/ysfgateway.service" "/etc/systemd/system/ysfgateway.service" 644
  log "YSFGateway installed."
}


install_ircddbgateway() {
  log "Installing ircDDBGateway..."
  log "Installing ircDDBGateway..."
  local J; J=$(jobs_detect)
  build_from_git \
    "https://github.com/dj0abr/ircDDBGateway.git" \
    "ircDDBGateway" "" \
    "make -C ircDDBGateway install; install -m 755 /usr/bin/ircddbgatewayd /usr/local/bin/ || true"
  install_file "configs/ircddbgateway.sample" "/etc/ircddbgateway" 664 "root:mmdvm"
  install -d -m 755 /usr/share/ircddbgateway
  chown -R mmdvm:mmdvm /usr/share/ircddbgateway
  install_file "hosts/CCS_Hosts.txt" "/usr/share/ircddbgateway/CCS_Hosts.txt" 644
  install_file "systemd/ircddbgateway.service" "/etc/systemd/system/ircddbgateway.service" 644
  log "ircDDBGateway installed."
}


install_dmrgateway() {
  log "Installing DMRGateway..."
  log "Installing DMRGateway..."
  local J; J=$(jobs_detect)
  build_from_git \
    "https://github.com/dj0abr/DMRGateway.git" \
    "DMRGateway" "" \
    "make -C DMRGateway install"
  install_file "configs/dmrgateway.sample" "/etc/dmrgateway" 664 "root:mmdvm"
  install_file "systemd/dmrgateway.service" "/etc/systemd/system/dmrgateway.service" 644
  log "DMRGateway installed."
}


install_hosts_and_tables() {
  log "Installing host lists..."
  if [[ -d hosts ]]; then
    cp -a hosts/* /usr/local/etc/ || true
  fi
  log "Fetching DMRIds.dat and RSSI.dat..."
  wget -q -O /usr/local/etc/DMRIds.dat https://database.radioid.net/static/user.csv
  wget -q -O /usr/local/etc/RSSI.dat https://raw.githubusercontent.com/g4klx/MMDVMHost/master/RSSI.dat
  chown mmdvm:mmdvm /usr/local/etc/*.dat || true
  log "Hosts & tables installed."
}

install_logrotate_snippets() {
  log "Installing logrotate snippets..."
  install_file "configs/mmdvm-logrotate" "/etc/logrotate.d/mmdvm" 644
  install_file "configs/ircddb-logrotate" "/etc/logrotate.d/ircddb" 644
}

install_gui_and_parser() {
  log "Installing GUI & Parser..."
  cp -r gui/html/* /var/www/html
  ( cd gui && ./compile.sh )
  ( cd gui/parser && make clean && make )
  install_file "gui/mmdvm-status.service" "/etc/systemd/system/mmdvm-status.service" 644
  install_file "gui/mmdvm-DVconfig.service" "/etc/systemd/system/mmdvm-DVconfig.service" 644
  log "GUI & Parser installed."
}

setup_mariadb() {
  log "Initializing MariaDB..."
  # Ensure service is running (Debian: mariadb)
  systemctl enable --now mariadb || true
  # Feed SQL via heredoc to avoid set -e pitfalls with read -d ''
  mysql -u root --protocol=socket <<'EOSQL'
CREATE DATABASE IF NOT EXISTS mmdvmdb CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
CREATE USER IF NOT EXISTS 'mmdvm'@'localhost' IDENTIFIED VIA unix_socket;
GRANT ALL PRIVILEGES ON mmdvmdb.* TO 'mmdvm'@'localhost';
CREATE USER IF NOT EXISTS 'www-data'@'localhost' IDENTIFIED VIA unix_socket;
GRANT SELECT ON mmdvmdb.* TO 'www-data'@'localhost';
GRANT INSERT, UPDATE ON mmdvmdb.* TO 'www-data'@'localhost';
FLUSH PRIVILEGES;
EOSQL
  log "MariaDB initialized."
}

systemd_enable_all() {
  log "Reloading systemd daemon..."
  systemctl daemon-reload
  log "Enabling services..."
  systemctl enable mmdvmhost || true
  systemctl enable ysfgateway || true
  systemctl enable ircddbgateway || true
  systemctl enable dmrgateway || true
  systemctl enable mmdvm-status.service || true
  systemctl enable mmdvm-DVconfig.service || true
  log "Services enabled."
}

# ------------------------------- main flow -----------------------------------
main() {
  echo "PART ALL-IN-ONE"
  echo "================"

  need_root
  log "Preparing base system..."
  base_packages
  remove_modemmanager
  ensure_user_mmdvm
  ensure_dirs
  pause_block

  log "Checking serial link /dev/mmdvm..."
  check_serial_symlink
  pause_block

  install_mmdvmhost
  install_logrotate_snippets
  pause_block

  install_ysfgateway
  pause_block

  install_ircddbgateway
  pause_block

  install_dmrgateway
  pause_block

  install_hosts_and_tables
  pause_block

  install_gui_and_parser
  pause_block

  setup_mariadb
  pause_block

  systemd_enable_all

  echo
  echo "Installation complete," 
  echo "Reboot the System now." 
}

main "$@"
