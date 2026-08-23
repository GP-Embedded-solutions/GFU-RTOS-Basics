# =============================================================================
#  Lernprojekt "Echtzeit-Messsystem" -- Stufe 1
#  Kurs: Echtzeit-Linux (GFU), Trainer: Gaetan Pandji
#
#  make            alles bauen
#  make test       Selbsttest ohne Sensor-Hardware (Kompilierpruefung + Logik)
#  make demo       Kurzer Vergleichslauf naiv vs. korrekt (braucht Sensor)
#  make clean      aufraeumen
# =============================================================================

CC       ?= gcc
CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -O2 -g
LDFLAGS  :=
LDLIBS   := -lm

# Ohne Optimierung waere die Jittermessung selbst zu langsam; -O2 ist Absicht.
# -g bleibt drin, damit GDB an Tag 3 direkt funktioniert.

BINARIES := stage1_naive stage1_reference stage1_uebung
OBJS_COMMON := bme280.o

.PHONY: all clean test demo help uebung loesung

all: $(BINARIES)

stage1_naive: stage1_naive.o $(OBJS_COMMON)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

stage1_reference: stage1_reference.o $(OBJS_COMMON)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Arbeitsversion fuer den Teilnehmer (Lueckentext mit TODO 1-3)
uebung: stage1_uebung
stage1_uebung: stage1_uebung.o $(OBJS_COMMON)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c bme280.h
	$(CC) $(CFLAGS) -c -o $@ $<

# -----------------------------------------------------------------------------
# Selbsttest: prueft, dass alles ohne Warnung kompiliert und die Binaries
# starten. Laeuft AUCH OHNE angeschlossenen Sensor (erwartet dann einen
# sauberen Fehler statt eines Absturzes -- das ist der eigentliche Test).
# -----------------------------------------------------------------------------
test: all
	@echo "=== Selbsttest ==="
	@echo "--- 1. Kompilierung ohne Warnungen"
	@$(MAKE) -s clean >/dev/null
	@if $(MAKE) -s all 2>&1 | grep -qi 'warning'; then \
	   echo "    FEHLGESCHLAGEN: Compiler-Warnungen vorhanden"; exit 1; \
	 else echo "    OK"; fi
	@echo "--- 2. Hilfetext"
	@./stage1_reference --help >/dev/null && echo "    OK" || (echo "    FEHLGESCHLAGEN"; exit 1)
	@echo "--- 3. Verhalten ohne Sensor (muss sauber fehlschlagen, nicht abstuerzen)"
	@if ./stage1_reference -d 1 2>&1 | grep -qE 'Sensor-Fehler|Sensor  :'; then \
	   echo "    OK"; else echo "    FEHLGESCHLAGEN"; exit 1; fi
	@echo "--- 4. I2C-Geraet vorhanden?"
	@if [ -e /dev/i2c-1 ]; then echo "    OK (/dev/i2c-1)"; \
	 else echo "    HINWEIS: /dev/i2c-1 fehlt -- Sensortests nicht moeglich"; fi
	@echo "=== Selbsttest abgeschlossen ==="

# -----------------------------------------------------------------------------
# Demo: der didaktische Kern. Erst die naive Version 30 s, dann die korrekte.
# Der Unterschied in der Bilanz ist der Aha-Moment.
# -----------------------------------------------------------------------------
demo: all
	@echo ""
	@echo "########################################################"
	@echo "#  1/2  NAIVE Version -- 30 Sekunden, Periode 100 ms   #"
	@echo "#       Erwartung: 300 Messungen                       #"
	@echo "########################################################"
	@timeout 30 ./stage1_naive || true
	@echo ""
	@echo "########################################################"
	@echo "#  2/2  KORREKTE Version -- 30 Sekunden, 100 ms        #"
	@echo "#       Erwartung: 300 Messungen                       #"
	@echo "########################################################"
	@./stage1_reference -d 30
	@echo ""
	@echo ">>> Vergleiche die Zeile 'Messungen tatsaechlich' in beiden Bilanzen."

clean:
	rm -f *.o $(BINARIES) *.csv

# Zeigt die Musterloesung fuer die drei TODOs
loesung:
	@echo "TODO 1:  double temperatur = messwert_holen();"
	@echo ""
	@echo "TODO 2:  long long nun_ns  = jetzt_ns();"
	@echo "         long long periode_us = (nun_ns - letzte_ns) / 1000;"
	@echo "         letzte_ns = nun_ns;"
	@echo ""
	@echo "TODO 3:  usleep(PERIODE_MS * 1000);"

help:
	@echo "make uebung  -- Arbeitsversion fuer den Teilnehmer"
	@echo "make loesung -- Musterloesung der drei TODOs anzeigen"
	@echo "make        -- bauen"
	@echo "make test   -- Selbsttest"
	@echo "make demo   -- Vergleichslauf naiv vs. korrekt (Sensor noetig)"
	@echo "make clean  -- aufraeumen"
