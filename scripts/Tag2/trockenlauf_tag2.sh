#!/usr/bin/env bash
# trockenlauf_tag2.sh — Der Trockenlauf, der ueber Tag 2 entscheidet.
#
# Zweck: In einem Durchlauf nachweisen, dass die Kette von Tag 2 haelt, UND
#        dabei die echten Zahlen erzeugen, die spaeter auf die Folien gehoeren.
#
# Der Ablauf ist ZWEIPHASIG, weil dazwischen ein Reboot liegt:
#
#   sudo ./trockenlauf_tag2.sh phase1     # Basismessung OHNE Isolierung
#   sudo ./switch-kernel.sh isolate on && sudo reboot
#   sudo ./trockenlauf_tag2.sh phase2     # Messung MIT Isolierung + Vergleich
#
# Ergebnis: ./messprotokoll_tag2.md  — am Kurstag danebenlegen.
set -u

PROTO="${PROTO:-./messprotokoll_tag2.md}"
CORE="${CORE:-3}"
DAUER="${DAUER:-60}"          # Sekunden je cyclictest-Lauf
STATE="./.trockenlauf_tag2.state"

say(){ printf "\n\033[1m>> %s\033[0m\n" "$1"; }
note(){ printf "   %s\n" "$1"; }
p(){ printf '%s\n' "$*" >> "$PROTO"; }

need_root(){ [ "$(id -u)" -eq 0 ] || { echo "Bitte mit sudo starten."; exit 1; }; }

have(){ command -v "$1" >/dev/null 2>&1; }

# --- cyclictest-Lauf, gibt "min avg max" zurueck -------------------------
messung(){   # $1 = Beschriftung, $2 = zusaetzliche Last ja/nein, $3 = cpu-arg
    local label="$1" last="$2" cpuarg="${3:-}"
    local lastpid=""
    if [ "$last" = "last" ]; then
        if have stress-ng; then
            stress-ng --cpu 4 --io 2 --vm 1 --vm-bytes 128M --timeout $((DAUER+5))s \
                >/dev/null 2>&1 & lastpid=$!
        else
            note "stress-ng fehlt -> Ersatzlast per dd/yes"
            ( timeout $((DAUER+5)) sh -c 'while :; do :; done' ) & lastpid=$!
        fi
        sleep 2
    fi
    local out
    out=$(cyclictest -m -S -p 99 -i 1000 -D "${DAUER}s" -q $cpuarg 2>/dev/null | tail -5)
    [ -n "$lastpid" ] && { kill "$lastpid" 2>/dev/null; wait "$lastpid" 2>/dev/null; }
    local mx av mn
    mx=$(echo "$out" | grep -o 'Max:[ ]*[0-9]*' | awk '{print $2}' | sort -n | tail -1)
    av=$(echo "$out" | grep -o 'Avg:[ ]*[0-9]*' | awk '{print $2}' | sort -n | tail -1)
    mn=$(echo "$out" | grep -o 'Min:[ ]*[0-9]*' | awk '{print $2}' | sort -n | head -1)
    printf "   %-38s min %-6s avg %-6s MAX %s us\n" "$label" "${mn:-?}" "${av:-?}" "${mx:-?}"
    p "| $label | ${mn:-?} | ${av:-?} | **${mx:-?}** |"
    echo "${mx:-0}"
}

# ------------------------------------------------------------------ PHASE 1
phase1(){
    need_root
    have cyclictest || { echo "FEHLER: cyclictest fehlt (Paket rt-tests)."; exit 1; }

    : > "$PROTO"
    p "# Messprotokoll Tag 2 — Echtzeit-Linux"
    p ""
    p "Erzeugt von \`trockenlauf_tag2.sh\` am $(date '+%F %T') auf \`$(uname -nr)\`."
    p "Kernel-Cmdline: \`$(cat /proc/cmdline)\`"
    p ""
    p "| Messung | Min (us) | Avg (us) | Max (us) |"
    p "|---|---|---|---|"

    say "PHASE 1 — Basismessung OHNE Isolierung  (dauert ca. $((DAUER*2/60+1)) Minuten)"
    note "Governor auf performance setzen, damit die Messung vergleichbar ist."
    for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        [ -w "$g" ] && echo performance > "$g" 2>/dev/null
    done

    M1=$(messung "RT-Kernel, keine Isolierung, Leerlauf" nolast | tail -1)
    M2=$(messung "RT-Kernel, keine Isolierung, unter Last" last | tail -1)

    echo "phase1_idle=$M1"  > "$STATE"
    echo "phase1_load=$M2" >> "$STATE"

    say "PHASE 1 fertig."
    note "Jetzt:  sudo ./switch-kernel.sh isolate on   &&   sudo reboot"
    note "Danach: sudo ./trockenlauf_tag2.sh phase2"
    note "Falls switch-kernel.sh nicht greift: isolcpus=$CORE nohz_full=$CORE rcu_nocbs=$CORE"
    note "manuell an cmdline.txt anhaengen (eine Zeile, Leerzeichen-getrennt!)."
}

