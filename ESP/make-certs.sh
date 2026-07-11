#!/bin/sh
# Generate the TLS certificate the Vybes HTTPS listener serves.
#
# Uses mkcert (https://github.com/FiloSottile/mkcert, `brew install mkcert`):
# it maintains a local CA and signs a certificate for vybes.local. Trust that
# CA once per device and the browser treats https://vybes.local as a real
# secure origin - which is what unlocks microphone access for the analyzer
# on iPhones.
#
# Output lands in esp-web-server/data/certs/ (gitignored - the key is
# private to you). Upload it to the device with:  pio run -d ESP -t uploadfs
#
# To trust the CA on an iPhone:
#   1. Run `mkcert -CAROOT` to find rootCA.pem
#   2. AirDrop/email rootCA.pem to the phone and open it
#   3. Settings > General > VPN & Device Management > install the profile
#   4. Settings > General > About > Certificate Trust Settings > enable full trust

set -e
cd "$(dirname "$0")"

if ! command -v mkcert >/dev/null 2>&1; then
    echo "mkcert not found - install it first (macOS: brew install mkcert)" >&2
    exit 1
fi

CERT_DIR="esp-web-server/data/certs"
mkdir -p "$CERT_DIR"

# 192.168.4.1 is the standalone-AP address; add any static LAN IP you use.
mkcert -install
mkcert -cert-file "$CERT_DIR/server.crt" -key-file "$CERT_DIR/server.key" \
    vybes.local 192.168.4.1

echo
echo "Certificates written to $CERT_DIR/"
echo "Upload to the device:   pio run -d ESP -t uploadfs"
echo "CA root for phones:     $(mkcert -CAROOT)/rootCA.pem"
