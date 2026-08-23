/* ===========================================================================
 * stage1_uebung.c -- Lernprojekt Stufe 1, ARBEITSVERSION fuer den Teilnehmer
 *
 * ===========================================================================
 *  IHRE AUFGABE STEHT AN DREI STELLEN.  Suchen Sie nach:   TODO 1, 2, 3
 * ===========================================================================
 *
 * Was Sie NICHT tun muessen:
 *   - I2C verstehen
 *   - Sensor-Register kennen
 *   - Kompensationsformeln implementieren
 *
 * Das ist alles fertig. Die Funktion messwert_holen() liefert Ihnen einfach
 * eine Temperatur. Wie sie das macht, ist heute nicht Ihr Problem --
 * behandeln Sie sie wie eine Bibliotheksfunktion.
 *
 * Worum es HEUTE geht:
 *   Ein Programm, das ZUVERLAESSIG alle 100 Millisekunden misst.
 *   Klingt trivial. Ist es nicht.
 *
 * Kompilieren:   make uebung
 * Ausfuehren:    ./stage1_uebung
 *                ./stage1_uebung --sim      (ohne Sensor-Hardware)
 * ======================================================================== */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "bme280.h"

#define PERIODE_MS   100
#define I2C_DEVICE   "/dev/i2c-1"

static volatile sig_atomic_t laeuft = 1;
static void signal_handler(int sig) { (void)sig; laeuft = 0; }

/* ===========================================================================
 *  FERTIG -- hier muessen Sie nichts tun
 * ======================================================================== */

static bme280_t g_dev;
static int      g_simulation = 0;

/* Oeffnet den Sensor. Bei --sim wird keine Hardware gebraucht. */
static int sensor_oeffnen(int simulation)
{
    g_simulation = simulation;
    if (simulation) {
        printf("Sensor  : SIMULATION (keine Hardware noetig)\n");
        return 0;
    }
    int rc = bme280_open(&g_dev, I2C_DEVICE, 0);
    if (rc != 0) {
        fprintf(stderr, "Sensor-Fehler: %s\n", bme280_strerror(rc));
        fprintf(stderr, "Tipp: mit --sim starten, dann laeuft es ohne Hardware.\n");
        return rc;
    }
    printf("Sensor  : Chip-ID 0x%02X auf Adresse 0x%02X\n", g_dev.chip_id, g_dev.addr);
    return 0;
}

/* Holt EINEN Messwert. Behandeln Sie das wie eine Bibliotheksfunktion.
 *
 * Wichtig zu wissen -- und das ist der einzige technische Punkt, den Sie
 * heute ueber den Sensor brauchen:
 *
 *     DIESER AUFRUF DAUERT UNTERSCHIEDLICH LANGE.
 *
 * Mal 5 Millisekunden, mal 15. Der Sensor braucht Zeit zum Wandeln, und
 * der I2C-Bus ist nicht deterministisch. Das ist voellig normal und laesst
 * sich nicht wegoptimieren.
 *
 * Merken Sie sich diesen Satz. Er ist gleich wichtig. */
static double messwert_holen(void)
{
    if (g_simulation) {
        /* Simuliert eine Messung, die 5-15 ms dauert -- genau wie echt. */
        long ns = (5000L + (rand() % 10000L)) * 1000L;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = ns };
        nanosleep(&ts, NULL);
        return 21.5 + (rand() % 100) / 100.0;
    }
    bme280_reading_t m;
    if (bme280_read(&g_dev, &m) != 0) return -273.15;   /* Fehlerwert */
    return m.temperature_c;
}

static void sensor_schliessen(void)
{
    if (!g_simulation) bme280_close(&g_dev);
}

/* Liefert die aktuelle Zeit in Nanosekunden.
 *
 * CLOCK_MONOTONIC ist eine Stoppuhr: laeuft seit dem Systemstart und
 * springt nie. CLOCK_REALTIME waere die Wanduhr -- die kann durch eine
 * Zeitsynchronisation springen, auch rueckwaerts. Fuer Zeitmessung
 * unbrauchbar. */
static long long jetzt_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* ===========================================================================
 *  AB HIER SIND SIE DRAN
 * ======================================================================== */

