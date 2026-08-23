/* ===========================================================================
 * stage1_naive.c -- Lernprojekt "Echtzeit-Messsystem", Stufe 1, NAIVE Version
 *
 *   ============================================================
 *   DIESES PROGRAMM IST ABSICHTLICH FALSCH.
 *   ============================================================
 *
 * Es ist der Ausgangspunkt fuer Productive Failure. Die Teilnehmenden
 * schreiben genau so ihr erstes Messprogramm -- und jeder tut das, weil es
 * offensichtlich aussieht:
 *
 *      while (1) { messen(); sleep(100ms); }
 *
 * Das ist die haeufigste Fehlkonstruktion in echten Embedded-Projekten.
 *
 * Der Fehler, den die Teilnehmenden SELBST finden sollen:
 *   Die Schleife schlaeft 100 ms NACH der Arbeit. Die Periode ist also
 *   nicht 100 ms, sondern 100 ms + Ausfuehrungszeit. Die Ausfuehrungszeit
 *   schwankt (I2C-Wandlung, Scheduling). Ergebnis: Die Messzeitpunkte
 *   driften monoton weg. Nach einer Stunde fehlen Messungen -- und niemand
 *   merkt es, weil "es laeuft ja".
 *
 * Ein Logger mit driftenden Zeitstempeln ist im Pharma-Umfeld ein
 * dokumentationspflichtiger Mangel. Genau deshalb ist das kein akademisches
 * Beispiel.
 *
 * Uebung: 60 Sekunden laufen lassen, dann die Frage stellen:
 *   "Wie viele Messungen haettest du erwartet? Wie viele hast du?"
 *
 * Kompilieren:  make
 * Ausfuehren:   ./stage1_naive
 * ======================================================================== */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bme280.h"

/* ── Simulation (--sim) ───────────────────────────────────────────────── */
static int g_simulation = 0;

/* Gibt Temperatur zurueck; blockiert realistisch 5-15 ms wie echter Sensor */
static double sim_messwert(void)
{
    long ns = (5000L + (rand() % 10000L)) * 1000L;
    struct timespec ts = { .tv_sec = 0, .tv_nsec = ns };
    nanosleep(&ts, NULL);
    return 21.5 + (rand() % 100) / 100.0;
}


#define PERIODE_MS      100
#define I2C_DEVICE      "/dev/i2c-1"

static volatile sig_atomic_t laeuft = 1;
static void signal_handler(int sig) { (void)sig; laeuft = 0; }

/* Monotone Zeit in Nanosekunden.
 * CLOCK_MONOTONIC, nicht CLOCK_REALTIME: CLOCK_REALTIME kann durch NTP
 * springen -- und zwar auch rueckwaerts. Fuer Zeitmessung unbrauchbar. */
