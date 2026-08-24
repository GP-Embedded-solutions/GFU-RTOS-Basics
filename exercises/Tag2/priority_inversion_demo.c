/*
 * priority_inversion_demo.c
 * -------------------------------------------------------------------------
 * Reproduziert unbeschraenkte Prioritaetsinversion auf einem einzigen CPU-Kern
 * und heilt sie per Priority Inheritance (PTHREAD_PRIO_INHERIT).
 *
 * Das ist die Code-Aufloesung der Mars-Pathfinder-Story (Tag 1 Cliffhanger,
 * Tag 2 Modul 6, 14:00).
 *
 * Aufbau (alle drei Threads auf DEMSELBEN Kern, sonst gibt es keine Inversion):
 *
 *   L  (SCHED_FIFO, Prio 10)  nimmt das Mutex und rechnet HOLD_MS lang
 *   H  (SCHED_FIFO, Prio 30)  will nach H_START_MS dasselbe Mutex
 *   M  (SCHED_FIFO, Prio 20)  rechnet ab M_START_MS stumpf M_BUSY_MS lang,
 *                             braucht das Mutex NICHT
 *
 * Ohne Vererbung:  M verdraengt L. L kann das Mutex nicht freigeben.
 *                  H wartet, obwohl H die hoechste Prioritaet hat.
 *                  Blockierzeit von H  ~  M_BUSY_MS + Rest von L.
 *
 * Mit  Vererbung:  L erbt beim Blockieren von H dessen Prioritaet 30,
 *                  verdraengt M, gibt das Mutex frei.
 *                  Blockierzeit von H  ~  nur der Rest der L-Sektion.
 *
 * Bauen:   make
 * Starten: sudo ./priority_inversion_demo            (kaputt)
 *          sudo ./priority_inversion_demo --inherit  (geheilt)
 *
 * Warum sudo: SCHED_FIFO braucht CAP_SYS_NICE.
 * Sicherheit: alle Busy-Loops sind zeitlich hart begrenzt, der Ablauf endet
 *             nach rund 1,5 Sekunden. Kein Runaway-Risiko.
 * -------------------------------------------------------------------------
 */

#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- Zeitparameter des Szenarios (Millisekunden) ---------------------- */
#define HOLD_MS     600   /* wie lange L das Mutex haelt (rechnend)        */
#define H_START_MS  150   /* wann H versucht, das Mutex zu nehmen          */
#define M_START_MS  250   /* wann der Stoerenfried M loslegt               */
#define M_BUSY_MS   700   /* wie lange M die CPU belegt                    */

static int   prio_low  = 10;
static int   prio_mid  = 20;
static int   prio_high = 30;
static int   use_inherit = 0;
static int   cpu_id      = -1;
static int   verbose     = 0;

static pthread_mutex_t lock;
static pthread_barrier_t startgate;   /* alle Threads sind fertig eingerichtet */
static struct timespec t0;            /* gemeinsamer Zeitnullpunkt (absolut)   */

/* ---- Zeit-Helfer ------------------------------------------------------ */
static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)(t.tv_sec - t0.tv_sec) * 1000.0
         + (double)(t.tv_nsec - t0.tv_nsec) / 1e6;
}

static void log_ev(const char *who, const char *what)
{
    printf("  [%8.2f ms]  %-2s  %s\n", now_ms(), who, what);
    fflush(stdout);
}

/* Busy-Wait: belegt die CPU wirklich (kein sleep!), sonst gibt es keine
   Verdraengung zu beobachten. Hart begrenzt. */
static void burn_ms(double ms)
{
    double end = now_ms() + ms;
    volatile unsigned long x = 0;
    while (now_ms() < end) {
        for (int i = 0; i < 2000; i++) x += i;   /* Arbeit simulieren */
    }
    (void)x;
}

/*
 * Bis zum ABSOLUTEN Zeitpunkt t0 + off_ms schlafen.
 *
 * Warum absolut und nicht relativ: auf EINEM Kern kommt ein Thread, der erst
 * nach dem Start von L erzeugt wird, gar nicht mehr zum Zug - L rechnet ja.
 * Alle drei Threads werden deshalb VOR dem Start eingerichtet, warten an einer
 * Barriere und wachen danach zu fest verabredeten Zeitpunkten auf. Genau das
 * ist auch die Technik aus Stufe 1 des Lernprojekts
 * (clock_nanosleep + TIMER_ABSTIME, kein driftendes sleep).
 */
