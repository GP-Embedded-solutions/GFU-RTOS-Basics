/*
 * stage3_telemetry.c — Lernprojekt „Echtzeit-Messsystem", Stufe 3
 * Seminar „Echtzeit-Linux", Tag 3 · Trainer: Gaëtan Pandji
 *
 * ---------------------------------------------------------------------------
 * WAS DIESES PROGRAMM ZEIGT (das ist der didaktische Kern, nicht die Telemetrie)
 * ---------------------------------------------------------------------------
 * Stufe 2 hat bewiesen: der Messthread laeuft deterministisch, wenn er
 * SCHED_FIFO hat, auf einem isolierten Kern liegt und sein Speicher gelockt ist.
 *
 * Stufe 3 stellt die Frage, an der reale Projekte scheitern:
 *   "Wie bekomme ich die Daten RAUS, ohne den Determinismus zu zerstoeren?"
 *
 * Die falsche Antwort ist, im RT-Thread ein send() oder ein fprintf() zu machen.
 * Beides kann blockieren — auf dem Socket-Puffer, auf dem Dateisystem, auf einer
 * malloc-Arena, auf einem Page-Fault. Ein einziges blockierendes send() unter
 * Netzlast erzeugt Latenz-Ausreisser im Millisekundenbereich. Genau diese
 * Signatur sehen die Teilnehmenden vorher in der Fehlerbilder-Galerie.
 *
 * Die richtige Antwort ist Entkopplung:
 *
 *   [RT-Thread, SCHED_FIFO, Kern 3]        [Sender-Thread, SCHED_OTHER, Kern 0-2]
 *          |                                          |
 *   clock_nanosleep(ABSTIME)                    ringbuffer_pop()
 *   Sensor lesen                                Zeilen zu einem Batch buendeln
 *   Jitter berechnen        --- SPSC-Ring --->  HTTP-POST an InfluxDB
 *   ringbuffer_push()  (nie blockierend)        darf blockieren, darf schlafen,
 *          |                                    darf sogar scheitern
 *
 * Der Ring ist Single-Producer/Single-Consumer, lock-frei ueber C11-Atomics.
 * Kein Mutex, kein malloc, kein Syscall im RT-Pfad. Laeuft der Ring voll, wird
 * die AELTESTE Messung verworfen und ein Zaehler erhoeht — Datenverlust ist
 * hier die richtige Entscheidung, denn ein Echtzeitsystem darf niemals auf
 * Telemetrie warten.
 *
 * Mit --demo-blocking laesst sich exakt der Fehler einschalten, den man sonst
 * nur erzaehlt: der HTTP-Post passiert dann IM RT-Thread. Die Jitter-Statistik
 * am Ende zeigt den Unterschied in Zahlen. Das ist Productive Failure mit
 * Messwert statt mit Anekdote.
 *
 * ---------------------------------------------------------------------------
 * BAUEN
 * ---------------------------------------------------------------------------
 *   make                # nutzt bme280.c aus Tag 1, falls im Verzeichnis
 *   make sim            # ohne Hardware, simulierter Sensor (auch auf x86)
 *
 * Bewusst KEINE externe Bibliothek: der HTTP-Post ist ein roher TCP-Socket.
 * Kein libcurl, kein Paket nachinstallieren, nichts, was am Kurstag fehlt.
 *
 * ---------------------------------------------------------------------------
 * TYPISCHER AUFRUF IM KURS
 * ---------------------------------------------------------------------------
 *   sudo ./stage3_telemetry --fifo 80 --mlock --cpu 3 \
 *        --influx 192.168.1.42:8086 --bucket echtzeit --org kurs \
 *        --token kurs-token-2026 --period 10 --duration 300 --csv lauf1.csv
 *
 * Ohne --influx laeuft es rein lokal (nur CSV + Konsole). Das ist der Fallback,
 * wenn das Netz beim Kunden quer schiesst.
 *
 * Lizenz: frei verwendbar im Rahmen des Seminars.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sched.h>
#include <pthread.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifdef HAVE_BME280
#include "bme280.h"
#endif

/* ========================================================================= */
/* Konstanten                                                                */
/* ========================================================================= */

