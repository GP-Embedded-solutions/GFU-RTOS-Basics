# Lernprojekt „Echtzeit-Messsystem" — Stufe 2
### Aufgabenblatt · Tag 2, 15:00–15:45 · Echtzeit-Linux

> **Ausgangspunkt:** Ihr Programm aus Stufe 1 liest den Sensor zyklisch aus. Es läuft als normaler Prozess, auf einem beliebigen Kern, mit normaler Priorität.
> **Ziel:** Am Ende dieser Stunde läuft dasselbe Programm deterministisch — und Sie können **belegen**, welche Maßnahme wie viel gebracht hat.
>
> **Die eiserne Regel dieser Übung: eine Änderung, eine Messung.** Wer drei Schalter gleichzeitig umlegt, hat am Ende bessere Zahlen und kein Wissen.

---

## Schritt 1 · Informieren (5 Min.)

Bevor Sie irgendetwas ändern, beantworten Sie sich diese drei Fragen schriftlich — zwei Zeilen genügen:

1. Welchen Zykluszeit-Sollwert hat mein Programm, und was ist die Konsequenz, wenn ein Zyklus zu spät kommt?
2. Welche der vier Störquellen von heute Vormittag betrifft mein Programm am wahrscheinlichsten?
3. Welche Priorität wähle ich — und **warum diese Zahl** und nicht 99?

> Diese drei Antworten sind Teil der Abnahme. Nicht, weil Papier wichtig ist, sondern weil eine Prioritätsvergabe ohne Begründung geraten ist.

---

## Schritt 2 · Planen (5 Min.)

Legen Sie die Reihenfolge Ihrer vier Messungen fest. Vorschlag — Sie dürfen abweichen, aber begründen:

| # | Was Sie ändern | Befehl / Schalter |
|---|---|---|
| M0 | nichts — **Basismessung** | `./stage1_reference --csv m0.csv` |
| M1 | nur Scheduling-Policy | `sudo ./stage1_reference --fifo 80 --csv m1.csv` |
| M2 | zusätzlich: Kern festlegen | `sudo ./stage1_reference --fifo 80 --cpu 3 --csv m2.csv` |
| M3 | zusätzlich: Speicher festnageln | `sudo ./stage1_reference --fifo 80 --cpu 3 --mlock --csv m3.csv` |

> *(Falls die Schalter bei Ihnen anders heißen: `./stage1_reference --help`)*

**Wichtig:** Jede Messung mindestens **60 Sekunden** und **unter Last**. Die Last erzeugen Sie in einem zweiten Terminal:
```bash
sudo stress-ng --cpu 4 --io 2 --vm 1 --vm-bytes 128M --timeout 70s
```
Ohne Last messen Sie den Normalfall. Der interessiert uns nicht.

---

## Schritt 3 · Entscheiden (5 Min.)

Bevor Sie messen: **Schreiben Sie Ihre Erwartung auf.** Für jede der drei Änderungen ein Wort: *stark / mittel / kaum*.

Das ist kein Ritual. Am Ende vergleichen Sie Erwartung mit Ergebnis — und genau in der Differenz steckt das, was Sie heute wirklich gelernt haben.

---

## Schritt 4 · Ausführen (20 Min.)

Führen Sie M0 bis M3 durch. Tragen Sie nach jedem Lauf ein:

| Messung | max. Jitter (µs) | Ihre Erwartung | Überraschung? |
|---|---|---|---|
| M0 Basis | | — | |
| M1 + FIFO 80 | | | |
| M2 + Kern 3 | | | |
| M3 + mlock | | | |

**Auswertung des CSV** (Beispiel, passen Sie die Spalte an):
```bash
awk -F, 'NR>1 {if ($2>max) max=$2; s+=$2; n++} END {printf "max %.1f us, avg %.1f us, n=%d\n", max, s/n, n}' m1.csv
```

**Stolpersteine, die Sie erwarten dürfen:**
- `--fifo` ohne `sudo` schlägt fehl. SCHED_FIFO braucht Rechte.
- `--cpu 3` funktioniert nur, wenn Kern 3 tatsächlich isoliert ist. Prüfen: `cat /sys/devices/system/cpu/isolated`
- Wenn der maximale Jitter zwischen zwei identischen Läufen um mehr als 30 % schwankt, ist Ihre Messdauer zu kurz oder Ihre Last nicht reproduzierbar.

---

## Schritt 5 · Kontrollieren (5 Min.)

Prüfen Sie selbst, bevor Sie melden:

- [ ] Ich habe vier Messungen, jede mindestens 60 s, jede unter Last.
- [ ] Ich habe pro Schritt **genau eine** Sache geändert.
- [ ] Ich vergleiche **Maxima**, nicht Mittelwerte.
- [ ] Ich kann sagen, welche Änderung am meisten gebracht hat.
- [ ] Ich kann sagen, welche Änderung **kaum etwas** gebracht hat — und eine Vermutung nennen, warum.
- [ ] Mein Programm läuft nach dem Umbau immer noch korrekt: die Sensorwerte sind plausibel und keine Zyklen fehlen.

> Der vorletzte Punkt ist der wichtigste. Eine der drei Maßnahmen bringt auf diesem Board wenig. Das herauszufinden ist kein Nebenprodukt, das ist die Aufgabe.

---

## Schritt 6 · Bewerten (5 Min.)

Zwei Sätze, mündlich:

1. „Auf diesem System bringt die größte Verbesserung ______, weil ______."
2. „In meinem eigenen Projekt würde ich als Erstes ______ ändern, weil ______."

Der zweite Satz wandert auf Ihre Karte von Tag 1 und wird morgen in der Transfersicherung wieder aufgegriffen.

---

## Wenn Sie schneller fertig sind (Zusatz)

1. Setzen Sie die Priorität einmal auf **99** und messen Sie. Beobachten Sie, ob das System noch normal reagiert. *(Vorsicht: bei einer Endlosschleife bräuchten Sie einen Neustart. Bleiben Sie bei kurzen Läufen.)*
2. Lassen Sie Ihr Programm auf einem **nicht isolierten** Kern mit FIFO 80 laufen und vergleichen Sie mit M2. Wie viel kommt von der Priorität, wie viel von der Isolierung?
3. Schauen Sie mit `ps -eLo pid,psr,class,rtprio,comm | grep stage1` nach, ob Ihr Prozess wirklich dort läuft, wo Sie ihn hingeschickt haben. Das ist die Gewohnheit, die Sie behalten sollen: **nachsehen, nicht annehmen.**
