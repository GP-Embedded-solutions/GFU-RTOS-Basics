/* ===========================================================================
 * bme280.c -- Implementierung. Kompensationsformeln nach Bosch-Datenblatt
 *             (BST-BME280-DS002), Abschnitt 4.2.3 / Appendix A.
 * ======================================================================== */
#define _POSIX_C_SOURCE 200809L

#include "bme280.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

/* ------------------------------------------------------------- Registerkarte */
#define REG_CHIP_ID     0xD0
#define REG_RESET       0xE0
#define REG_CALIB00     0x88   /* 26 Bytes: 0x88..0xA1 */
#define REG_CALIB26     0xE1   /* 7 Bytes:  0xE1..0xE7 */
#define REG_CTRL_HUM    0xF2
#define REG_STATUS      0xF3
#define REG_CTRL_MEAS   0xF4
#define REG_CONFIG      0xF5
#define REG_DATA        0xF7   /* 8 Bytes: press(3) temp(3) hum(2) */

#define ERR_OPEN        -1
#define ERR_IOCTL       -2
#define ERR_IO          -3
#define ERR_CHIPID      -4
#define ERR_TIMEOUT     -5

/* ------------------------------------------------------------- I2C-Primitive */

static int i2c_write_reg(int fd, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return (write(fd, buf, 2) == 2) ? 0 : ERR_IO;
}

static int i2c_read_regs(int fd, uint8_t reg, uint8_t *buf, size_t len)
{
    if (write(fd, &reg, 1) != 1)                 return ERR_IO;
    if (read(fd, buf, len) != (ssize_t)len)      return ERR_IO;
    return 0;
}

/* ------------------------------------------------------- Kalibrierung lesen */

static int read_calibration(bme280_t *d)
{
    uint8_t c[26], h[7];
    int rc;

    if ((rc = i2c_read_regs(d->fd, REG_CALIB00, c, 26)) != 0) return rc;

    d->dig_T1 = (uint16_t)(c[1]  << 8 | c[0]);
    d->dig_T2 = (int16_t) (c[3]  << 8 | c[2]);
    d->dig_T3 = (int16_t) (c[5]  << 8 | c[4]);
    d->dig_P1 = (uint16_t)(c[7]  << 8 | c[6]);
    d->dig_P2 = (int16_t) (c[9]  << 8 | c[8]);
    d->dig_P3 = (int16_t) (c[11] << 8 | c[10]);
    d->dig_P4 = (int16_t) (c[13] << 8 | c[12]);
    d->dig_P5 = (int16_t) (c[15] << 8 | c[14]);
    d->dig_P6 = (int16_t) (c[17] << 8 | c[16]);
    d->dig_P7 = (int16_t) (c[19] << 8 | c[18]);
    d->dig_P8 = (int16_t) (c[21] << 8 | c[20]);
    d->dig_P9 = (int16_t) (c[23] << 8 | c[22]);
    /* c[24] ist reserviert */
    d->dig_H1 = c[25];

    if (!d->has_humidity) return 0;

    if ((rc = i2c_read_regs(d->fd, REG_CALIB26, h, 7)) != 0) return rc;

    d->dig_H2 = (int16_t)(h[1] << 8 | h[0]);
    d->dig_H3 = h[2];
    /* dig_H4 und dig_H5 teilen sich ein Byte -- klassische Datenblatt-Falle */
    d->dig_H4 = (int16_t)((int8_t)h[3] * 16 | (h[4] & 0x0F));
    d->dig_H5 = (int16_t)((int8_t)h[5] * 16 | (h[4] >> 4));
    d->dig_H6 = (int8_t)h[6];

    return 0;
}

/* ---------------------------------------------------- Kompensation (Datenblatt) */

static int32_t compensate_temperature(bme280_t *d, int32_t adc_T)
{
    int32_t var1, var2, T;
    var1 = ((((adc_T >> 3) - ((int32_t)d->dig_T1 << 1))) * ((int32_t)d->dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)d->dig_T1)) *
              ((adc_T >> 4) - ((int32_t)d->dig_T1))) >> 12) * ((int32_t)d->dig_T3)) >> 14;
    d->t_fine = var1 + var2;
    T = (d->t_fine * 5 + 128) >> 8;
    return T;                       /* Einheit: 0,01 Grad C */
}

static uint32_t compensate_pressure(bme280_t *d, int32_t adc_P)
{
    int64_t var1, var2, p;
    var1 = ((int64_t)d->t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)d->dig_P6;
    var2 = var2 + ((var1 * (int64_t)d->dig_P5) << 17);
    var2 = var2 + (((int64_t)d->dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)d->dig_P3) >> 8) +
           ((var1 * (int64_t)d->dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)d->dig_P1) >> 33;
    if (var1 == 0) return 0;        /* Division durch null vermeiden */
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)d->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)d->dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)d->dig_P7) << 4);
    return (uint32_t)p;             /* Einheit: 1/256 Pa */
}