static void wake_at(double off_ms)
{
    struct timespec ts = t0;
    long long ns = ts.tv_nsec + (long long)(off_ms * 1e6);
    ts.tv_sec  += ns / 1000000000LL;
    ts.tv_nsec  = ns % 1000000000LL;
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR)
        ;
}

/* ---- Thread-Setup ----------------------------------------------------- */
static int pin_to_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

static int set_fifo(int prio)
{
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = prio;
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
}

static int prepare_thread(const char *who, int prio)
{
    if (pin_to_cpu(cpu_id) != 0) {
        fprintf(stderr, "FEHLER: %s konnte nicht auf CPU %d gebunden werden\n",
                who, cpu_id);
        return -1;
    }
    if (set_fifo(prio) != 0) {
        fprintf(stderr,
                "FEHLER: %s bekam kein SCHED_FIFO (%s).\n"
                "        Bitte mit sudo starten.\n", who, strerror(errno));
        return -1;
    }
    if (verbose) {
        int policy; struct sched_param sp;
        pthread_getschedparam(pthread_self(), &policy, &sp);
        printf("  [ setup ]  %-2s  policy=%s prio=%d cpu=%d\n", who,
               policy == SCHED_FIFO ? "SCHED_FIFO" : "andere",
               sp.sched_priority, sched_getcpu());
    }
    return 0;
}

/* ---- Die drei Akteure ------------------------------------------------- */
static double h_blocked_ms = -1.0;   /* Messergebnis */
static double h_wait_start = 0.0;

static void *thread_low(void *arg)
{
    (void)arg;
    if (prepare_thread("L", prio_low) != 0) { pthread_barrier_wait(&startgate); return NULL; }
    pthread_barrier_wait(&startgate);
    wake_at(0);

    log_ev("L", "nimmt das Mutex");
    pthread_mutex_lock(&lock);
    log_ev("L", "hat das Mutex, beginnt zu rechnen");

    burn_ms(HOLD_MS);

    log_ev("L", "fertig, gibt das Mutex frei");
    pthread_mutex_unlock(&lock);
    return NULL;
}

static void *thread_mid(void *arg)
{
    (void)arg;
    if (prepare_thread("M", prio_mid) != 0) { pthread_barrier_wait(&startgate); return NULL; }
    pthread_barrier_wait(&startgate);
    wake_at(M_START_MS);

    log_ev("M", "startet Rechenlast (braucht KEIN Mutex)");
    burn_ms(M_BUSY_MS);
    log_ev("M", "fertig");
    return NULL;
}

static void *thread_high(void *arg)
{
    (void)arg;
    if (prepare_thread("H", prio_high) != 0) { pthread_barrier_wait(&startgate); return NULL; }
    pthread_barrier_wait(&startgate);
    wake_at(H_START_MS);

    log_ev("H", "will das Mutex  ->  blockiert ab jetzt");
    h_wait_start = now_ms();

    pthread_mutex_lock(&lock);
    h_blocked_ms = now_ms() - h_wait_start;

    log_ev("H", "hat das Mutex bekommen");
    burn_ms(30);
    pthread_mutex_unlock(&lock);
    log_ev("H", "fertig");
    return NULL;
}

