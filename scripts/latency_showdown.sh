#!/usr/bin/env bash
# =============================================================================
#  latency_showdown.sh   --   Die Leitdemo von Tag 1, 11:45
#
#  Misst Latenz in vier Szenarien und speichert die Ergebnisse:
#     1. ohne Last
#     2. unter CPU-Last
#     3. unter CPU + I/O + Speicher-Last
#     4. (nur RT) mit SCHED_FIFO und hoher Prioritaet
#
#  ---------------------------------------------------------------------------
#  DER ENTSCHEIDENDE PUNKT, DEN DIE MEISTEN TRAINER VERPASSEN:
#
#  Ein Standardkernel OHNE Last sieht hervorragend aus. Wer cyclictest auf
#  einem unbelasteten System laufen laesst und daraus schliesst, er brauche
#  kein PREEMPT_RT, hat das Experiment falsch aufgebaut.
#
#  Der Unterschied wird erst unter LAST sichtbar. Deshalb erzeugt dieses
#  Skript Last -- und deshalb ist Szenario 1 im Kurs die FALLE, die du
#  bewusst aufbaust, bevor du sie aufloest.
#  ---------------------------------------------------------------------------
#
#  Ablauf im Kurs:
#     Tag zuvor:  auf Standardkernel laufen lassen  -> ergebnisse/std_*.txt
#                 auf RT-Kernel laufen lassen       -> ergebnisse/rt_*.txt
#     Live 11:45: Ergebnisse vergleichen, dann EIN Szenario live wiederholen
#
#  Nutzung:
#      sudo ./latency_showdown.sh              # alle Szenarien, je 60 s
#      sudo ./latency_showdown.sh -d 30        # kuerzer
#      sudo ./latency_showdown.sh --vergleich  # gespeicherte Ergebnisse gegenueberstellen
# =============================================================================
set -uo pipefail

SKRIPT_VERSION="2026-08-20b"

DAUER=60
ERGDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/ergebnisse"
NUR_VERGLEICH=0

while [ $# -gt 0 ]; do
    case "$1" in
        -d|--dauer)   DAUER="$2"; shift 2 ;;
        --vergleich)  NUR_VERGLEICH=1; shift ;;
        -h|--help)    sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "Unbekannte Option: $1"; exit 1 ;;
    esac
done

GRN='\033[1;32m'; RED='\033[1;31m'; BLU='\033[1;34m'; YLW='\033[1;33m'; BLD='\033[1m'; NC='\033[0m'

mkdir -p "$ERGDIR"

# ---------------------------------------------------------- Max-Wert auslesen
max_aus_datei() {
    local datei="$1"
    [ -f "$datei" ] || { printf -- '-'; return; }
    local werte
    werte="$(grep -a 'Max Latencies' "$datei" | grep -oE '[0-9]+' | sort -n | tail -1)"
    if [ -n "$werte" ]; then printf '%s' "$((10#$werte))"; else printf -- '-'; fi
}

# --------------------------------------------------------------- Kernel erkennen
if uname -a | grep -q PREEMPT_RT; then
    KERNELTYP="rt";  KERNELLABEL="PREEMPT_RT"
else
    KERNELTYP="std"; KERNELLABEL="Standardkernel"
fi

