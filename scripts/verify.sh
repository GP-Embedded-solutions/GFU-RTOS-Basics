#!/usr/bin/env bash
# =============================================================================
#  verify.sh   --   Verifikation der kompletten Kette vor Tag 1
#
#  Prueft 14 Punkte: Boot -> Kernel -> Werkzeuge -> I2C -> Sensor -> Messung
#  Schreibt ein Protokoll nach ~/verify-report-<zeitstempel>.txt
#
#  Nutzung:  ./verify.sh          (normal)
#            ./verify.sh --quick  (ohne cyclictest-Lauf, ~10 s)
#
#  Regel: Solange dieses Protokoll nicht auf BEIDEN Kerneln gruen ist,
#         wird nichts anderes gebaut.
# =============================================================================
set -uo pipefail

QUICK=0
[ "${1:-}" = "--quick" ] && QUICK=1

REPORT="$HOME/verify-report-$(date +%Y%m%d-%H%M%S).txt"
PASS=0; FAIL=0; WARN=0

GRN='\033[1;32m'; RED='\033[1;31m'; YLW='\033[1;33m'; BLU='\033[1;34m'; NC='\033[0m'

log() { printf '%s\n' "$*" >> "$REPORT"; }

check() {  # check "Beschreibung" "befehl" ["erwartetes muster"]
    local desc="$1" cmd="$2" pattern="${3:-}"
    local out rc
    out="$(eval "$cmd" 2>&1)"; rc=$?
    if [ $rc -eq 0 ] && { [ -z "$pattern" ] || printf '%s' "$out" | grep -qE "$pattern"; }; then
        printf "${GRN}  PASS${NC}  %s\n" "$desc"
        log "PASS  $desc"
        [ -n "$out" ] && log "      -> $(printf '%s' "$out" | head -3 | tr '\n' '|')"
        PASS=$((PASS+1)); return 0
    else
        printf "${RED}  FAIL${NC}  %s\n" "$desc"
        printf "        %s\n" "$(printf '%s' "$out" | head -2)"
        log "FAIL  $desc"
        log "      -> $(printf '%s' "$out" | head -3 | tr '\n' '|')"
        FAIL=$((FAIL+1)); return 1
    fi
}

warn_check() {
    local desc="$1" cmd="$2" pattern="${3:-}"
    local out; out="$(eval "$cmd" 2>&1)"
    if [ -z "$pattern" ] || printf '%s' "$out" | grep -qE "$pattern"; then
        printf "${GRN}  PASS${NC}  %s\n" "$desc"; log "PASS  $desc"; PASS=$((PASS+1))
    else
        printf "${YLW}  WARN${NC}  %s\n" "$desc"; log "WARN  $desc"; WARN=$((WARN+1))
    fi
}

echo
printf "${BLU}==============================================================${NC}\n"
printf "${BLU}  Echtzeit-Labor Verifikation${NC}\n"
printf "${BLU}  Host: %s   Datum: %s${NC}\n" "$(hostname)" "$(date '+%Y-%m-%d %H:%M:%S')"
printf "${BLU}==============================================================${NC}\n"
echo

log "=== Echtzeit-Labor Verifikationsprotokoll ==="
log "Host:   $(hostname)"
log "Datum:  $(date '+%Y-%m-%d %H:%M:%S')"
log "Kernel: $(uname -a)"
log ""

# ---------------------------------------------------------- A. System
printf "${BLU}A. System${NC}\n"
check "1.  Raspberry Pi 4 erkannt" \
      "cat /proc/device-tree/model | tr -d '\\0'" "Raspberry Pi 4"
check "2.  64-bit Userspace (aarch64)" "uname -m" "aarch64"
check "3.  Debian Trixie oder neuer" "cat /etc/os-release" "trixie|VERSION_ID=\"1[3-9]\""
warn_check "4.  Mindestens 4 CPU-Kerne" "nproc" "^[4-9]|^[1-9][0-9]"
echo

# ---------------------------------------------------------- B. Kernel
printf "${BLU}B. Kernel und Preemption${NC}\n"
KREL="$(uname -r)"
printf "        Laufender Kernel: %s\n" "$KREL"

check "6.  Beide Kernel-Images vorhanden" \
      "ls /boot/firmware/kernel8.img /boot/firmware/kernel8-rt*.img" "kernel8-rt"
check "7.  arm_64bit=1 in config.txt gesetzt" \
      "grep -E '^\\s*arm_64bit=1' /boot/firmware/config.txt" "arm_64bit=1"
warn_check "8.  High-Resolution-Timer aktiv" \
      "grep -c hrtimer /proc/timer_list || cat /proc/timer_list | head -5" "."
echo

# ---------------------------------------------------------- C. Werkzeuge
printf "${BLU}C. Messwerkzeuge${NC}\n"
check "9. cyclictest verfuegbar" "command -v cyclictest" "cyclictest"
warn_check "10. trace-cmd verfuegbar" "command -v trace-cmd" "trace-cmd"
warn_check "11. stress-ng verfuegbar" "command -v stress-ng" "stress-ng"
warn_check "12. osnoise/timerlat-Tracer im Kernel" \
      "cat /sys/kernel/tracing/available_tracers 2>/dev/null || sudo cat /sys/kernel/debug/tracing/available_tracers 2>/dev/null" \
      "timerlat|osnoise"
