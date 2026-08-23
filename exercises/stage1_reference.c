/* ===========================================================================
 * stage1_reference.c -- Lernprojekt "Echtzeit-Messsystem", Stufe 1
 *                       KORREKTE Referenzloesung
 *
 * Unterschied zu stage1_naive.c: EINE Zeile Konzept, riesige Wirkung.
 *
 *   NAIV:      arbeite();  schlafe(100 ms);          -> Periode driftet
 *   RICHTIG:   naechste += 100 ms;
 *              schlafe_bis(naechste);  arbeite();    -> Periode driftet NICHT
 *
 * Das Werkzeug dafuer ist clock_nanosleep() mit dem Flag TIMER_ABSTIME.
 * Es schlaeft nicht "eine Dauer", sondern "bis zu einem Zeitpunkt".
 * Damit ist die Ausfuehrungszeit der Arbeit egal -- sie wird von der
 * Schlafzeit automatisch abgezogen. Der Fehler akkumuliert nicht mehr.
 *
 * Das ist das Muster fuer JEDE zyklische Echtzeitaufgabe unter Linux.
 * Wer es einmal verstanden hat, macht diesen Fehler nie wieder.
 *
 * Zusaetzlich vorbereitet (fuer Stufe 2 an Tag 2, hier per Option schaltbar):
 *   --fifo <prio>   SCHED_FIFO mit gegebener Prioritaet
 *   --mlock         Speicher sperren, Page-Faults vermeiden
 *   --cpu <n>       Affinitaet auf einen Kern
 *   --csv <datei>   Jitter-Protokoll fuer die Auswertung
 *
 * Kompilieren:  make
 * Beispiele:
 *   ./stage1_reference
 *   sudo ./stage1_reference --fifo 80 --mlock --cpu 3 --csv jitter.csv -d 60
 * ======================================================================== */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <limits.h>
#include <math.h>

#include "bme280.h"

/* ── Simulation (--sim) ───────────────────────────────────────────────── */
static int g_simulation = 0;

static double sim_messwert(void)
{
    long ns = (5000L + (rand() % 10000L)) * 1000L;
    struct timespec ts = { .tv_sec = 0, .tv_nsec = ns };
    nanosleep(&ts, NULL);
    return 21.5 + (rand() % 100) / 100.0;
}


#define NS_PRO_SEK      1000000000LL
#define I2C_DEVICE      "/dev/i2c-1"

static volatile sig_atomic_t laeuft = 1;
static void signal_handler(int sig) { (void)sig; laeuft = 0; }

/* ------------------------------------------------------------ Zeit-Hilfsmittel */

static inline long long ts_zu_ns(const struct timespec *ts)
{
    return (long long)ts->tv_sec * NS_PRO_SEK + ts->tv_nsec;
}

/* Addiert Nanosekunden auf ein timespec und normalisiert den Ueberlauf.
 * Der haeufigste Anfaengerfehler ist, tv_nsec ueber 999.999.999 laufen zu
 * lassen -- clock_nanosleep gibt dann EINVAL zurueck. */
static inline void ts_addiere_ns(struct timespec *ts, long long ns)
{
    ts->tv_nsec += ns;
    while (ts->tv_nsec >= NS_PRO_SEK) {
        ts->tv_nsec -= NS_PRO_SEK;
        ts->tv_sec  += 1;
    }
}

/* ------------------------------------------------------------ Echtzeit-Setup */

static int setze_scheduler(int prio)
{
    struct sched_param p;
    memset(&p, 0, sizeof(p));
    p.sched_priority = prio;
    if (sched_setscheduler(0, SCHED_FIFO, &p) != 0) {
        fprintf(stderr, "WARNUNG: SCHED_FIFO(%d) fehlgeschlagen: %s\n",
                prio, strerror(errno));
        fprintf(stderr, "         -> mit sudo starten, oder Benutzer in die "
                        "Gruppe 'realtime' aufnehmen\n");
        return -1;
    }
    return 0;
}