static uint32_t compensate_humidity(bme280_t *d, int32_t adc_H)
{
    int32_t v;
    v = d->t_fine - ((int32_t)76800);
    v = (((((adc_H << 14) - (((int32_t)d->dig_H4) << 20) -
            (((int32_t)d->dig_H5) * v)) + ((int32_t)16384)) >> 15) *
         (((((((v * ((int32_t)d->dig_H6)) >> 10) *
              (((v * ((int32_t)d->dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
            ((int32_t)2097152)) * ((int32_t)d->dig_H2) + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)d->dig_H1)) >> 4);
    v = (v < 0)        ? 0        : v;
    v = (v > 419430400)? 419430400: v;
    return (uint32_t)(v >> 12);     /* Einheit: 1/1024 % rF */
}

/* ------------------------------------------------------------- Oeffentliche API */

int bme280_open(bme280_t *d, const char *i2c_device, uint8_t addr)
{
    uint8_t candidates[2];
    int n_cand, i, rc;

    memset(d, 0, sizeof(*d));

    d->fd = open(i2c_device ? i2c_device : "/dev/i2c-1", O_RDWR);
    if (d->fd < 0) return ERR_OPEN;

    if (addr != 0) { candidates[0] = addr; n_cand = 1; }
    else { candidates[0] = BME280_ADDR_PRIMARY;
           candidates[1] = BME280_ADDR_SECONDARY; n_cand = 2; }

    for (i = 0; i < n_cand; i++) {
        uint8_t id = 0;
        if (ioctl(d->fd, I2C_SLAVE, candidates[i]) < 0) continue;
        if (i2c_read_regs(d->fd, REG_CHIP_ID, &id, 1) != 0) continue;
        if (id == BME280_CHIP_ID || id == BMP280_CHIP_ID) {
            d->addr         = candidates[i];
            d->chip_id      = id;
            d->has_humidity = (id == BME280_CHIP_ID);
            break;
        }
    }
    if (d->addr == 0) { close(d->fd); d->fd = -1; return ERR_CHIPID; }

    if ((rc = read_calibration(d)) != 0) { close(d->fd); d->fd = -1; return rc; }

    /* Konfiguration:
     *   ctrl_hum  = 0x01  -> Feuchte-Oversampling x1
     *   config    = 0x00  -> IIR-Filter aus, kuerzeste Standby-Zeit
     *                        (Filter AUS ist hier Absicht: er glaettet
     *                         zwar, verzoegert aber -- in einem Echtzeit-
     *                         kurs ein diskussionswuerdiger Trade-off)
     * ctrl_meas wird pro Messung gesetzt (forced mode). */
    if ((rc = i2c_write_reg(d->fd, REG_CTRL_HUM, 0x01)) != 0) return rc;
    if ((rc = i2c_write_reg(d->fd, REG_CONFIG,   0x00)) != 0) return rc;

    return 0;
}

int bme280_read(bme280_t *d, bme280_reading_t *out)
{
    uint8_t raw[8], status;
    int32_t adc_T, adc_P, adc_H;
    int rc, tries;

    if (d->fd < 0) return ERR_OPEN;

    /* Forced mode ausloesen:
     * osrs_t = x1 (001), osrs_p = x1 (001), mode = forced (01)
     * -> 0b001_001_01 = 0x25 */
    if ((rc = i2c_write_reg(d->fd, REG_CTRL_MEAS, 0x25)) != 0) return rc;

    /* Auf Wandlungsende warten. Bei Oversampling x1 typisch unter 10 ms.
     * Hier wird bewusst gepollt statt blind zu schlafen -- und genau hier
     * sitzt die Nicht-Determinismus-Quelle, ueber die im Kurs geredet wird. */
    for (tries = 0; tries < 100; tries++) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000L }; /* 1 ms */
        nanosleep(&ts, NULL);
        if ((rc = i2c_read_regs(d->fd, REG_STATUS, &status, 1)) != 0) return rc;
        if ((status & 0x08) == 0) break;   /* Bit 3 = measuring */
    }
    if (tries >= 100) return ERR_TIMEOUT;

    if ((rc = i2c_read_regs(d->fd, REG_DATA, raw, 8)) != 0) return rc;

    adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    adc_H = ((int32_t)raw[6] << 8)  |  (int32_t)raw[7];

    /* Reihenfolge ist zwingend: Temperatur zuerst, weil sie t_fine setzt,
     * das Druck und Feuchte brauchen. Klassische Fehlerquelle. */
    out->temperature_c = compensate_temperature(d, adc_T) / 100.0;
    out->pressure_hpa  = compensate_pressure(d, adc_P) / 256.0 / 100.0;
    out->humidity_pct  = d->has_humidity
                       ? compensate_humidity(d, adc_H) / 1024.0
                       : 0.0;
    return 0;
}

void bme280_close(bme280_t *d)
{
    if (d && d->fd >= 0) { close(d->fd); d->fd = -1; }
}

const char *bme280_strerror(int err)
{
    switch (err) {
    case 0:           return "Erfolg";
    case ERR_OPEN:    return "I2C-Geraet konnte nicht geoeffnet werden "
                             "(existiert /dev/i2c-1? Ist der Benutzer in der Gruppe 'i2c'?)";
    case ERR_IOCTL:   return "ioctl(I2C_SLAVE) fehlgeschlagen";
    case ERR_IO:      return "I2C-Uebertragungsfehler (Verkabelung pruefen)";
    case ERR_CHIPID:  return "Kein BME280/BMP280 gefunden (i2cdetect -y 1 ausfuehren)";
    case ERR_TIMEOUT: return "Sensor-Wandlung ohne Timeout nicht abgeschlossen";
    default:          return "Unbekannter Fehler";
    }
}