/* ---- Auswertung ------------------------------------------------------- */
static void verdict(void)
{
    double erwartet_kaputt   = (double)(M_START_MS + M_BUSY_MS) - H_START_MS;
    double erwartet_geheilt  = (double)(HOLD_MS) - H_START_MS;

    printf("\n---------------------------------------------------------------\n");
    printf("  Modus                 : %s\n",
           use_inherit ? "PTHREAD_PRIO_INHERIT  (geheilt)"
                       : "PTHREAD_PRIO_NONE     (Standard, kaputt)");
    printf("  H war blockiert       : %.2f ms\n", h_blocked_ms);
    printf("  Erwartung ohne Erbe   : ~%.0f ms  (M_BUSY schiebt sich dazwischen)\n",
           erwartet_kaputt);
    printf("  Erwartung mit Erbe    : ~%.0f ms  (nur der Rest von L)\n",
           erwartet_geheilt);
    printf("---------------------------------------------------------------\n");

    if (h_blocked_ms < 0) {
        printf("  ERGEBNIS: kein Messwert - lief H ueberhaupt? (sudo vergessen?)\n");
        return;
    }
    /* Schwelle liegt bewusst mittig zwischen beiden Erwartungen */
    double schwelle = (erwartet_kaputt + erwartet_geheilt) / 2.0;
    if (h_blocked_ms > schwelle)
        printf("  ERGEBNIS: UNBESCHRAENKTE PRIORITAETSINVERSION.\n"
               "            Der hoechstpriore Thread wartete auf einen Thread,\n"
               "            mit dem er nichts zu tun hat. Genau das ist 1997\n"
               "            auf dem Mars passiert.\n");
    else
        printf("  ERGEBNIS: Inversion begrenzt. L hat die Prioritaet von H geerbt,\n"
               "            M verdraengt, das Mutex freigegeben. Das ist der Fix,\n"
               "            den die JPL-Ingenieure per Funk aktiviert haben.\n");
    printf("\n");
}

/* ---- main ------------------------------------------------------------- */
static void usage(const char *p)
{
    printf(
"Verwendung: sudo %s [Optionen]\n"
"  --inherit           Mutex mit PTHREAD_PRIO_INHERIT anlegen (der Fix)\n"
"  --cpu N             alle Threads auf Kern N binden (Vorgabe: letzter Kern)\n"
"  --prio L,M,H        SCHED_FIFO-Prioritaeten (Vorgabe: 10,20,30)\n"
"  --verbose           Setup je Thread ausgeben\n"
"  --help              diese Hilfe\n", p);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--inherit"))      use_inherit = 1;
        else if (!strcmp(argv[i], "--verbose")) verbose = 1;
        else if (!strcmp(argv[i], "--cpu") && i + 1 < argc) cpu_id = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--prio") && i + 1 < argc)
            sscanf(argv[++i], "%d,%d,%d", &prio_low, &prio_mid, &prio_high);
        else { usage(argv[0]); return (strcmp(argv[i], "--help") == 0) ? 0 : 1; }
    }

    if (cpu_id < 0) {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        cpu_id = (int)(n > 0 ? n - 1 : 0);
    }

    if (geteuid() != 0)
        fprintf(stderr,
            "HINWEIS: nicht als root gestartet - SCHED_FIFO wird vermutlich\n"
            "         scheitern. Starte mit sudo.\n\n");

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    if (use_inherit) {
        if (pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT) != 0) {
            fprintf(stderr, "FEHLER: PTHREAD_PRIO_INHERIT nicht verfuegbar\n");
            return 1;
        }
    }
    pthread_mutex_init(&lock, &attr);

    pthread_barrier_init(&startgate, NULL, 4);   /* 3 Threads + main */

    printf("\n=== Prioritaetsinversion: %s ===\n",
           use_inherit ? "MIT Priority Inheritance" : "OHNE Priority Inheritance");
    printf("    ein Kern (CPU %d), Prioritaeten L=%d M=%d H=%d\n\n",
           cpu_id, prio_low, prio_mid, prio_high);

    /* Zeitnullpunkt liegt bewusst in der Zukunft: erst wenn alle drei
       Threads eingerichtet an der Barriere stehen, laeuft die Uhr los. */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    t0.tv_nsec += 50 * 1000000L;                 /* +50 ms Vorlauf */
    if (t0.tv_nsec >= 1000000000L) { t0.tv_sec++; t0.tv_nsec -= 1000000000L; }

    pthread_t tl, tm, th;
    pthread_create(&tl, NULL, thread_low,  NULL);
    pthread_create(&th, NULL, thread_high, NULL);
    pthread_create(&tm, NULL, thread_mid,  NULL);

    pthread_barrier_wait(&startgate);   /* Startschuss */

    pthread_join(tl, NULL);
    pthread_join(th, NULL);
    pthread_join(tm, NULL);

    verdict();

    pthread_barrier_destroy(&startgate);
    pthread_mutex_destroy(&lock);
    pthread_mutexattr_destroy(&attr);
    return 0;
}
