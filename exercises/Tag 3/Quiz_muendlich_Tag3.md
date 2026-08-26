# Abschluss-Quiz — muendlich, 15:15–15:40
### Ueber alle drei Tage · mit Loesungen fuer dich

> **Format:** Du stellst die Frage, laesst 10–20 Sekunden Bedenkzeit, dann
> gemeinsam die Antwort erarbeiten — nicht abfragen im Pruefungsstil,
> sondern ein letztes gemeinsames Durcharbeiten. Bei falscher/unvollstaendiger
> Antwort nicht korrigieren, sondern durch Rueckfrage selbst finden lassen.

---

**1. Was unterscheidet harte von weicher Echtzeit — an einem Beispiel aus
Ihrem eigenen Umfeld?**
*Erwartete Antwort:* Harte Echtzeit: Verpasste Deadline = Systemversagen
(Motorsteuerung). Weiche Echtzeit: Verpasste Deadline = Qualitaetsverlust,
kein Totalausfall (Pharma-Kuehlketten-Monitoring, ms-Jitter unkritisch).

**2. Warum reicht `isolcpus` allein nicht, um Determinismus zu garantieren?**
*Erwartete Antwort:* Kernel-Housekeeping (RCU, Timer-Interrupts) laeuft
weiterhin auf jedem Kern. Isolierung haelt nur Userspace-Last fern — braucht
zusaetzlich IRQ-Affinitaet und Prioritaeten.

**3. Was war das Problem beim Mars Pathfinder, und wie wurde es geloest?**
*Erwartete Antwort:* Priority Inversion — ein niedrigpriorisierter Thread
hielt einen Mutex, ein mittelpriorisierter Thread verdraengte ihn, der
hochpriorisierte Thread wartete unbegrenzt. Geloest durch Priority
Inheritance (`PTHREAD_PRIO_INHERIT`).

**4. Sie sehen im Latenz-Histogramm zwei getrennte Buckel statt einem. Was
vermuten Sie als Erstes?**
*Erwartete Antwort:* Frequency Scaling / falscher Governor — zwei
Taktfrequenzen erzeugen zwei charakteristische Latenzwerte.

**5. Warum darf ein Echtzeit-Thread keinen `send()`-Aufruf auf einen Socket
machen?**
*Erwartete Antwort:* Kann auf vollem Sendepuffer / Netzwerklatenz
blockieren — unkontrollierte Wartezeit. Loesung: Entkopplung ueber
Ringpuffer und separaten Sender-Thread.

**6. Was ist der Unterschied zwischen dem, was `cyclictest` misst, und dem,
was `perf` misst?**
*Erwartete Antwort:* cyclictest misst Weckverzug (wann wacht ein Thread
nach `clock_nanosleep` tatsaechlich auf). perf misst, wo die CPU ihre
Taktzyklen verbringt (Sampling-Profiler).

**7. Ein Freund sagt: „Mein cyclictest-Durchschnitt ist super niedrig,
also ist mein System echtzeitfaehig." Was antworten Sie?**
*Erwartete Antwort:* Der Durchschnitt sagt wenig — entscheidend ist das
Maximum bzw. hohe Perzentile (p99.9). Ein guter Mittelwert bei schlechtem
Maximum ist ein System, das meistens funktioniert und gelegentlich versagt.

**8. Was ist der Zweck von `mlockall()`, und warum reicht der Aufruf allein
nicht?**
*Erwartete Antwort:* Verhindert Auslagerung/Demand-Paging. Reicht allein
nicht, weil noch nicht beruehrter Speicher weiterhin einen Page-Fault
ausloest — deshalb zusaetzlich Speicher „vorwaermen" (einmal beruehren).

---

## Abschlusssatz nach dem Quiz

> „Sie haben heute in acht Fragen im Grunde die ganze Reise der drei Tage
> nochmal durchlaufen — von der Definition ueber die Konfiguration bis zur
> Diagnose. Das war der Punkt der drei Tage: nicht Befehle auswendig
> lernen, sondern eine Denkweise, mit der Sie an JEDEM System — nicht nur
> diesem Pi — die richtige Frage zuerst stellen."