# ------------------------------------------------------------------- Vergleich
if [ $NUR_VERGLEICH -eq 1 ]; then
    printf "${BLU}${BLD}\n"
    printf "================================================================\n"
    printf "   LATENCY SHOWDOWN -- Gegenueberstellung\n"
    printf "================================================================${NC}\n\n"
    printf "Skript-Version : %s\n" "$SKRIPT_VERSION"
    printf "Ergebnis-Ordner: %s\n\n" "$ERGDIR"

    if [ ! -d "$ERGDIR" ]; then
        printf "${RED}Ordner existiert nicht.${NC}\n"
        printf "Das Skript sucht immer NEBEN SICH SELBST. Liegt dieses Skript\n"
        printf "im selben Verzeichnis wie beim Messen? Gefundene Ergebnisordner:\n"
        find "$HOME" -maxdepth 3 -type d -name ergebnisse 2>/dev/null | sed 's/^/    /'
        exit 1
    fi

    ANZ="$(ls -1 "$ERGDIR"/*.txt 2>/dev/null | wc -l)"
    printf "Gefundene Messdateien: %s\n" "$ANZ"
    if [ "$ANZ" -eq 0 ]; then
        printf "\n${RED}Dieser Ordner ist leer.${NC}\n"
        printf "${YLW}Haeufigste Ursache: Das Skript liegt in einem ANDEREN Verzeichnis\n"
        printf "als beim Messen. Es sucht immer neben sich selbst -- und legt einen\n"
        printf "leeren Ordner an, wenn keiner da ist.${NC}\n\n"
        printf "Andere Ergebnisordner auf diesem System:\n"
        find "$HOME" -maxdepth 4 -type d -name ergebnisse 2>/dev/null \
            | while read -r d; do printf "    %-40s (%s Dateien)\n" "$d" "$(ls -1 "$d"/*.txt 2>/dev/null | wc -l)"; done
        printf "\nLoesung: Skript dorthin kopieren, oder Ergebnisse hierher verschieben.\n\n"
    fi
    if [ "$ANZ" -gt 0 ]; then
        for f in "$ERGDIR"/*.txt; do
            n="$(grep -ac 'Max Latencies' "$f" 2>/dev/null)"; n="${n:-0}"
            if [ "$n" -gt 0 ]; then
                printf "  ${GRN}OK${NC}   %-24s (Max-Zeilen: %s)\n" "$(basename "$f")" "$n"
            else
                printf "  ${RED}LEER${NC} %-24s keine Zeile 'Max Latencies' enthalten\n" "$(basename "$f")"
            fi
        done
    fi
    echo
    printf "%-26s %14s %14s %8s\n" "Szenario" "Standard" "PREEMPT_RT" "Faktor"
    printf -- "--------------------------------------------------------------------\n"
    for sz in 1_ohne_last 2_cpu_last 3_volle_last 4_fifo; do
        max_std="$(max_aus_datei "$ERGDIR/std_${sz}.txt")"
        max_rt="$(max_aus_datei  "$ERGDIR/rt_${sz}.txt")"
        label="$(printf '%s' "$sz" | sed 's/^[0-9]_//; s/_/ /g')"
        faktor="-"
        if [ "$max_std" != "-" ] && [ "$max_rt" != "-" ] && [ "$max_rt" -gt 0 ]; then
            faktor="$(awk -v a="$max_std" -v b="$max_rt" 'BEGIN{printf "%.1fx", a/b}')"
        fi
        printf "%-26s %11s us %11s us %8s\n" "$label" "$max_std" "$max_rt" "$faktor"
    done
    printf -- "--------------------------------------------------------------------\n"
    if [ "$(max_aus_datei "$ERGDIR/std_3_volle_last.txt")" = "-" ]; then
        printf "\n${YLW}HINWEIS: Fuer den Standardkernel liegen noch keine Messungen vor.${NC}\n"
        printf "${YLW}         sudo ./switch-kernel.sh std   &&   sudo ./latency_showdown.sh${NC}\n"
    fi
    printf "\n${YLW}Lies die Zeile 'volle last'. Dort entscheidet sich alles.${NC}\n"
    printf "${YLW}Die Zeile 'ohne last' ist die Falle: dort sehen beide gut aus.${NC}\n\n"
    exit 0
fi

[ "$(id -u)" -eq 0 ] || { printf "${RED}Bitte mit sudo ausfuehren (cyclictest braucht Rechte).${NC}\n"; exit 1; }
command -v cyclictest >/dev/null || { printf "${RED}cyclictest fehlt: sudo apt install rt-tests${NC}\n"; exit 1; }

HAT_STRESS=0
command -v stress-ng >/dev/null && HAT_STRESS=1
[ $HAT_STRESS -eq 0 ] && printf "${YLW}WARNUNG: stress-ng fehlt. Lastszenarien sind ohne Aussagekraft!${NC}\n"

printf "${BLU}${BLD}\n"
printf "================================================================\n"
printf "   LATENCY SHOWDOWN\n"
printf "   Kernel : %s\n" "$(uname -r)"
printf "   Modell : %s\n" "$KERNELLABEL"
printf "   Dauer  : %s s pro Szenario (gesamt ca. %s Minuten)\n" "$DAUER" "$(( DAUER * 4 / 60 + 1 ))"
printf "================================================================${NC}\n"

# ------------------------------------------------------------------ Hilfsfunktion
lauf() {
    local id="$1" titel="$2" cyclictest_extra="$3" stress_cmd="$4"
    local datei="$ERGDIR/${KERNELTYP}_${id}.txt"

    printf "\n${BLU}${BLD}--- %s ---${NC}\n" "$titel"

    local stress_pid=""
    if [ -n "$stress_cmd" ] && [ $HAT_STRESS -eq 1 ]; then
        printf "    Last: %s\n" "$stress_cmd"
        eval "$stress_cmd" >/dev/null 2>&1 &
        stress_pid=$!
        sleep 3   # Last einschwingen lassen
    fi

    printf "    cyclictest laeuft %s s ...\n" "$DAUER"
    # -m mlockall | -S SMP-Modus (ein Thread pro Kern) | -p Prioritaet
    # -i 1000 us Intervall | -h 400 Histogramm bis 400 us | -q leise
    local out
    out="$(cyclictest -m -S -p 99 -i 1000 -h 400 -D "${DAUER}s" -q $cyclictest_extra 2>&1)"

    if [ -n "$stress_pid" ]; then
        kill "$stress_pid" 2>/dev/null || true
        pkill -f stress-ng 2>/dev/null || true
        sleep 1
    fi

    {
        echo "# Szenario : $titel"
        echo "# Kernel   : $(uname -r)  ($KERNELLABEL)"
        echo "# Datum    : $(date '+%Y-%m-%d %H:%M:%S')"
        echo "# Dauer    : ${DAUER}s"
        echo "# Last     : ${stress_cmd:-keine}"
        echo "$out"
    } > "$datei"

    local zusammenfassung
    zusammenfassung="$(printf '%s' "$out" | grep -E '^# (Min|Avg|Max) Latencies' || printf '%s' "$out" | tail -4)"
    printf '%s\n' "$zusammenfassung" | sed 's/^/    /'

    local maxval
    maxval="$(printf '%s' "$out" | grep -oE 'Max Latencies:.*' | grep -oE '[0-9]+' | sort -n | tail -1)"
    if [ -n "$maxval" ]; then
        if [ "$maxval" -lt 100 ];      then printf "    ${GRN}=> Max %s us${NC}\n" "$maxval"
        elif [ "$maxval" -lt 500 ];    then printf "    ${YLW}=> Max %s us${NC}\n" "$maxval"
        else                                printf "    ${RED}=> Max %s us${NC}\n" "$maxval"; fi
    fi
    printf "    gespeichert: %s\n" "$datei"
}

# ------------------------------------------------------------------ Szenarien
lauf "1_ohne_last" \
     "Szenario 1: OHNE LAST  (Achtung -- hier sieht auch der Standardkernel gut aus)" \
     "" ""

lauf "2_cpu_last" \
     "Szenario 2: CPU-LAST  (alle Kerne rechnen)" \
     "" "stress-ng --cpu $(nproc) --timeout $((DAUER + 10))s"

lauf "3_volle_last" \
     "Szenario 3: VOLLE LAST  (CPU + I/O + Speicher + Interrupts) -- DIE ENTSCHEIDENDE MESSUNG" \
     "" "stress-ng --cpu $(nproc) --io 4 --vm 2 --vm-bytes 128M --hdd 2 --timeout $((DAUER + 10))s"

lauf "4_fifo" \
     "Szenario 4: VOLLE LAST + Messthread auf isoliertem Kern" \
     "-a 3 -t 1" "stress-ng --cpu $(nproc) --io 4 --vm 2 --vm-bytes 128M --timeout $((DAUER + 10))s"

# ------------------------------------------------------------------ Abschluss
printf "\n${GRN}${BLD}Alle Szenarien abgeschlossen (%s).${NC}\n" "$KERNELLABEL"
printf "\nNaechster Schritt:\n"
if [ "$KERNELTYP" = "std" ]; then
    printf "    sudo ./switch-kernel.sh rt\n"
    printf "    sudo ./latency_showdown.sh -d %s\n" "$DAUER"
    printf "    sudo ./latency_showdown.sh --vergleich\n"
else
    printf "    sudo ./latency_showdown.sh --vergleich\n"
fi

# ------------------------------------------------------------------ Bonus
printf "\n${YLW}BONUS fuer den Kurs -- die modernen Werkzeuge:${NC}\n"
printf "Der RT-Defconfig hat CONFIG_TIMERLAT_TRACER und CONFIG_OSNOISE_TRACER aktiv.\n"
printf "Das kennen die wenigsten. Zwei Befehle, die im Kurs Eindruck machen:\n\n"
printf "    sudo rtla timerlat top -d 30s     # Timer-Latenz, live, mit IRQ/Thread-Aufschluesselung\n"
printf "    sudo rtla osnoise top -d 30s      # WOHIN die verlorene Zeit geht (Hardware-IRQ, NMI, Thread)\n\n"
printf "Der Unterschied zu cyclictest: cyclictest sagt DASS es ruckelt.\n"
printf "osnoise sagt WARUM. Das ist der Uebergang zu Tag 3.\n\n"
