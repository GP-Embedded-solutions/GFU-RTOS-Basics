# Musterlösung + Trainerhandreichung · Lernprojekt Stufe 2
### Nur für den Trainer. Nicht an den Teilnehmer geben.

---

## 1. Was fachlich herauskommen soll

| Maßnahme | Erwartete Wirkung auf den max. Jitter | Warum |
|---|---|---|
| **SCHED_FIFO 80** | **groß** — meist der stärkste Einzeleffekt | Ohne Echtzeit-Policy konkurriert das Programm unter Last mit allem anderen. Der Sprung ist hier am deutlichsten sichtbar. |
| **Kern festlegen (isoliert)** | mittel bis groß, **abhängig vom Board** | Wirkt nur, wenn die Isolierung sauber ist (nohz_full, rcu_nocbs). Ohne die beiden Zusatzparameter oft überraschend klein. |
| **mlockall** | **klein bis kaum messbar** in dieser Übung | Das Programm ist kompakt, die Seiten sind nach kurzer Laufzeit ohnehin eingelagert. mlock zeigt seine Wirkung bei großem Speicherbedarf oder Speicherdruck. |

**Das ist die Pointe der Übung:** `mlockall` ist mit hoher Wahrscheinlichkeit die Maßnahme, die „kaum etwas bringt". Und die richtige Schlussfolgerung ist **nicht** „mlock ist unnötig", sondern: *„Auf diesem Board, mit diesem Programm, unter dieser Last bringt es wenig. Unter Speicherdruck sieht das anders aus."*

Wenn er zu „mlock ist Quatsch" springt, korrigierst du genau dort — das ist der wertvollste Korrekturmoment der Stunde.

> **Vorbehalt, ehrlich:** Diese Erwartungen stammen aus der Systematik, nicht aus einer Messung auf **deinem** Pi. Trage nach dem Trockenlauf am Donnerstag deine tatsächliche Rangfolge hier ein. Wenn sie abweicht, gewinnt deine Messung — und du sagst das im Kurs auch so.

---

## 2. Definition of Done — Stufe 2

Abgenommen ist die Stufe, wenn **alle sechs** Punkte erfüllt sind:

1. Das Programm läuft unter SCHED_FIFO mit einer **begründeten** Priorität (nicht 99).
2. Es läuft nachweislich auf dem vorgesehenen Kern — nachgewiesen mit `ps -eLo psr`, nicht behauptet.
3. Es gibt mindestens vier Messungen, jede ≥ 60 s, jede unter Last.
4. Pro Messschritt wurde **genau eine** Sache geändert.
5. Verglichen werden **Maxima**.
6. Er kann benennen, welche Maßnahme am meisten und welche am wenigsten gebracht hat — mit einer Vermutung zum Warum.

**Nicht Teil der Abnahme:** ein bestimmter Zahlenwert. Es geht um die Methode, nicht um ein Latenzziel.

---

## 3. Beobachtungspunkte (notieren, nicht sofort eingreifen)

| Beobachtung | Was sie bedeutet | Wann du eingreifst |
|---|---|---|
| Ändert mehrere Dinge gleichzeitig | Er will das Ergebnis, nicht die Erkenntnis | Sofort, aber als Frage: *„Welche der drei Änderungen hat das bewirkt?"* |
| Vergleicht Mittelwerte | Der Kernpunkt von 09:20 ist nicht angekommen | Sofort — das ist der wichtigste Lerninhalt des Tages |
| Misst ohne Last | Der Fehler vom Vormittag, jetzt im eigenen Code | Sofort, mit Verweis auf Block 3 |
| Setzt Priorität 99 | Häufiger Reflex | Nicht sofort. Lass ihn messen, frag danach: *„Wer läuft sonst noch auf 99?"* |
| Prüft nicht nach, ob der Prozess wirklich auf Kern 3 läuft | Vertraut der Konfiguration | Nach 10 Minuten fragen: *„Woher wissen Sie, dass er dort läuft?"* |
| Kommt schnell durch | Stufe-1-Tempo war unterschätzt | Zusatzaufgaben aus dem Aufgabenblatt geben, **nicht** Tag-3-Stoff vorziehen |
| Hängt bei Werkzeugfehlern | Nicht sein Lernziel | Nach 5 Minuten selbst lösen und weitermachen |

---

## 4. Wenn es klemmt — Sofortlösungen

| Symptom | Ursache | Fix |
|---|---|---|
| `--fifo` scheitert mit „Operation not permitted" | kein sudo bzw. RLIMIT_RTPRIO | `sudo` davor, oder `ulimit -r 99` |
| `--cpu 3` läuft, aber Werte unverändert | Isolierung nicht aktiv | `cat /sys/devices/system/cpu/isolated`, sonst auf Kern 2 ausweichen und offen benennen |
| Jitter schwankt zwischen identischen Läufen stark | Last nicht reproduzierbar / Messdauer zu kurz | Dauer auf 90 s, `stress-ng` mit identischen Parametern |
| Sensor liefert plötzlich Fehler | I²C-Timeout unter FIFO-Last | Priorität auf 50 senken, im Kurs als Lehrstück nutzen: **zu hohe Priorität kann Treiber aushungern** |
| CSV bleibt leer | Programm zu früh beendet | Läufe mit `timeout 70` statt manuellem Abbruch |

> Der vierte Fall ist ein Geschenk, falls er eintritt. Ein hochpriorer Thread, der den Kernel-Thread des I²C-Treibers verdrängt, ist Prioritätsinversion in freier Wildbahn — direkt nach der Mars-Story. Nimm ihn dann bewusst mit.

---

## 5. Übergabe an Tag 3

Notiere für morgen:
- Welche Maßnahme bei ihm am meisten brachte → damit beginnt der Recap um 09:00.
- Welche Frage er nicht beantworten konnte → das ist der Einstieg in Modul 8 (Debugging): *„Gestern konnten Sie nicht sagen, warum. Heute lernen Sie die Werkzeuge, die es Ihnen zeigen."*
- Ob seine CSV-Daten brauchbar sind → sie sind das Ausgangsmaterial für den InfluxDB/Grafana-Bonus, falls Tag 3 Zeit lässt.
