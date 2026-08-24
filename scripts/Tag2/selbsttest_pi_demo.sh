#!/usr/bin/env bash
# selbsttest_pi_demo.sh
# Prueft VOR dem Kurs, dass die Prioritaetsinversions-Demo auf DIESER Maschine
# den erwarteten Kontrast zeigt. Wenn dieser Test rot ist, zeig die Demo nicht.
#
#   sudo ./selbsttest_pi_demo.sh [--cpu N]
set -u

CPU_ARG=""
[ "${1:-}" = "--cpu" ] && CPU_ARG="--cpu $2"

BIN=./priority_inversion_demo
[ -x "$BIN" ] || { echo "FEHLER: $BIN nicht gebaut. 'make' ausfuehren."; exit 1; }
[ "$(id -u)" -eq 0 ] || { echo "FEHLER: bitte mit sudo starten (SCHED_FIFO)."; exit 1; }

blockzeit() {
    $BIN $CPU_ARG "$@" 2>/dev/null \
      | awk '/H war blockiert/ {print $5}'
}

echo "== Selbsttest Prioritaetsinversion =="
OHNE=$(blockzeit)
MIT=$(blockzeit --inherit)

echo "  ohne Vererbung : ${OHNE:-?} ms"
echo "  mit  Vererbung : ${MIT:-?} ms"

fail=0
awk -v a="${OHNE:-0}" 'BEGIN{exit !(a>650)}' || { echo "  ROT: ohne Vererbung < 650 ms - Inversion tritt nicht auf."; fail=1; }
awk -v b="${MIT:-9999}" 'BEGIN{exit !(b<560)}' || { echo "  ROT: mit Vererbung > 560 ms - Vererbung greift nicht."; fail=1; }
awk -v a="${OHNE:-0}" -v b="${MIT:-9999}" 'BEGIN{exit !(a-b>250)}' || { echo "  ROT: Kontrast < 250 ms - didaktisch zu schwach."; fail=1; }

if [ "$fail" -eq 0 ]; then
    echo "  GRUEN: Demo ist kurstauglich."
    echo
    echo "  Haeufigste Ursache fuer ROT auf dem Pi: die drei Threads landen auf"
    echo "  verschiedenen Kernen. Dann gibt es keine Verdraengung und damit keine"
    echo "  Inversion. Loesung: --cpu N explizit setzen (alle auf denselben Kern)."
    exit 0
fi
echo "  Demo NICHT zeigen, bevor das behoben ist."
exit 1
