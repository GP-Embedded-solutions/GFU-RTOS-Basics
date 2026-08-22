#!/usr/bin/env bash
# =============================================================================
#  switch-kernel.sh   --   Umschaltung zwischen Standard- und PREEMPT_RT-Kernel
#
#  Nutzung:
#      sudo ./switch-kernel.sh rt        PREEMPT_RT aktivieren + Reboot
#      sudo ./switch-kernel.sh std       Standardkernel aktivieren + Reboot
#      ./switch-kernel.sh status         Aktuellen Zustand anzeigen
#      sudo ./switch-kernel.sh rt --no-reboot
#
#  Zusatzoption fuer Tag 2 (CPU-Isolierung):
#      sudo ./switch-kernel.sh isolate on    # isolcpus=2,3 nohz_full=2,3 ...
#      sudo ./switch-kernel.sh isolate off
# =============================================================================
set -Eeuo pipefail

BOOT=/boot/firmware
CFG="$BOOT/config.txt"
CMDLINE="$BOOT/cmdline.txt"

RED='\033[1;31m'; GRN='\033[1;32m'; BLU='\033[1;34m'; YLW='\033[1;33m'; NC='\033[0m'
ok()  { printf "${GRN}[ OK ]${NC} %s\n" "$*"; }
inf() { printf "${BLU}[INFO]${NC} %s\n" "$*"; }
wrn() { printf "${YLW}[WARN]${NC} %s\n" "$*"; }
die() { printf "${RED}[FEHLER]${NC} %s\n" "$*" >&2; exit 1; }

ISOL_PARAMS="isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3 irqaffinity=0,1"

show_status() {
    echo
    printf "${BLU}=== Kernel-Status ===${NC}\n"
    printf "  Laufender Kernel : %s\n" "$(uname -r)"
    if uname -a | grep -q 'PREEMPT_RT'; then
        printf "  Preemption-Modell: ${GRN}PREEMPT_RT (Echtzeit)${NC}\n"
    elif uname -a | grep -q 'PREEMPT_DYNAMIC'; then
        printf "  Preemption-Modell: PREEMPT_DYNAMIC (Standard)\n"
    else
        printf "  Preemption-Modell: PREEMPT (Standard)\n"
    fi
    if [ -r /sys/kernel/realtime ]; then
        printf "  /sys/kernel/realtime : %s\n" "$(cat /sys/kernel/realtime)"
    else
        printf "  /sys/kernel/realtime : nicht vorhanden (= kein RT-Kernel)\n"
    fi
    printf "  config.txt kernel=  : %s\n" \
        "$(grep -E '^\s*kernel=' "$CFG" 2>/dev/null | tail -1 | cut -d= -f2 || echo '(nicht gesetzt)')"
    if grep -q 'isolcpus=' "$CMDLINE" 2>/dev/null; then
        printf "  CPU-Isolierung      : ${GRN}aktiv${NC}  (%s)\n" \
            "$(tr ' ' '\n' < "$CMDLINE" | grep isolcpus= || true)"
    else
        printf "  CPU-Isolierung      : inaktiv\n"
    fi
    echo
    printf "  Verfuegbare Kernel in %s:\n" "$BOOT"
    ls -1 "$BOOT"/kernel8*.img 2>/dev/null | sed 's|^|    |' || echo "    (keine gefunden)"
    echo
}

set_kernel() {
    local img="$1" label="$2"
    [ "$(id -u)" -eq 0 ] || die "Bitte mit sudo ausfuehren."
    [ -f "$BOOT/$img" ] || die "$BOOT/$img nicht gefunden. Erst install-rt-kernel.sh ausfuehren."

    cp "$CFG" "$CFG.bak"
    if grep -qE '^\s*kernel=' "$CFG"; then
        sed -i -E "s|^\s*kernel=.*|kernel=${img}|" "$CFG"
    else
        printf '\nkernel=%s\n' "$img" >> "$CFG"
    fi
    grep -qE '^\s*arm_64bit=1' "$CFG" || printf 'arm_64bit=1\n' >> "$CFG"
    sync
    ok "Naechster Boot: ${label}  (${img})"
}

set_isolation() {
    local mode="$1"
    [ "$(id -u)" -eq 0 ] || die "Bitte mit sudo ausfuehren."
    cp "$CMDLINE" "$CMDLINE.bak"
    local line; line="$(tr -d '\n' < "$CMDLINE")"
    for p in isolcpus nohz_full rcu_nocbs irqaffinity; do
        line="$(printf '%s' "$line" | sed -E "s/(^| )${p}=[^ ]*//g")"
    done
    line="$(printf '%s' "$line" | tr -s ' ' | sed 's/^ //;s/ $//')"
    if [ "$mode" = "on" ]; then
        line="${line} ${ISOL_PARAMS}"
        ok "CPU-Isolierung aktiviert: ${ISOL_PARAMS}"
    else
        ok "CPU-Isolierung entfernt."
    fi
    printf '%s\n' "$line" > "$CMDLINE"
    sync
    inf "Aktuelle cmdline.txt:"
    sed 's|^|    |' "$CMDLINE"
}

do_reboot() {
    if [ "${NO_REBOOT:-0}" = "1" ]; then
        wrn "Reboot uebersprungen (--no-reboot). Aenderung wird erst nach Neustart wirksam."
        return
    fi
    echo
    inf "Neustart in 5 Sekunden ... (Strg+C zum Abbrechen)"
    sleep 5
    systemctl reboot
}

# ------------------------------------------------------------------ Argumente
ACTION="${1:-status}"
[ "${2:-}" = "--no-reboot" ] && NO_REBOOT=1

case "$ACTION" in
    rt)
        RT_IMG="$(ls -1 "$BOOT"/kernel8-rt*.img 2>/dev/null | head -1 | xargs -r basename)"
        [ -n "$RT_IMG" ] || die "Kein RT-Kernel in $BOOT gefunden."
        set_kernel "$RT_IMG" "PREEMPT_RT"
        do_reboot
        ;;
    std|standard)
        set_kernel "kernel8-std.img" "Standardkernel"
        do_reboot
        ;;
    isolate)
        case "${2:-}" in
            on)  set_isolation on ;;
            off) set_isolation off ;;
            *)   die "Nutzung: sudo $0 isolate {on|off}" ;;
        esac
        NO_REBOOT="${NO_REBOOT:-0}"
        do_reboot
        ;;
    status)
        show_status
        ;;
    *)
        cat <<EOF
Nutzung:
  sudo $0 rt [--no-reboot]        PREEMPT_RT-Kernel aktivieren
  sudo $0 std [--no-reboot]       Standardkernel aktivieren
  sudo $0 isolate {on|off}        CPU-Isolierung (isolcpus=2,3) schalten
       $0 status                  Aktuellen Zustand anzeigen
EOF
        exit 1
        ;;
esac