static int sperre_speicher(void)
{
    /* mlockall verhindert, dass Seiten ausgelagert werden. Ein Page-Fault
     * mitten in einer Echtzeitschleife kostet Millisekunden -- die
     * unauffaelligste Latenzquelle ueberhaupt. */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        fprintf(stderr, "WARNUNG: mlockall fehlgeschlagen: %s\n", strerror(errno));
        return -1;
    }
    /* Stack vorab beruehren, damit er jetzt und nicht spaeter faultet. */
    {
        unsigned char puffer[64 * 1024];
        memset(puffer, 0, sizeof(puffer));
    }
    return 0;
}

static int setze_affinitaet(int cpu)
{
    cpu_set_t menge;
    CPU_ZERO(&menge);
    CPU_SET(cpu, &menge);
    if (sched_setaffinity(0, sizeof(menge), &menge) != 0) {
        fprintf(stderr, "WARNUNG: Affinitaet auf CPU %d fehlgeschlagen: %s\n",
                cpu, strerror(errno));
        return -1;
    }
    return 0;
}

/* --------------------------------------------------------------- Hauptprogramm */

static void nutzung(const char *prog)
{
    printf(
"Nutzung: %s [Optionen]\n"
"\n"
"  -p, --periode <ms>   Zykluszeit in Millisekunden   (Standard: 100)\n"
"  -d, --dauer <s>      Laufzeit in Sekunden, 0 = endlos (Standard: 0)\n"
"      --fifo <prio>    SCHED_FIFO mit Prioritaet 1..99 (braucht Rechte)\n"
"      --mlock          Speicher sperren (mlockall)\n"
"      --cpu <n>        Auf CPU-Kern n festnageln\n"
"      --csv <datei>    Jitter-Protokoll als CSV schreiben\n"
"      --sim            Simulation ohne Sensor-Hardware\n"
"  -q, --quiet          Keine laufende Ausgabe, nur Bilanz\n"
"  -h, --help           Diese Hilfe\n"
"\n"
"Beispiele:\n"
"  %s -p 10 -d 60\n"
"  %s -p 10 -d 60 --sim\n"
"  sudo %s -p 1 -d 60 --fifo 80 --mlock --cpu 3 --csv jitter.csv\n",
    prog, prog, prog, prog);
}