#define NS_PRO_SEK        1000000000LL
#define RING_KAPAZITAET   4096          /* Zweierpotenz! Maskierung statt Modulo */
#define RING_MASKE        (RING_KAPAZITAET - 1)
#define BATCH_MAX         256           /* Messungen pro HTTP-Post              */
#define SENDER_INTERVALL_MS 500
#define LINE_MAX          256

/* ========================================================================= */
/* Messwert-Datensatz — bewusst flach, ohne Zeiger, ohne dynamischen Speicher */
/* ========================================================================= */

typedef struct {
    long long zaehler;
    long long t_unix_ns;     /* Wandzeit fuer InfluxDB (CLOCK_REALTIME)       */
    long      jitter_us;     /* Ist-Aufwachzeit minus Soll-Aufwachzeit        */
    long      arbeit_us;     /* Dauer des Sensor-Lesevorgangs                 */
    double    temperatur_c;
    double    druck_hpa;
    double    feuchte_pct;
    int       fehler;        /* 1 = Sensorlesung fehlgeschlagen               */
} messung_t;

/* ========================================================================= */
/* SPSC-Ringpuffer — der eigentliche Lehrgegenstand dieser Stufe             */
/* ========================================================================= */
/*
 * Warum das ohne Mutex sicher ist:
 *   - Genau EIN Producer (RT-Thread) schreibt kopf.
 *   - Genau EIN Consumer (Sender-Thread) schreibt schwanz.
 *   - Beide lesen den jeweils anderen Index mit acquire-Semantik, schreiben
 *     den eigenen mit release-Semantik. Das erzwingt die noetige Ordnung
 *     zwischen "Daten geschrieben" und "Index sichtbar gemacht".
 *   - Ein Mutex waere hier nicht nur unnoetig, sondern gefaehrlich: ein
 *     pthread_mutex_lock im RT-Thread kann ohne Priority Inheritance genau
 *     die Prioritaetsumkehr ausloesen, die wir an Tag 2 behandelt haben.
 */

typedef struct {
    messung_t         slot[RING_KAPAZITAET];
    _Atomic uint64_t  kopf;        /* naechster Schreibindex (nur Producer)   */
    _Atomic uint64_t  schwanz;     /* naechster Leseindex   (nur Consumer)    */
    _Atomic uint64_t  verworfen;   /* Ueberlauf-Zaehler                        */
} ring_t;

static ring_t g_ring;

/* Producer-Seite: NIEMALS blockierend, NIEMALS Syscall. */
static void ring_push(ring_t *r, const messung_t *m)
{
    uint64_t kopf    = atomic_load_explicit(&r->kopf,    memory_order_relaxed);
    uint64_t schwanz = atomic_load_explicit(&r->schwanz, memory_order_acquire);

    if (kopf - schwanz >= RING_KAPAZITAET) {
        /* Voll. Aelteste Messung verwerfen, statt den RT-Thread zu bremsen. */
        atomic_fetch_add_explicit(&r->verworfen, 1, memory_order_relaxed);
        atomic_store_explicit(&r->schwanz, schwanz + 1, memory_order_release);
    }
    r->slot[kopf & RING_MASKE] = *m;
    atomic_store_explicit(&r->kopf, kopf + 1, memory_order_release);
}

/* Consumer-Seite: liefert 1 bei Erfolg, 0 wenn leer. */
static int ring_pop(ring_t *r, messung_t *out)
{
    uint64_t schwanz = atomic_load_explicit(&r->schwanz, memory_order_relaxed);
    uint64_t kopf    = atomic_load_explicit(&r->kopf,    memory_order_acquire);
    if (schwanz == kopf) return 0;
    *out = r->slot[schwanz & RING_MASKE];
    atomic_store_explicit(&r->schwanz, schwanz + 1, memory_order_release);
    return 1;
}

