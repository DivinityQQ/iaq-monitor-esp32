#!/usr/bin/env bash
set -euo pipefail

# Generate an ECDSA P-256 self-signed certificate with SANs suitable for the ESP32 web portal.
# Defaults:
#   - Hosts: iaq-monitor.local, iaq-monitor, iaq-monitor.lan
#   - IPs:   192.168.4.1
# Output (default):
#   - www/cert.pem (public cert)
#   - www/key.pem  (private key)
#
# Optional:
#   --embed writes directly to firmware-embedded files so they persist across
#   /www rebuilds:
#     - components/web_portal/certs/servercert.pem
#     - components/web_portal/certs/prvtkey.pem
#
# Usage:
#   ./generate_cert.sh [--hosts host1,host2,...] [--ips ip1,ip2,...] [--days N] [--out DIR] [--embed]
#
# Examples:
#   ./generate_cert.sh
#   ./generate_cert.sh --hosts iaq-monitor.local,my-iaq.local --ips 192.168.4.1,192.168.1.50
#   ./generate_cert.sh --days 3650 --out ../../..//www
#   ./generate_cert.sh --hosts iaq-monitor.local --ips 192.168.0.117 --embed

ROOT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
OUT_DIR="$ROOT_DIR/www"
DAYS=3650
HOSTS="iaq-monitor.local,iaq-monitor,iaq-monitor.lan"
IPS="192.168.4.1"
EMBED_TARGET="no"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --hosts)
      HOSTS="$2"; shift 2;;
    --ips)
      IPS="$2"; shift 2;;
    --days)
      DAYS="$2"; shift 2;;
    --out)
      OUT_DIR="$2"; shift 2;;
    --embed)
      EMBED_TARGET="yes"; shift 1;;
    -h|--help)
      echo "Usage: $0 [--hosts host1,host2] [--ips ip1,ip2] [--days N] [--out DIR] [--embed]"; exit 0;;
    *)
      echo "Unknown arg: $1"; exit 1;;
  esac
done

if [[ "$EMBED_TARGET" == "yes" ]]; then
  OUT_DIR="$ROOT_DIR/components/web_portal/certs"
fi

mkdir -p "$OUT_DIR"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

# Build SAN config dynamically
SAN_CFG="$TMP_DIR/san.cnf"
{
  echo "[req]"
  echo "prompt = no"
  echo "default_md = sha256"
  echo "x509_extensions = v3_req"
  echo "distinguished_name = dn"
  echo "[dn]"
  # Use first host as CN (cosmetic; browsers use SAN instead)
  FIRST_HOST="${HOSTS%%,*}"
  echo "CN = ${FIRST_HOST}"
  echo "[v3_req]"
  echo "basicConstraints = CA:FALSE"
  echo "keyUsage = digitalSignature"
  echo "extendedKeyUsage = serverAuth"
  echo "subjectAltName = @alt_names"
  echo "[alt_names]"
  I=1
  IFS=',' read -r -a HOST_ARR <<< "$HOSTS"
  for h in "${HOST_ARR[@]}"; do
    h_trim="${h## }"; h_trim="${h_trim%% }"
    [[ -z "$h_trim" ]] && continue
    echo "DNS.${I} = ${h_trim}"
    I=$((I+1))
  done
  J=1
  IFS=',' read -r -a IP_ARR <<< "$IPS"
  for ip in "${IP_ARR[@]}"; do
    ip_trim="${ip## }"; ip_trim="${ip_trim%% }"
    [[ -z "$ip_trim" ]] && continue
    echo "IP.${J} = ${ip_trim}"
    J=$((J+1))
  done
} > "$SAN_CFG"

if [[ "$EMBED_TARGET" == "yes" ]]; then
  KEY_PATH="$OUT_DIR/prvtkey.pem"
  CRT_PATH="$OUT_DIR/servercert.pem"
else
  KEY_PATH="$OUT_DIR/key.pem"
  CRT_PATH="$OUT_DIR/cert.pem"
fi

echo "Generating ECDSA P-256 private key → $KEY_PATH"
openssl ecparam -genkey -name prime256v1 -noout -out "$KEY_PATH"

echo "Generating self-signed certificate (SANs) → $CRT_PATH"
openssl req -new -x509 -days "$DAYS" -key "$KEY_PATH" -out "$CRT_PATH" -config "$SAN_CFG" -extensions v3_req

chmod 600 "$KEY_PATH"
echo "Done. Outputs:"
echo "  cert: $CRT_PATH"
echo "  key : $KEY_PATH"
if [[ "$EMBED_TARGET" == "yes" ]]; then
  echo "These are the embedded defaults used when /www/cert.pem is absent."
else
  echo "Place these in the LittleFS image (/www) to override embedded certs."
  echo "Tip: when syncing frontend build to /www, exclude cert.pem/key.pem to keep them:"
  echo "  rsync -a --delete --exclude=cert.pem --exclude=key.pem dist/ www/"
fi