int main(int argc, char **argv)
{
    int simulation = 0;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--sim")) simulation = 1;

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    srand(42);

    printf("=== Lernprojekt Stufe 1 ===\n");
    printf("Sollperiode: %d ms\n", PERIODE_MS);
    printf("Beenden mit Strg+C\n\n");

    if (sensor_oeffnen(simulation) != 0) return EXIT_FAILURE;
    printf("\n");

    long long start_ns   = jetzt_ns();
    long long letzte_ns  = start_ns;
    long      zaehler    = 0;

    while (laeuft) {

        /* ===================================================================
         * TODO 1 -- Messen
         *
         * Holen Sie einen Messwert und speichern Sie ihn in einer Variablen.
         * Die Funktion heisst messwert_holen() und gibt ein double zurueck.
         *
         * Eine Zeile.
         * ================================================================ */

        double temperatur = 0.0;   /* <-- ersetzen */


        /* ===================================================================
         * TODO 2 -- Zeit messen
         *
         * Bestimmen Sie, wie viele MIKROsekunden seit der letzten Messung
         * vergangen sind, und speichern Sie das in periode_us.
         *
         * Hilfsmittel:
         *   jetzt_ns()   liefert die aktuelle Zeit in Nanosekunden
         *   letzte_ns    enthaelt den Zeitpunkt der vorigen Messung
         *   1 Mikrosekunde = 1000 Nanosekunden
         *
         * Vergessen Sie nicht, letzte_ns danach zu aktualisieren --
         * sonst messen Sie beim naechsten Durchlauf gegen den falschen
         * Bezugspunkt.
         *
         * Zwei bis drei Zeilen.
         * ================================================================ */

        long long periode_us = 0;   /* <-- ersetzen */


        zaehler++;

        /* Schutz: Wenn TODO 3 noch nicht ausgefuellt ist, rast die Schleife.
         * Statt das Terminal zu fluten, brechen wir mit einem Hinweis ab. */
        if (zaehler == 5000 && (jetzt_ns() - start_ns) < 1000000000LL) {
            printf("\n----------------------------------------------------\n");
            printf("  5000 Durchlaeufe in unter einer Sekunde.\n");
            printf("  Das Programm wartet nicht -- TODO 3 fehlt noch.\n");
            printf("  Fuellen Sie TODO 3 aus und starten Sie neu.\n");
            printf("----------------------------------------------------\n\n");
            sensor_schliessen();
            return EXIT_FAILURE;
        }

        if (zaehler % 10 == 0) {
            printf("[%5ld]  T=%6.2f C   gemessene Periode: %6lld us\n",
                   zaehler, temperatur, periode_us);
            fflush(stdout);
        }

        /* ===================================================================
         * TODO 3 -- Warten
         *
         * Warten Sie, bis die naechste Messung faellig ist.
         *
         * Der naheliegende Weg:
         *     usleep(PERIODE_MS * 1000);
         *
         * Nehmen Sie den. Er ist genau richtig fuer heute.
         *
         * (Ja, das ist ein Hinweis. Nein, ich sage Ihnen jetzt nicht,
         *  worauf er hinauslaeuft.)
         * ================================================================ */

        /* <-- hier */

    }

    /* ===================================================================
     *  Bilanz -- fertig, hier muessen Sie nichts tun.
     *
     *  DIESE AUSGABE IST DER EIGENTLICHE PUNKT DER UEBUNG.
     *  Lassen Sie das Programm 60 Sekunden laufen und lesen Sie sie.
     * ================================================================ */
    {
        double laufzeit_s  = (jetzt_ns() - start_ns) / 1e9;
        double soll_anzahl = laufzeit_s / (PERIODE_MS / 1000.0);

        printf("\n=========== Bilanz ===========\n");
        printf("Laufzeit                : %8.2f s\n",  laufzeit_s);
        printf("Messungen erwartet      : %8.0f\n",    soll_anzahl);
        printf("Messungen tatsaechlich  : %8ld\n",     zaehler);
        printf("Differenz               : %8.0f",      soll_anzahl - zaehler);
        if (soll_anzahl > 0)
            printf("   (%.1f %%)", 100.0 * (soll_anzahl - zaehler) / soll_anzahl);
        printf("\n==============================\n\n");
        printf("Frage: Wo sind die fehlenden Messungen hin?\n\n");
    }

    sensor_schliessen();
    return EXIT_SUCCESS;
}