int main(int argc, char **argv)
{
    long   periode_ms = 100;
    long   dauer_s    = 0;
    int    fifo_prio  = 0;
    int    mlock_an   = 0;
    int    cpu        = -1;
    int    quiet      = 0;
    const char *csv_pfad = NULL;

    /* ------------------------------------------------- Argumente auswerten */
    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { nutzung(argv[0]); return 0; }
        else if ((!strcmp(argv[i], "-p") || !strcmp(argv[i], "--periode")) && i+1 < argc) periode_ms = atol(argv[++i]);
        else if ((!strcmp(argv[i], "-d") || !strcmp(argv[i], "--dauer"))   && i+1 < argc) dauer_s    = atol(argv[++i]);
        else if (!strcmp(argv[i], "--fifo")  && i+1 < argc) fifo_prio = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cpu")   && i+1 < argc) cpu       = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--csv")   && i+1 < argc) csv_pfad  = argv[++i];
        else if (!strcmp(argv[i], "--mlock")) mlock_an = 1;
        else if (!strcmp(argv[i], "--sim"))   g_simulation = 1;
        else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet")) quiet = 1;
        else { fprintf(stderr, "Unbekannte Option: %s\n\n", argv[i]); nutzung(argv[0]); return 1; }
    }

    if (periode_ms < 1) { fprintf(stderr, "Periode muss mindestens 1 ms sein.\n"); return 1; }

    srand(42);
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    /* ------------------------------------------------- Echtzeit-Eigenschaften */
    printf("=== Lernprojekt Stufe 1 -- Referenzloesung ===\n");
    printf("Periode : %ld ms\n", periode_ms);
    printf("Dauer   : %s\n", dauer_s > 0 ? "begrenzt" : "endlos (Strg+C)");

    if (mlock_an)      printf("mlockall: %s\n", sperre_speicher()  == 0 ? "aktiv" : "FEHLGESCHLAGEN");
    if (cpu >= 0)      printf("CPU     : %d %s\n", cpu, setze_affinitaet(cpu) == 0 ? "(gesetzt)" : "(FEHLGESCHLAGEN)");
    if (fifo_prio > 0) printf("Policy  : SCHED_FIFO prio %d %s\n", fifo_prio,
                              setze_scheduler(fifo_prio) == 0 ? "(gesetzt)" : "(FEHLGESCHLAGEN)");
    else               printf("Policy  : SCHED_OTHER (Standard-Scheduler)\n");
    printf("\n");

    /* ------------------------------------------------- Sensor oeffnen */
    bme280_t dev;
    if (!g_simulation) {
        int rc = bme280_open(&dev, I2C_DEVICE, 0);
        if (rc != 0) {
            fprintf(stderr, "Sensor-Fehler: %s\n", bme280_strerror(rc));
            fprintf(stderr, "Tipp: mit --sim starten, dann laeuft es ohne Hardware.\n");
            return EXIT_FAILURE;
        }
        printf("Sensor  : Chip-ID 0x%02X auf 0x%02X%s\n\n",
               dev.chip_id, dev.addr, dev.has_humidity ? " (BME280)" : " (BMP280)");
    } else {
        printf("Sensor  : SIMULATION (kein I2C noetig)\n\n");
    }

    FILE *csv = NULL;
    if (csv_pfad) {
        csv = fopen(csv_pfad, "w");
        if (!csv) { fprintf(stderr, "CSV konnte nicht geschrieben werden: %s\n", strerror(errno)); }
        else       { fprintf(csv, "zyklus,jitter_us,arbeitszeit_us,temperatur_c,druck_hpa,feuchte_pct\n"); }
    }

    /* =====================================================================
     *  DAS MUSTER
     *
     *  1. Startzeit EINMAL holen.
     *  2. In der Schleife die naechste Deadline BERECHNEN (nicht messen).
     *  3. Absolut bis dorthin schlafen.
     *  4. Arbeiten.
     *
     *  Der Zeitpunkt der naechsten Ausfuehrung haengt damit ausschliesslich
     *  von der Startzeit ab, nicht von der bisherigen Ausfuehrungszeit.
     *  Der Fehler kann nicht akkumulieren.
     * ================================================================== */

    struct timespec naechste;
    clock_gettime(CLOCK_MONOTONIC, &naechste);
    const long long start_ns   = ts_zu_ns(&naechste);
    const long long periode_ns = (long long)periode_ms * 1000000LL;

    long long zaehler = 0, fehler = 0, verpasst = 0;
    long long jitter_summe = 0, jitter_quadrat_summe = 0;
    long      jitter_max = LONG_MIN;
    long      jitter_min = LONG_MAX;
    long      arbeit_max_us = 0;

    /* Erste Deadline eine Periode in der Zukunft */
    ts_addiere_ns(&naechste, periode_ns);

    while (laeuft) {

        /* --- 1. Absolut bis zur Deadline schlafen ------------------------ */
        int sr = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &naechste, NULL);
        if (sr != 0 && sr != EINTR) {
            fprintf(stderr, "clock_nanosleep: %s\n", strerror(sr));
            break;
        }
        if (!laeuft) break;

        /* --- 2. Jitter messen: Ist-Aufwachzeit minus Soll-Aufwachzeit ---- */
        struct timespec ist;
        clock_gettime(CLOCK_MONOTONIC, &ist);
        long jitter_us = (long)((ts_zu_ns(&ist) - ts_zu_ns(&naechste)) / 1000);

        /* --- 3. Die eigentliche Arbeit ----------------------------------- */
        bme280_reading_t messung;
        struct timespec  arbeit_start = ist, arbeit_ende;
        if (g_simulation) {
            messung.temperature_c = sim_messwert();
            messung.pressure_hpa  = 1013.2;
            messung.humidity_pct  = 45.0;
        } else {
            if (bme280_read(&dev, &messung) != 0) fehler++;
        }
        clock_gettime(CLOCK_MONOTONIC, &arbeit_ende);
        long arbeit_us = (long)((ts_zu_ns(&arbeit_ende) - ts_zu_ns(&arbeit_start)) / 1000);
        if (arbeit_us > arbeit_max_us) arbeit_max_us = arbeit_us;

        /* --- 4. Statistik ------------------------------------------------ */
        zaehler++;
        jitter_summe         += jitter_us;
        jitter_quadrat_summe += (long long)jitter_us * jitter_us;
        if (jitter_us > jitter_max) jitter_max = jitter_us;
        if (jitter_us < jitter_min) jitter_min = jitter_us;

        /* Deadline verpasst, wenn die Arbeit laenger dauert als die Periode */
        if (jitter_us + arbeit_us > periode_ms * 1000L) verpasst++;

        if (csv) {
            fprintf(csv, "%lld,%ld,%ld,%.2f,%.2f,%.1f\n",
                    zaehler, jitter_us, arbeit_us,
                    messung.temperature_c, messung.pressure_hpa, messung.humidity_pct);
        }

        if (!quiet && (zaehler % 10 == 0)) {
            printf("[%6lld] T=%6.2f C  p=%7.2f hPa  rF=%5.1f %%  |  "
                   "Jitter=%+5ld us  Arbeit=%4ld us  JitterMax=%+5ld us\n",
                   zaehler, messung.temperature_c, messung.pressure_hpa,
                   messung.humidity_pct, jitter_us, arbeit_us, jitter_max);
            fflush(stdout);
        }

        /* --- 5. Naechste Deadline: BERECHNEN, nicht messen --------------- */
        ts_addiere_ns(&naechste, periode_ns);

        /* Abbruch nach Zeit */
        if (dauer_s > 0 && (ts_zu_ns(&ist) - start_ns) >= (long long)dauer_s * NS_PRO_SEK)
            break;
    }

    /* ------------------------------------------------------------- Bilanz */
    {
        struct timespec ende;
        clock_gettime(CLOCK_MONOTONIC, &ende);
        double  laufzeit_s = (ts_zu_ns(&ende) - start_ns) / 1e9;
        double  soll       = laufzeit_s / (periode_ms / 1000.0);
        double  mittel     = zaehler ? (double)jitter_summe / zaehler : 0.0;
        double  varianz    = zaehler ? (double)jitter_quadrat_summe / zaehler - mittel * mittel : 0.0;
        double  stdabw     = varianz > 0 ? sqrt(varianz) : 0.0;

        printf("\n=========== Bilanz ===========\n");
        printf("Laufzeit                : %10.2f s\n", laufzeit_s);
        printf("Messungen erwartet      : %10.0f\n",   soll);
        printf("Messungen tatsaechlich  : %10lld\n",   zaehler);
        printf("Differenz               : %10.0f\n",   soll - zaehler);
        printf("Lesefehler Sensor       : %10lld\n",   fehler);
        printf("Deadline verpasst       : %10lld\n",   verpasst);
        printf("--------------------------------\n");
        printf("Jitter Min              : %+9ld us\n", jitter_min);
        printf("Jitter Mittel           : %+9.1f us\n", mittel);
        printf("Jitter Max              : %+9ld us\n", jitter_max);
        printf("Jitter Standardabw.     : %9.1f us\n", stdabw);
        printf("Arbeitszeit Max         : %9ld us\n", arbeit_max_us);
        printf("==============================\n\n");
        printf("Merksatz: Nicht der MITTELWERT entscheidet ueber Echtzeit-\n");
        printf("          faehigkeit, sondern das MAXIMUM. Determinismus heisst:\n");
        printf("          der schlechteste Fall ist bekannt und beschraenkt.\n\n");
    }

    if (csv) { fclose(csv); printf("Jitter-Protokoll geschrieben: %s\n", csv_pfad); }
    if (!g_simulation) bme280_close(&dev);
    return EXIT_SUCCESS;
}
