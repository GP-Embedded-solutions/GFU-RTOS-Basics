# Makefile — Lernprojekt Stufe 3, Seminar „Echtzeit-Linux" Tag 3
#
# Zwei Baumodi:
#   make        -> bindet bme280.c aus Tag 1 ein, FALLS die Datei hier liegt
#   make sim    -> baut ohne Sensorcode; nur --sim funktioniert (auch auf x86)
#
# Kopiere bme280.c und bme280.h aus dem Tag-1-Verzeichnis hierher, dann findet
# der Makefile sie automatisch. Fehlen sie, wird still die Sim-Variante gebaut
# und eine Warnung ausgegeben — der Kurs bleibt lauffaehig.

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
LDLIBS  := -lpthread -lm

BME_SRC := $(wildcard bme280.c)

ifeq ($(BME_SRC),)
  EXTRA_CFLAGS :=
  EXTRA_SRC    :=
  HINWEIS      := "bme280.c nicht gefunden -> Sim-Variante gebaut (nur --sim nutzbar)"
else
  EXTRA_CFLAGS := -DHAVE_BME280
  EXTRA_SRC    := bme280.c
  HINWEIS      := "bme280.c gefunden -> echter Sensor verfuegbar"
endif

.PHONY: all sim clean test hinweis

all: hinweis stage3_telemetry

hinweis:
	@echo "==> $(HINWEIS)"

stage3_telemetry: stage3_telemetry.c $(EXTRA_SRC)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(LDLIBS)

# Erzwungene Sim-Variante, auch wenn bme280.c vorhanden ist
sim: stage3_telemetry.c
	$(CC) $(CFLAGS) -o stage3_telemetry_sim stage3_telemetry.c $(LDLIBS)

# Rauchtest ohne Hardware und ohne Netz — muss immer gruen sein
test: sim
	@echo "== Rauchtest: 3 s Simulation, 5 ms Periode =="
	./stage3_telemetry_sim --sim --period 5 --duration 3 --quiet --csv /tmp/stufe3_test.csv
	@test -s /tmp/stufe3_test.csv && echo "== CSV geschrieben: OK ==" || (echo "FEHLER: CSV leer"; exit 1)
	@echo "== Rauchtest bestanden =="

clean:
	rm -f stage3_telemetry stage3_telemetry_sim *.o
