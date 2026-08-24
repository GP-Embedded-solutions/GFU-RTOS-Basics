/* ===========================================================================
 * bme280.h -- Minimaler BME280-Treiber ueber /dev/i2c-N (Linux userspace)
 *
 * Bewusst OHNE Fremdbibliothek geschrieben: Die Teilnehmenden sollen sehen,
 * was ein I2C-Zugriff tatsaechlich ist -- ein ioctl() und ein write()/read().
 * Kein magisches Framework dazwischen.
 *
 * Kurs "Echtzeit-Linux", Tag 1, Lernprojekt Stufe 1
 * ======================================================================== */
#ifndef BME280_H
#define BME280_H

#include <stdint.h>

/* Standard-I2C-Adressen. SDO auf GND -> 0x76, SDO auf VCC -> 0x77 */
#define BME280_ADDR_PRIMARY    0x76
#define BME280_ADDR_SECONDARY  0x77

/* Chip-ID im Register 0xD0 */
#define BME280_CHIP_ID         0x60   /* BME280: Temperatur, Druck, Feuchte */
#define BMP280_CHIP_ID         0x58   /* BMP280: ohne Feuchtesensor        */

typedef struct {
    int      fd;          /* Dateideskriptor auf /dev/i2c-N */
    uint8_t  addr;        /* 7-Bit-I2C-Adresse              */
    uint8_t  chip_id;     /* gelesene Chip-ID               */
    int      has_humidity;/* 1 bei BME280, 0 bei BMP280     */

    /* Werkskalibrierung, im Sensor-ROM abgelegt */
    uint16_t dig_T1; int16_t dig_T2, dig_T3;
    uint16_t dig_P1; int16_t dig_P2, dig_P3, dig_P4, dig_P5,
                             dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  dig_H1; int16_t dig_H2; uint8_t dig_H3;
    int16_t  dig_H4, dig_H5; int8_t dig_H6;

    int32_t  t_fine;      /* Zwischenwert, koppelt T an P und H */
} bme280_t;

typedef struct {
    double temperature_c;   /* Grad Celsius       */
    double pressure_hpa;    /* Hektopascal        */
    double humidity_pct;    /* Prozent rel. Feuchte (0 falls BMP280) */
} bme280_reading_t;

/* Oeffnet den Bus, prueft die Chip-ID, liest die Kalibrierung
 * und konfiguriert den Sensor.
 * addr = 0 -> automatische Erkennung (0x76, dann 0x77).
 * Rueckgabe: 0 = Erfolg, negativ = Fehler. */
int bme280_open(bme280_t *dev, const char *i2c_device, uint8_t addr);

/* Loest eine Einzelmessung aus (forced mode) und liefert kompensierte Werte.
 * Rueckgabe: 0 = Erfolg, negativ = Fehler.
 *
 * ACHTUNG (Kursthema!): Diese Funktion BLOCKIERT waehrend der Wandlungszeit
 * des Sensors. Sie ist NICHT echtzeitfaehig im harten Sinn. Genau darum geht
 * es in der Diskussion: Die Datenerfassung darf langsam sein -- das ZEIT-
 * VERHALTEN der Regelschleife muss deterministisch sein. Zwei getrennte
 * Probleme. */
int bme280_read(bme280_t *dev, bme280_reading_t *out);

/* Schliesst den Bus. */
void bme280_close(bme280_t *dev);

/* Menschenlesbare Fehlerbeschreibung zum letzten negativen Rueckgabewert. */
const char *bme280_strerror(int err);

#endif /* BME280_H */