# ------------------------------------------------------------------ PHASE 2
phase2(){
    need_root
    [ -f "$STATE" ] || { echo "FEHLER: Phase 1 fehlt. Erst 'phase1' laufen lassen."; exit 1; }
    # shellcheck disable=SC1090
    . "$STATE"

    say "PHASE 2 — Isolierung pruefen"
    if [ -x ./isolation_check.sh ]; then ./isolation_check.sh "$CORE"; else
        note "isolation_check.sh nicht gefunden - manuell pruefen:"
        note "  cat /sys/devices/system/cpu/isolated"
    fi

    p ""
    p "## Nach der Isolierung (Kern $CORE)"
    p ""
    p "| Messung | Min (us) | Avg (us) | Max (us) |"
    p "|---|---|---|---|"

    say "Messung MIT Isolierung, unter Last"
    M3=$(messung "Isolierter Kern $CORE, unter Last" last "-a $CORE" | tail -1)

    say "Gegenprobe: derselbe Kernel, aber Messung auf einem NICHT isolierten Kern"
    M4=$(messung "Nicht-isolierter Kern 0, unter Last" last "-a 0" | tail -1)

    say "Lernprojekt Stufe 2 im Zusammenspiel"
    if [ -x ./stage1_reference ]; then
        note "stage1_reference --fifo 80 --mlock --cpu $CORE laeuft 30 s ..."
        ./stage1_reference --fifo 80 --mlock --cpu "$CORE" --csv jitter_isoliert.csv &
        SP=$!; sleep 30; kill $SP 2>/dev/null; wait $SP 2>/dev/null
        if [ -f jitter_isoliert.csv ]; then
            note "CSV geschrieben: $(wc -l < jitter_isoliert.csv) Zeilen"
            p ""
            p "Lernprojekt Stufe 2 auf isoliertem Kern: \`jitter_isoliert.csv\`, $(wc -l < jitter_isoliert.csv) Messpunkte."
        else
            note "WARNUNG: keine CSV - Parameter oder Sensor pruefen."
        fi
    else
        note "WARNUNG: stage1_reference nicht gefunden. Ohne diesen Test ist der"
        note "         Slot 15:00 (Lernprojekt Stufe 2) UNGEPRUEFT."
        p ""
        p "> **Offen:** Zusammenspiel \`isolate on\` + \`stage1_reference --cpu $CORE\` wurde nicht getestet."
    fi

    say "Prioritaetsinversions-Demo"
    if [ -x ./selbsttest_pi_demo.sh ]; then ./selbsttest_pi_demo.sh --cpu "$CORE"
    else note "selbsttest_pi_demo.sh nicht gefunden."; fi

    # --- Urteil ---------------------------------------------------------
    p ""
    p "## Urteil"
    p ""
    p "- Ohne Isolierung, Leerlauf: **${phase1_idle} us** max"
    p "- Ohne Isolierung, unter Last: **${phase1_load} us** max"
    p "- Mit Isolierung, unter Last: **${M3} us** max"
    p "- Nicht-isolierter Kern, unter Last: **${M4} us** max"
    p ""

    say "ERGEBNIS"
    note "ohne Isolierung / Last : ${phase1_load} us"
    note "mit  Isolierung / Last : ${M3} us"
    if awk -v a="${phase1_load:-0}" -v b="${M3:-0}" 'BEGIN{exit !(a>b && a-b>20)}'; then
        note "-> Die Isolierung wirkt messbar. Diese Zahlen gehoeren auf die Folie."
        p "**Isolierung wirkt messbar.** Diese Zahlen im Kurs live wiederholen, nicht behaupten."
    else
        note "-> Kein klarer Gewinn. ACHTUNG: dann NICHT behaupten, Isolierung helfe immer."
        note "   Ehrliche Variante im Kurs: 'Auf diesem Board bringt es X - das ist der"
        note "   Punkt: gemessen wird, nicht geglaubt.'"
        p "**Kein klarer Gewinn gemessen.** Im Kurs offen benennen und als Lehrstueck nutzen:"
        p "Isolierung ist kein Automatismus, sie muss nachgewiesen werden."
    fi
    say "Protokoll geschrieben: $PROTO"
}

case "${1:-}" in
    phase1) phase1 ;;
    phase2) phase2 ;;
    *) sed -n '2,20p' "$0"; exit 1 ;;
esac