static long long jetzt_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--sim")) g_simulation = 1;
    srand(42);

    bme280_t          dev;
    bme280_reading_t  messung;
    long long         start_ns, letzte_ns, jitter_summe_us = 0;
    long              zaehler = 0, fehler = 0;
    long              jitter_max_us = 0, jitter_min_us = 1000000;
    int               rc;

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    printf("=== Lernprojekt Stufe 1 -- NAIVE Version ===\n");
    printf("Sollperiode: %d ms\n", PERIODE_MS);
    printf("Beenden mit Strg+C\n\n");

    if (!g_simulation) {
        rc = bme280_open(&dev, I2C_DEVICE, 0);
        if (rc != 0) {
            fprintf(stderr, "Sensor-Fehler: %s\n", bme280_strerror(rc));
            fprintf(stderr, "Tipp: mit --sim starten, dann laeuft es ohne Hardware.\n");
            return EXIT_FAILURE;
        }
    }
    if (g_simulation)
        printf("Sensor  : SIMULATION (kein I2C noetig)\n\n");
    else
        printf("Sensor  : Chip-ID 0x%02X auf 0x%02X%s\n\n",
               dev.chip_id, dev.addr,
               dev.has_humidity ? " (BME280)" : " (BMP280)");

    start_ns  = jetzt_ns();
    letzte_ns = start_ns;

    while (laeuft) {

        /* ---------------------------------------------- die eigentliche Arbeit */
        if (g_simulation) {
            messung.temperature_c = sim_messwert();
            messung.pressure_hpa  = 1013.2;
            messung.humidity_pct  = 45.0;
        } else {
            rc = bme280_read(&dev, &messung);
            if (rc != 0) fehler++;
        }

        /* ------------------------------------------------------- Buchhaltung */
        long long nun_ns    = jetzt_ns();
        long long delta_us  = (nun_ns - letzte_ns) / 1000;
        letzte_ns = nun_ns;

        if (zaehler > 0) {              /* erste Iteration hat keinen Vorgaenger */
            long abweichung_us = (long)(delta_us - PERIODE_MS * 1000L);
            jitter_summe_us += abweichung_us;
            if (abweichung_us > jitter_max_us) jitter_max_us = abweichung_us;
            if (abweichung_us < jitter_min_us) jitter_min_us = abweichung_us;
        }
        zaehler++;

        if (zaehler % 10 == 0) {
            double laufzeit_s = (nun_ns - start_ns) / 1e9;
            double soll       = laufzeit_s / (PERIODE_MS / 1000.0);
            printf("[%6ld] T=%6.2f C  p=%7.2f hPa  rF=%5.1f %%  |  "
                   "Periode=%6lld us  Soll waeren %.0f Messungen -> Rueckstand: %.0f\n",
                   zaehler, messung.temperature_c, messung.pressure_hpa,
                   messung.humidity_pct, delta_us, soll, soll - zaehler);
            fflush(stdout);
        }

        /* ================================================================
         *  HIER SITZT DER FEHLER
         *
         *  Es wird PERIODE_MS geschlafen -- ZUSAETZLICH zur Arbeitszeit
         *  oben. Die tatsaechliche Periode ist damit:
         *
         *      T_real = PERIODE_MS + t_arbeit + t_scheduling
         *
         *  t_arbeit ist nicht konstant (I2C-Wandlung schwankt).
         *  t_scheduling ist nicht konstant (Standard-Scheduler).
         *
         *  Der Fehler akkumuliert. Er verschwindet nicht.
         * ================================================================ */
        usleep(PERIODE_MS * 1000);
    }

    /* ------------------------------------------------------------- Bilanz */
    {
        double laufzeit_s   = (jetzt_ns() - start_ns) / 1e9;
        double soll_anzahl  = laufzeit_s / (PERIODE_MS / 1000.0);

        printf("\n=========== Bilanz ===========\n");
        printf("Laufzeit                  : %8.2f s\n", laufzeit_s);
        printf("Messungen erwartet        : %8.0f\n",   soll_anzahl);
        printf("Messungen tatsaechlich    : %8ld\n",    zaehler);
        printf("FEHLENDE MESSUNGEN        : %8.0f  (%.2f %%)\n",
               soll_anzahl - zaehler,
               100.0 * (soll_anzahl - zaehler) / soll_anzahl);
        printf("Lesefehler Sensor         : %8ld\n",    fehler);
        printf("Periodenabweichung Min    : %8ld us\n", jitter_min_us);
        printf("Periodenabweichung Max    : %8ld us\n", jitter_max_us);
        printf("Periodenabweichung Mittel : %8.1f us\n",
               zaehler > 1 ? (double)jitter_summe_us / (zaehler - 1) : 0.0);
        printf("==============================\n\n");
        printf("Leitfrage: Warum fehlen Messungen -- und warum wird es\n");
        printf("           mit laengerer Laufzeit SCHLIMMER, nicht besser?\n\n");
    }

    if (!g_simulation) bme280_close(&dev);
    return EXIT_SUCCESS;
}