/* ========================================================================= */
/* Laufzeit-Konfiguration                                                    */
/* ========================================================================= */

typedef struct {
    int   periode_ms;
    int   dauer_s;
    int   fifo_prio;      /* 0 = SCHED_OTHER beibehalten */
    int   cpu;            /* -1 = keine Affinitaet       */
    bool  mlock;
    bool  quiet;
    bool  demo_blocking;  /* HTTP im RT-Thread — absichtlich falsch */
    char  csv_pfad[256];
    char  influx_host[128];
    int   influx_port;
    char  bucket[64];
    char  org[64];
    char  token[128];
    char  host_tag[64];
    char  lauf_tag[64];
} konfig_t;

static volatile sig_atomic_t g_laeuft = 1;
static void sig_handler(int s) { (void)s; g_laeuft = 0; }

/* ========================================================================= */
/* Zeit-Helfer                                                               */
/* ========================================================================= */

static inline long long ts_zu_ns(const struct timespec *t)
{
    return (long long)t->tv_sec * NS_PRO_SEK + t->tv_nsec;
}

static inline void ts_addiere_ns(struct timespec *t, long long ns)
{
    t->tv_nsec += ns % NS_PRO_SEK;
    t->tv_sec  += ns / NS_PRO_SEK;
    if (t->tv_nsec >= NS_PRO_SEK) { t->tv_nsec -= NS_PRO_SEK; t->tv_sec += 1; }
}

/* ========================================================================= */
/* Sensor-Abstraktion: echt oder simuliert                                   */
/* ========================================================================= */
/*
 * Der simulierte Sensor ist kein Spielzeug — er hat einen Zweck:
 * Damit laeuft das gesamte Programm auf einem x86-Notebook ohne Hardware.
 * Der Teilnehmer arbeitet beruflich auf x86-Server-Linux; das ist die Bruecke.
 * Und im Notfall (Sensor kaputt, I2C blockiert) laeuft der Kurs weiter.
 */

typedef struct {
    int  fd;
    bool sim;
    double phase;
} sensor_t;

static int sensor_open(sensor_t *s, bool sim, const char *i2c_dev, int addr)
{
    memset(s, 0, sizeof(*s));
    s->sim = sim;
    if (sim) return 0;
#ifdef HAVE_BME280
    return bme280_open_dev(&s->fd, i2c_dev, addr);
#else
    (void)i2c_dev; (void)addr;
    fprintf(stderr,
            "Ohne HAVE_BME280 gebaut. Bitte 'make' im Verzeichnis mit bme280.c\n"
            "aufrufen oder --sim verwenden.\n");
    return -1;
#endif
}

static int sensor_read(sensor_t *s, messung_t *m)
{
    if (s->sim) {
        /* Deterministische, langsam schwingende Werte plus etwas Rauschen.
         * Bewusst mit einer kleinen busy-Schleife, damit die Arbeitszeit im
         * Zyklus nicht exakt null ist — sonst sieht die Statistik unrealistisch
         * schoen aus und die Teilnehmenden lernen das Falsche. */
        s->phase += 0.01;
        volatile double x = 0;
        for (int i = 0; i < 2000; i++) x += (double)i * 1.000001;
        (void)x;
        m->temperatur_c = 21.5 + 1.5 * __builtin_sin(s->phase);
        m->druck_hpa    = 1013.2 + 0.8 * __builtin_cos(s->phase * 0.3);
        m->feuchte_pct  = 44.0 + 3.0 * __builtin_sin(s->phase * 0.7);
        m->fehler = 0;
        return 0;
    }
#ifdef HAVE_BME280
    {
        bme280_reading_t r;
        if (bme280_read_dev(s->fd, &r) != 0) { m->fehler = 1; return -1; }
        m->temperatur_c = r.temperature_c;
        m->druck_hpa    = r.pressure_hpa;
        m->feuchte_pct  = r.humidity_pct;
        m->fehler = 0;
        return 0;
    }
#else
    m->fehler = 1;
    return -1;
#endif
}

