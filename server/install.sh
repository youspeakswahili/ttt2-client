#!/bin/bash
# Installs or updates ttt2d on Ubuntu (run as root, next to the ttt2d binary):
#   bash install.sh
# - the program in /usr/local/bin/ttt2d, its data (players, matches, certificate) in /var/lib/ttt2d
# - runs as its own user "ttt2d", started by systemd at boot and restarted if it stops
# - firewall: SSH, 443/tcp (clients) and 27620/udp (hole punching + relay) open, everything else closed
# Running it again updates the program and keeps all data.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
if [ "$(id -u)" -ne 0 ]; then echo "Run this as root (ssh root@<server>)."; exit 1; fi
if [ ! -f "$HERE/ttt2d" ]; then echo "ttt2d is not next to install.sh."; exit 1; fi

echo "== Installing ttt2d"
id ttt2d >/dev/null 2>&1 || useradd --system --home /var/lib/ttt2d --shell /usr/sbin/nologin ttt2d
install -d -o ttt2d -g ttt2d -m 700 /var/lib/ttt2d
systemctl stop ttt2d 2>/dev/null || true
install -m 755 "$HERE/ttt2d" /usr/local/bin/ttt2d

cat > /etc/systemd/system/ttt2d.service <<'EOF'
[Unit]
Description=TTT2 matchmaking and relay server
After=network-online.target
Wants=network-online.target

[Service]
User=ttt2d
Group=ttt2d
ExecStart=/usr/local/bin/ttt2d -listen :443 -udp :27620 -data /var/lib/ttt2d
Restart=always
RestartSec=2
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/ttt2d
PrivateTmp=true
PrivateDevices=true
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
EOF

echo "== Firewall"
if ! command -v ufw >/dev/null 2>&1; then apt-get update -qq && apt-get install -y -qq ufw; fi
ufw allow OpenSSH >/dev/null
ufw allow 443/tcp >/dev/null
ufw allow 27620/udp >/dev/null
ufw default deny incoming >/dev/null
ufw default allow outgoing >/dev/null
ufw --force enable >/dev/null
ufw status | sed 's/^/   /'

echo "== Starting"
systemctl daemon-reload
systemctl enable --now ttt2d >/dev/null
sleep 2
if ! systemctl is-active --quiet ttt2d; then
  echo "ttt2d did not start. Its log:"; journalctl -u ttt2d -n 30 --no-pager; exit 1
fi
FP=$(sudo -u ttt2d /usr/local/bin/ttt2d -data /var/lib/ttt2d -fingerprint)
IP=$(curl -4 -s --max-time 5 https://api.ipify.org || hostname -I | awk '{print $1}')
echo
echo "ttt2d $(/usr/local/bin/ttt2d -version | awk '{print $2}') is running."
echo "  Server address : $IP"
echo "  Fingerprint    : $FP"
echo
echo "Send both lines above to set up the client. Back up /var/lib/ttt2d (players + certificate)."
