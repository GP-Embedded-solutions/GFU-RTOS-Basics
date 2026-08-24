#!/usr/bin/env bash
# isolation_check.sh — Beweist objektiv, ob die CPU-Isolierung wirklich greift.
#
# Warum das noetig ist: `isolcpus=3` in cmdline.txt zu schreiben und einen
# Reboot zu machen heisst NICHT, dass Kern 3 sauber ist. Fehlt nohz_full oder
# rcu_nocbs, laufen Timer-Ticks und RCU-Callbacks weiter. Genau dieser Punkt
# wird in Kursen behauptet statt gezeigt.
#
#   ./isolation_check.sh [KERN]     (Vorgabe: 3)
#
# Ausgabe: pro Kriterium GRUEN/GELB/ROT + am Ende ein Gesamturteil.
set -u
CORE="${1:-3}"
ok=0; warn=0; bad=0

g(){ printf "  \033[32mGRUEN\033[0m  %s\n" "$1"; ok=$((ok+1)); }
y(){ printf "  \033[33mGELB \033[0m  %s\n" "$1"; warn=$((warn+1)); }
r(){ printf "  \033[31mROT  \033[0m  %s\n" "$1"; bad=$((bad+1)); }
h(){ printf "\n\033[1m%s\033[0m\n" "$1"; }

echo "==================================================================="
echo " Isolationspruefung fuer Kern $CORE   ($(date '+%F %T'))"
echo "==================================================================="

h "1) Boot-Parameter"
CMD="$(cat /proc/cmdline)"
echo "  $CMD" | fold -w 66 -s | sed 's/^/    /'
case "$CMD" in *"isolcpus="*) g "isolcpus gesetzt";; *) r "isolcpus FEHLT - der Rest ist dann Kosmetik";; esac
case "$CMD" in *"nohz_full="*) g "nohz_full gesetzt (Timer-Tick wird unterdrueckt)";;
               *) y "nohz_full fehlt - Tick laeuft weiter, ~1 Ereignis/ms auf dem Kern";; esac
case "$CMD" in *"rcu_nocbs="*) g "rcu_nocbs gesetzt (RCU-Callbacks ausgelagert)";;
               *) y "rcu_nocbs fehlt - RCU-Callbacks stoeren den isolierten Kern";; esac

h "2) Was der Kernel selbst meldet"
if [ -r /sys/devices/system/cpu/isolated ]; then
    ISO="$(cat /sys/devices/system/cpu/isolated)"
    echo "    /sys/devices/system/cpu/isolated = '${ISO:-<leer>}'"
    case ",$ISO," in *",$CORE,"*|"$CORE"*) g "Kern $CORE ist als isoliert gefuehrt";;
        *) [ -z "$ISO" ] && r "Kernel fuehrt KEINEN Kern als isoliert" || y "Kern $CORE nicht in der Liste";; esac
else
    y "sysfs-Knoten nicht lesbar (aelterer Kernel?)"
fi

h "3) Laeuft dort noch Arbeit? (Threads mit psr=$CORE)"
LIST="$(ps -eLo pid,psr,class,rtprio,comm --no-headers 2>/dev/null | awk -v c="$CORE" '$2==c')"
CNT="$(printf '%s\n' "$LIST" | grep -c . || true)"
KERNELTHREADS="$(printf '%s\n' "$LIST" | grep -Ec '\[|kworker|ksoftirqd|rcu|migration|idle_inject|cpuhp' || true)"
echo "$LIST" | head -15 | sed 's/^/    /'
if [ "$CNT" -le 6 ]; then g "nur $CNT Threads auf dem Kern (Rest sind unvermeidbare Kernel-Threads)"
elif [ "$CNT" -le 15 ]; then y "$CNT Threads auf dem Kern - das ist mehr als erwartet"
else r "$CNT Threads auf dem Kern - Isolierung wirkt nicht"; fi
[ "${KERNELTHREADS:-0}" -gt 4 ] && y "$KERNELTHREADS Kernel-Threads gebunden - per-CPU-Threads lassen sich nicht verschieben (normal)"

h "4) Interrupt-Verteilung"
if [ -r /proc/interrupts ]; then
    TOT="$(awk -v c=$((CORE+2)) 'NR>1 && $c ~ /^[0-9]+$/ {s+=$c} END{print s+0}' /proc/interrupts)"
    echo "    Summe IRQs auf Kern $CORE seit Boot: $TOT"
    echo "    (2x messen mit 10 s Abstand - die DIFFERENZ zaehlt, nicht der Absolutwert)"
    TOP="$(awk -v c=$((CORE+2)) 'NR>1 && $c ~ /^[0-9]+$/ && $c>0 {print $c, $NF}' /proc/interrupts | sort -rn | head -4)"
    [ -n "$TOP" ] && { echo "    Groesste Verursacher:"; echo "$TOP" | sed 's/^/      /'; }
    g "IRQ-Daten erhoben (Bewertung anhand der Differenzmessung)"
else
    r "/proc/interrupts nicht lesbar"
fi

h "5) Frequenz-Governor (der haeufigste Latenz-Killer)"
GOV="$(cat /sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_governor 2>/dev/null || echo 'n/v')"
echo "    Governor auf Kern $CORE: $GOV"
case "$GOV" in
    performance) g "performance - richtig fuer Messungen";;
    n/v)         y "kein cpufreq sichtbar (auf dem Pi 4 je nach Firmware normal)";;
    *)           r "'$GOV' - Frequenz skaliert waehrend der Messung. Erst umstellen, dann messen.";;
esac

h "6) Praxistest: laesst sich der Kern belegen?"
if command -v chrt >/dev/null && command -v taskset >/dev/null; then
    if taskset -c "$CORE" true 2>/dev/null; then g "taskset -c $CORE funktioniert"
    else r "taskset -c $CORE schlaegt fehl - Kern offline?"; fi
else
    y "chrt/taskset fehlen (Paket util-linux)"
fi

echo
echo "==================================================================="
printf " Ergebnis: %d gruen, %d gelb, %d rot\n" "$ok" "$warn" "$bad"
if [ "$bad" -eq 0 ] && [ "$warn" -le 2 ]; then
    echo " URTEIL: Kern $CORE ist kurstauglich isoliert."
elif [ "$bad" -eq 0 ]; then
    echo " URTEIL: brauchbar, aber benenne die gelben Punkte im Kurs offen."
    echo "         Ein Trainer, der Schwaechen selbst zeigt, wirkt staerker"
    echo "         als einer, der sie uebergeht."
else
    echo " URTEIL: NICHT als 'isoliert' verkaufen. Rote Punkte zuerst beheben."
fi
echo "==================================================================="
exit 0
