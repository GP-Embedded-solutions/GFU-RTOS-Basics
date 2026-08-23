#!/usr/bin/env bash
# =============================================================================
#  provision-pi.sh   --   AUF DEM RASPBERRY PI ausfuehren (mit sudo)
#  Idempotent: mehrfaches Ausfuehren ist unschaedlich.
#
#  Richtet das Echtzeit-Labor ein: Werkzeuge, I2C/SPI, Benutzer, Tailscale.
# =============================================================================
set -Eeuo pipefail

BOOT=/boot/firmware
TEILNEHMER_USER=teilnehmer

GRN='\033[1;32m'; BLU='\033[1;34m'; YLW='\033[1;33m'; RED='\033[1;31m'; NC='\033[0m'
ok()  { printf "${GRN}[ OK ]${NC} %s\n" "$*"; }
inf() { printf "${BLU}[INFO]${NC} %s\n" "$*"; }
wrn() { printf "${YLW}[WARN]${NC} %s\n" "$*"; }
die() { printf "${RED}[FEHLER]${NC} %s\n" "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "Bitte mit sudo ausfuehren."

inf "Raspberry Pi Echtzeit-Labor: Provisioning startet."
inf "Aktueller Kernel: $(uname -r)"
echo

# ------------------------------------------------------------- 1. System-Update
inf "System aktualisieren (kann einige Minuten dauern) ..."
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get -y -qq upgrade
ok "System aktuell."

# ------------------------------------------------------------- 2. Pakete
inf "Installiere Werkzeuge ..."
PKGS=(
  # Echtzeit-Messung
  rt-tests
  # Tracing / Analyse
  trace-cmd linux-perf
  # Lastgenerierung fuer Productive Failure
  stress-ng
  # I2C / SPI
  i2c-tools python3-smbus
  # Build
  build-essential git cmake pkg-config
  # CPU / Topologie
  cpufrequtils hwloc util-linux
  # Auswertung
  python3-matplotlib python3-numpy
  # Komfort
  htop tmux vim curl jq bc
)
for p in "${PKGS[@]}"; do
    if apt-get -y -qq install "$p" >/dev/null 2>&1; then
        ok "  $p"
    else
        wrn "  $p konnte nicht installiert werden (Paketname ggf. abweichend) -- weiter."
    fi
done

# perf ist auf Pi OS haeufig unter anderem Namen erreichbar
if ! command -v perf >/dev/null 2>&1; then
    PERF_BIN="$(ls -1 /usr/bin/perf_* 2>/dev/null | head -1 || true)"
    if [ -n "$PERF_BIN" ]; then
        ln -sf "$PERF_BIN" /usr/local/bin/perf
        ok "perf verlinkt: $PERF_BIN -> /usr/local/bin/perf"
    else
        wrn "perf nicht gefunden. Alternative im Kurs: trace-cmd, osnoise, timerlat."
    fi
fi

# ------------------------------------------------------------- 3. I2C und SPI
inf "Aktiviere I2C und SPI ..."
CFG="$BOOT/config.txt"
[ -f "$CFG" ] || die "$CFG nicht gefunden."
cp "$CFG" "$CFG.backup-provision-$(date +%Y%m%d-%H%M%S)"

add_cfg() { grep -qxF "$1" "$CFG" || printf '%s\n' "$1" >> "$CFG"; }
add_cfg "dtparam=i2c_arm=on"
add_cfg "dtparam=spi=on"
# Kein dynamisches Uebertakten -> deterministischere Messungen
add_cfg "force_turbo=0"

# Module beim Boot laden
grep -qxF 'i2c-dev' /etc/modules || echo 'i2c-dev' >> /etc/modules
modprobe i2c-dev 2>/dev/null || true
ok "I2C/SPI konfiguriert (wirksam nach Reboot)."

# ------------------------------------------------------------- 4. Gruppen
for u in $(getent passwd | awk -F: '$3>=1000 && $3<65534 {print $1}'); do
    usermod -aG i2c,spi,gpio,dialout "$u" 2>/dev/null || true
done
ok "Benutzer den Hardware-Gruppen hinzugefuegt."

# ------------------------------------------------------------- 5. RT-Limits
inf "Setze Echtzeit-Limits (rtprio, memlock) ..."
cat > /etc/security/limits.d/99-realtime.conf <<'EOF'
# Erlaubt Benutzern der Gruppe 'realtime' SCHED_FIFO-Prioritaeten und
# das Sperren von Speicher (mlockall) ohne root.
@realtime   -   rtprio      99
@realtime   -   memlock     unlimited
@realtime   -   nice       -20
EOF
getent group realtime >/dev/null || groupadd realtime
for u in $(getent passwd | awk -F: '$3>=1000 && $3<65534 {print $1}'); do
    usermod -aG realtime "$u" 2>/dev/null || true
done
ok "Limits gesetzt (wirksam nach neuem Login)."

# ------------------------------------------------------------- 6. Zusammenfassung
echo
ok "Provisioning abgeschlossen."
cat <<EOF

Noch offen:
  1. REBOOT  (I2C/SPI und Limits werden erst dann wirksam)
  2. Danach:  i2cdetect -y 1      -> BME280 muss bei 0x76 oder 0x77 erscheinen
  3. Danach:  ./verify.sh
  4. RT-Kernel installieren:  sudo ./install-rt-kernel.sh

Jetzt neu starten?  ->  sudo reboot
EOF