echo

# ---------------------------------------------------------- D. Hardware
printf "${BLU}D. Hardware-Anbindung${NC}\n"
check "13. I2C-Bus /dev/i2c-1 vorhanden" "ls /dev/i2c-1" "i2c-1"

SENSOR_ADDR=""
if command -v i2cdetect >/dev/null 2>&1; then
    SCAN="$(i2cdetect -y 1 2>/dev/null || sudo i2cdetect -y 1 2>/dev/null || true)"
    if printf '%s' "$SCAN" | grep -qE '(^|[[:space:]])76([[:space:]]|$)'; then SENSOR_ADDR=0x76; fi
    if printf '%s' "$SCAN" | grep -qE '(^|[[:space:]])77([[:space:]]|$)'; then SENSOR_ADDR=0x77; fi
fi

if [ -n "$SENSOR_ADDR" ]; then
    printf "${GRN}  PASS${NC}  14. BME280 gefunden auf %s\n" "$SENSOR_ADDR"
    log "PASS  14. BME280 auf $SENSOR_ADDR"; PASS=$((PASS+1))
    CHIPID="$( (i2cget -y 1 "$SENSOR_ADDR" 0xD0 2>/dev/null || sudo i2cget -y 1 "$SENSOR_ADDR" 0xD0 2>/dev/null) || true)"
    case "$CHIPID" in
        0x60) printf "${GRN}  PASS${NC}  16. Chip-ID 0x60 = BME280 bestaetigt\n"; PASS=$((PASS+1)); log "PASS  16. Chip-ID 0x60 (BME280)";;
        0x58) printf "${YLW}  WARN${NC}  16. Chip-ID 0x58 = BMP280 (kein Feuchtesensor) -- funktioniert, Luftfeuchte entfaellt\n"; WARN=$((WARN+1)); log "WARN  16. BMP280 statt BME280";;
        *)    printf "${YLW}  WARN${NC}  16. Unerwartete Chip-ID: %s\n" "$CHIPID"; WARN=$((WARN+1)); log "WARN  16. Chip-ID $CHIPID";;
    esac
else
    printf "${RED}  FAIL${NC}  14. Kein BME280 auf dem I2C-Bus gefunden\n"
    printf "        Pruefen: Verkabelung (3V3/GND/SDA=Pin3/SCL=Pin5), dtparam=i2c_arm=on, Reboot\n"
    log "FAIL  14. Kein Sensor gefunden"; FAIL=$((FAIL+1))
fi
echo

# ---------------------------------------------------------- E. Messung
printf "${BLU}E. Latenzmessung${NC}\n"
if [ $QUICK -eq 1 ]; then
    printf "${YLW}  SKIP${NC}  15. cyclictest (--quick gesetzt)\n"; log "SKIP  15. cyclictest"
elif command -v cyclictest >/dev/null 2>&1; then
    printf "        cyclictest laeuft 20 Sekunden ...\n"
    CT="$(sudo cyclictest -m -S -p 80 -i 1000 -D 20s -q 2>&1 | tail -6 || true)"
    printf '%s\n' "$CT" | sed 's/^/        /'
    log "cyclictest-Ergebnis:"; log "$CT"
    MAXVAL="$(printf '%s' "$CT" | grep -oE 'Max:[[:space:]]*[0-9]+' | grep -oE '[0-9]+' | sort -n | tail -1)"
    if [ -n "$MAXVAL" ]; then
        printf "${GRN}  PASS${NC}  15. cyclictest lief durch (hoechster Max-Wert: %s us)\n" "$MAXVAL"
        PASS=$((PASS+1)); log "PASS  15. cyclictest Max=${MAXVAL}us"
    else
        printf "${RED}  FAIL${NC}  15. cyclictest lieferte kein auswertbares Ergebnis\n"
        FAIL=$((FAIL+1)); log "FAIL  15. cyclictest ohne Ergebnis"
    fi
else
    printf "${RED}  FAIL${NC}  15. cyclictest nicht installiert\n"; FAIL=$((FAIL+1))
fi
echo

# ---------------------------------------------------------- F. Netz
printf "${BLU}F. Fernzugriff${NC}\n"
warn_check "16. SSH-Dienst laeuft" "systemctl is-active ssh || systemctl is-active sshd" "active"
echo

# ---------------------------------------------------------- Bilanz
printf "${BLU}==============================================================${NC}\n"
printf "  Ergebnis:  ${GRN}%d PASS${NC}   ${YLW}%d WARN${NC}   ${RED}%d FAIL${NC}\n" "$PASS" "$WARN" "$FAIL"
if [ $IS_RT -eq 1 ]; then
    printf "  Getestet auf: ${GRN}PREEMPT_RT-Kernel${NC}\n"
else
    printf "  Getestet auf: Standardkernel\n"
    printf "  ${YLW}-> Diesen Test noch einmal nach 'sudo ./switch-kernel.sh rt' fahren.${NC}\n"
fi
printf "  Protokoll: %s\n" "$REPORT"
printf "${BLU}==============================================================${NC}\n"
echo

log ""
log "BILANZ: $PASS PASS, $WARN WARN, $FAIL FAIL"

[ $FAIL -eq 0 ] || exit 1