/* ========================================================================= */
/* InfluxDB v2 Line Protocol ueber rohes HTTP                                */
/* ========================================================================= */
/*
 * Format einer Zeile:
 *   measurement,tag=wert feld=wert,feld2=wert  <unix-nanosekunden>
 *
 * Beispiel:
 *   rtmess,host=pi4,lauf=stufe3 jitter_us=12,arbeit_us=880,temp_c=21.83 1724... 
 *
 * Warum ohne libcurl: eine Abhaengigkeit weniger, die am Kurstag fehlen kann,
 * und der Teilnehmer sieht, dass hinter "Datenbank-Client" nur ein POST steckt.
 */

static int influx_zeile(char *buf, size_t n, const konfig_t *k, const messung_t *m)
{
    return snprintf(buf, n,
        "rtmess,host=%s,lauf=%s "
        "jitter_us=%ldi,arbeit_us=%ldi,temp_c=%.3f,druck_hpa=%.2f,feuchte_pct=%.2f,fehler=%di "
        "%lld\n",
        k->host_tag, k->lauf_tag,
        m->jitter_us, m->arbeit_us,
        m->temperatur_c, m->druck_hpa, m->feuchte_pct, m->fehler,
        m->t_unix_ns);
}

static int tcp_verbinden(const char *host, int port, int timeout_s)
{
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }

    struct timeval tv = { .tv_sec = timeout_s, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* Gibt 0 bei HTTP 2xx zurueck, sonst -1. Blockierend — deshalb NIE im RT-Pfad. */
static int influx_post(const konfig_t *k, const char *body, size_t bodylen)
{
    if (k->influx_host[0] == '\0') return 0;   /* Telemetrie deaktiviert */

    int fd = tcp_verbinden(k->influx_host, k->influx_port, 3);
    if (fd < 0) return -1;

    char kopf[768];
    int kl = snprintf(kopf, sizeof kopf,
        "POST /api/v2/write?org=%s&bucket=%s&precision=ns HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Authorization: Token %s\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        k->org, k->bucket, k->influx_host, k->influx_port, k->token, bodylen);

    if (write(fd, kopf, (size_t)kl) != kl)            { close(fd); return -1; }
    if (write(fd, body, bodylen) != (ssize_t)bodylen) { close(fd); return -1; }

    char antwort[256];
    ssize_t n = read(fd, antwort, sizeof antwort - 1);
    close(fd);
    if (n <= 0) return -1;
    antwort[n] = '\0';
    /* "HTTP/1.1 204 No Content" ist der Erfolgsfall bei InfluxDB v2. */
    return (strstr(antwort, " 20") != NULL) ? 0 : -1;
}

/* ========================================================================= */
/* Sender-Thread — normale Prioritaet, darf alles, was der RT-Thread nicht darf */
/* ========================================================================= */

typedef struct {
    const konfig_t *k;
    _Atomic long long gesendet;
    _Atomic long long sende_fehler;
} sender_ctx_t;

static void *sender_thread(void *arg)
{
    sender_ctx_t *ctx = (sender_ctx_t *)arg;
    const konfig_t *k = ctx->k;

    /* Ausdruecklich NICHT auf den isolierten Kern. Der Sender gehoert zum
     * Housekeeping — genau die Trennung, die an Tag 2 gebaut wurde. */
    static char body[BATCH_MAX * LINE_MAX];

    while (g_laeuft || atomic_load(&g_ring.kopf) != atomic_load(&g_ring.schwanz)) {
        size_t len = 0;
        int    anzahl = 0;
        messung_t m;

        while (anzahl < BATCH_MAX && ring_pop(&g_ring, &m)) {
            int w = influx_zeile(body + len, sizeof(body) - len, k, &m);
            if (w <= 0 || (size_t)w >= sizeof(body) - len) break;
            len += (size_t)w;
            anzahl++;
        }

        if (anzahl > 0) {
            if (influx_post(k, body, len) == 0)
                atomic_fetch_add(&ctx->gesendet, anzahl);
            else
                atomic_fetch_add(&ctx->sende_fehler, anzahl);
        }

        struct timespec pause = { .tv_sec = 0,
                                  .tv_nsec = SENDER_INTERVALL_MS * 1000000L };
        nanosleep(&pause, NULL);
        if (!g_laeuft && anzahl == 0) break;
    }
    return NULL;
}

/* ========================================================================= */
/* Echtzeit-Einstellungen                                                    */
/* ========================================================================= */

static int rt_konfigurieren(const konfig_t *k)
{
    if (k->mlock) {
        if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
            fprintf(stderr, "WARNUNG mlockall: %s\n", strerror(errno));
        }
    }
    if (k->cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(k->cpu, &set);
        if (sched_setaffinity(0, sizeof set, &set) != 0) {
            fprintf(stderr, "FEHLER sched_setaffinity(cpu %d): %s\n",
                    k->cpu, strerror(errno));
            return -1;
        }
    }
    if (k->fifo_prio > 0) {
        struct sched_param p = { .sched_priority = k->fifo_prio };
        if (sched_setscheduler(0, SCHED_FIFO, &p) != 0) {
            fprintf(stderr,
                    "FEHLER sched_setscheduler(SCHED_FIFO %d): %s\n"
                    "Hinweis: als root starten (sudo) oder RLIMIT_RTPRIO setzen.\n",
                    k->fifo_prio, strerror(errno));
            return -1;
        }
    }
    return 0;
}

/* Beruehrt den Stack einmal vor, damit spaeter keine Page-Faults auftreten.
 * Ohne mlockall waere das wirkungslos — beides gehoert zusammen. */
static void stack_vorwaermen(void)
{
    volatile unsigned char puffer[64 * 1024];
    memset((void *)puffer, 0, sizeof puffer);
}

/* ========================================================================= */
/* Hilfe                                                                     */
/* ========================================================================= */

static void hilfe(const char *prog)
{
    printf(
"stage3_telemetry — Echtzeit-Messsystem, Stufe 3\n"
"\n"
"Aufruf: %s [Optionen]\n"
"\n"
"Echtzeit:\n"
"  --fifo <prio>        SCHED_FIFO mit Prioritaet (1..99), Empfehlung 80\n"
"  --cpu <n>            Auf CPU n festnageln (der isolierte Kern, meist 3)\n"
"  --mlock              mlockall(MCL_CURRENT|MCL_FUTURE)\n"
"  --period <ms>        Zykluszeit in Millisekunden (Standard 10)\n"
"  --duration <s>       Laufzeit in Sekunden, 0 = bis Strg-C (Standard 0)\n"
"\n"
"Sensor:\n"
"  --sim                Simulierter Sensor statt BME280 (kein I2C noetig)\n"
"  --i2c <pfad>         I2C-Geraet (Standard /dev/i2c-1)\n"
"  --addr <hex>         I2C-Adresse (Standard 0x76)\n"
"\n"
"Telemetrie:\n"
"  --influx <host:port> InfluxDB-Ziel. Ohne diese Option: nur lokal.\n"
"  --org <name>         InfluxDB-Organisation (Standard kurs)\n"
"  --bucket <name>      InfluxDB-Bucket (Standard echtzeit)\n"
"  --token <token>      InfluxDB-Token\n"
"  --tag-host <name>    Wert des host-Tags (Standard: Hostname)\n"
"  --tag-lauf <name>    Wert des lauf-Tags, z.B. 'ohne_fifo' (Standard lauf1)\n"
"\n"
"Sonstiges:\n"
"  --csv <datei>        Rohdaten zusaetzlich als CSV schreiben\n"
"  --demo-blocking      FALSCHE Variante: HTTP-Post im RT-Thread. Nur zur\n"
"                       Demonstration — zeigt den Latenzschaden in Zahlen.\n"
"  --quiet              Keine laufende Konsolenausgabe\n"
"  --help               Diese Hilfe\n"
"\n"
"Beispiel (Kurs, Stufe 3):\n"
"  sudo %s --fifo 80 --mlock --cpu 3 --period 10 --duration 300 \\\n"
"       --influx 192.168.1.42:8086 --org kurs --bucket echtzeit \\\n"
"       --token kurs-token-2026 --tag-lauf stufe3 --csv stufe3.csv\n",
    prog, prog);
}

/* ========================================================================= */
/* main                                                                      */
/* ========================================================================= */

int main(int argc, char **argv)
{
    konfig_t k;
    memset(&k, 0, sizeof k);
    k.periode_ms  = 10;
    k.dauer_s     = 0;
    k.fifo_prio   = 0;
    k.cpu         = -1;
    k.influx_port = 8086;
    strcpy(k.org,      "kurs");
    strcpy(k.bucket,   "echtzeit");
    strcpy(k.lauf_tag, "lauf1");
    gethostname(k.host_tag, sizeof k.host_tag - 1);

    bool sim = false;
    char i2c_dev[128] = "/dev/i2c-1";
    int  i2c_addr = 0x76;

    static struct option opts[] = {
        {"fifo",      required_argument, 0, 'f'},
        {"cpu",       required_argument, 0, 'c'},
        {"mlock",     no_argument,       0, 'm'},
        {"period",    required_argument, 0, 'p'},
        {"duration",  required_argument, 0, 'd'},
        {"sim",       no_argument,       0, 'S'},
        {"i2c",       required_argument, 0, 'I'},
        {"addr",      required_argument, 0, 'A'},
        {"influx",    required_argument, 0, 'x'},
        {"org",       required_argument, 0, 'o'},
        {"bucket",    required_argument, 0, 'b'},
        {"token",     required_argument, 0, 't'},
        {"tag-host",  required_argument, 0, 'H'},
        {"tag-lauf",  required_argument, 0, 'L'},
        {"csv",       required_argument, 0, 'v'},
        {"demo-blocking", no_argument,   0, 'B'},
        {"quiet",     no_argument,       0, 'q'},
        {"help",      no_argument,       0, 'h'},
        {0,0,0,0}
    };

    int c, idx;
    while ((c = getopt_long(argc, argv, "", opts, &idx)) != -1) {
        switch (c) {
        case 'f': k.fifo_prio  = atoi(optarg); break;
        case 'c': k.cpu        = atoi(optarg); break;
        case 'm': k.mlock      = true;         break;
        case 'p': k.periode_ms = atoi(optarg); break;
        case 'd': k.dauer_s    = atoi(optarg); break;
        case 'S': sim = true;                  break;
        case 'I': snprintf(i2c_dev, sizeof i2c_dev, "%s", optarg); break;
        case 'A': i2c_addr = (int)strtol(optarg, NULL, 0); break;
        case 'x': {
            char *doppel = strrchr(optarg, ':');
            if (doppel) { *doppel = '\0'; k.influx_port = atoi(doppel + 1); }
            snprintf(k.influx_host, sizeof k.influx_host, "%s", optarg);
            break;
        }
        case 'o': snprintf(k.org,      sizeof k.org,      "%s", optarg); break;
        case 'b': snprintf(k.bucket,   sizeof k.bucket,   "%s", optarg); break;
        case 't': snprintf(k.token,    sizeof k.token,    "%s", optarg); break;
        case 'H': snprintf(k.host_tag, sizeof k.host_tag, "%s", optarg); break;
        case 'L': snprintf(k.lauf_tag, sizeof k.lauf_tag, "%s", optarg); break;
        case 'v': snprintf(k.csv_pfad, sizeof k.csv_pfad, "%s", optarg); break;
        case 'B': k.demo_blocking = true; break;
        case 'q': k.quiet = true; break;
        case 'h': hilfe(argv[0]); return 0;
        default:  hilfe(argv[0]); return 2;
        }
    }

    if (k.periode_ms <= 0) { fprintf(stderr, "--period muss > 0 sein\n"); return 2; }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    sensor_t sensor;
    if (sensor_open(&sensor, sim, i2c_dev, i2c_addr) != 0) return 1;

    FILE *csv = NULL;
    if (k.csv_pfad[0]) {
        csv = fopen(k.csv_pfad, "w");
        if (!csv) { fprintf(stderr, "CSV: %s\n", strerror(errno)); return 1; }
        fprintf(csv, "n,jitter_us,arbeit_us,temp_c,druck_hpa,feuchte_pct\n");
    }

    if (rt_konfigurieren(&k) != 0) return 1;
    stack_vorwaermen();

    /* Sender-Thread nur starten, wenn wir NICHT die falsche Variante zeigen. */
    pthread_t sender_tid;
    sender_ctx_t sctx = { .k = &k, .gesendet = 0, .sende_fehler = 0 };
    bool sender_aktiv = (!k.demo_blocking && k.influx_host[0] != '\0');
    if (sender_aktiv) {
        if (pthread_create(&sender_tid, NULL, sender_thread, &sctx) != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(errno));
            return 1;
        }
    }

    printf("stage3_telemetry | Periode %d ms | %s | CPU %s | mlock %s | Sensor %s\n",
           k.periode_ms,
           k.fifo_prio ? "SCHED_FIFO" : "SCHED_OTHER",
           k.cpu >= 0 ? "fest" : "frei",
           k.mlock ? "an" : "aus",
           sim ? "SIM" : "BME280");
    if (k.influx_host[0])
        printf("Telemetrie -> %s:%d  bucket=%s  org=%s  modus=%s\n",
               k.influx_host, k.influx_port, k.bucket, k.org,
               k.demo_blocking ? "BLOCKIEREND IM RT-THREAD (Demo)" : "entkoppelt");
    else
        printf("Telemetrie: aus (nur lokal)\n");
    printf("Abbruch mit Strg-C.\n\n");

    /* ---------------------------------------------------------------- Schleife */
    struct timespec naechste, jetzt;
    clock_gettime(CLOCK_MONOTONIC, &naechste);
    long long start_ns   = ts_zu_ns(&naechste);
    long long periode_ns = (long long)k.periode_ms * 1000000LL;
    ts_addiere_ns(&naechste, periode_ns);

    long long zaehler = 0, jitter_summe = 0, jitter_quad = 0;
    long jitter_max = -1000000, jitter_min = 1000000, arbeit_max = 0;
    long long verpasst = 0, lesefehler = 0;

    static char einzelzeile[LINE_MAX];

    while (g_laeuft) {
        int sr = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &naechste, NULL);
        if (sr != 0 && sr != EINTR) {
            fprintf(stderr, "clock_nanosleep: %s\n", strerror(sr));
            break;
        }
        if (!g_laeuft) break;

        clock_gettime(CLOCK_MONOTONIC, &jetzt);
        long jitter_us = (long)((ts_zu_ns(&jetzt) - ts_zu_ns(&naechste)) / 1000);

        messung_t m;
        memset(&m, 0, sizeof m);
        struct timespec wand;
        clock_gettime(CLOCK_REALTIME, &wand);
        m.t_unix_ns = ts_zu_ns(&wand);
        m.jitter_us = jitter_us;

        struct timespec a0 = jetzt, a1;
        if (sensor_read(&sensor, &m) != 0) lesefehler++;
        clock_gettime(CLOCK_MONOTONIC, &a1);
        m.arbeit_us = (long)((ts_zu_ns(&a1) - ts_zu_ns(&a0)) / 1000);
        m.zaehler   = ++zaehler;

        /* --- Der entscheidende Unterschied ------------------------------- */
        if (k.demo_blocking) {
            /* FALSCH, absichtlich: Netzwerk-I/O im RT-Thread. */
            int w = influx_zeile(einzelzeile, sizeof einzelzeile, &k, &m);
            if (w > 0) influx_post(&k, einzelzeile, (size_t)w);
        } else {
            /* RICHTIG: nur in den Ring legen, ohne Syscall, ohne Blockieren. */
            ring_push(&g_ring, &m);
        }
        /* ----------------------------------------------------------------- */

        if (m.arbeit_us > arbeit_max) arbeit_max = m.arbeit_us;
        jitter_summe += jitter_us;
        jitter_quad  += (long long)jitter_us * jitter_us;
        if (jitter_us > jitter_max) jitter_max = jitter_us;
        if (jitter_us < jitter_min) jitter_min = jitter_us;
        if (jitter_us + m.arbeit_us > (long)k.periode_ms * 1000L) verpasst++;

        if (csv)
            fprintf(csv, "%lld,%ld,%ld,%.3f,%.2f,%.2f\n",
                    m.zaehler, m.jitter_us, m.arbeit_us,
                    m.temperatur_c, m.druck_hpa, m.feuchte_pct);

        if (!k.quiet && (zaehler % 50 == 0)) {
            printf("[%7lld] T=%6.2f C | Jitter=%+6ld us (max %+6ld) | "
                   "Arbeit=%5ld us | Ring verworfen=%llu\n",
                   zaehler, m.temperatur_c, m.jitter_us, jitter_max, m.arbeit_us,
                   (unsigned long long)atomic_load(&g_ring.verworfen));
            fflush(stdout);
        }

        ts_addiere_ns(&naechste, periode_ns);
        if (k.dauer_s > 0 && (ts_zu_ns(&jetzt) - start_ns) >= (long long)k.dauer_s * NS_PRO_SEK)
            break;
    }

    g_laeuft = 0;
    if (sender_aktiv) pthread_join(sender_tid, NULL);
    if (csv) fclose(csv);

    /* ---------------------------------------------------------------- Bilanz */
    double mittel  = zaehler ? (double)jitter_summe / (double)zaehler : 0.0;
    double varianz = zaehler ? (double)jitter_quad / (double)zaehler - mittel * mittel : 0.0;
    double stdabw  = varianz > 0 ? __builtin_sqrt(varianz) : 0.0;

    printf("\n===================== Bilanz =====================\n");
    printf("Zyklen              : %lld\n", zaehler);
    printf("Jitter min/mit/max  : %+ld / %+.1f / %+ld us\n", jitter_min, mittel, jitter_max);
    printf("Jitter Standardabw. : %.1f us\n", stdabw);
    printf("Arbeit max          : %ld us\n", arbeit_max);
    printf("Deadlines verpasst  : %lld\n", verpasst);
    printf("Sensor-Lesefehler   : %lld\n", lesefehler);
    printf("Ring verworfen      : %llu\n", (unsigned long long)atomic_load(&g_ring.verworfen));
    if (k.influx_host[0]) {
        printf("Telemetrie gesendet : %lld\n", (long long)atomic_load(&sctx.gesendet));
        printf("Telemetrie Fehler   : %lld\n", (long long)atomic_load(&sctx.sende_fehler));
    }
    printf("==================================================\n");
    printf("\nMerksatz: der maximale Jitter ist die Zahl, die zaehlt.\n"
           "Ein guter Mittelwert bei schlechtem Maximum ist ein kaputtes System\n"
           "mit schoener Statistik.\n");
    return 0;
}
