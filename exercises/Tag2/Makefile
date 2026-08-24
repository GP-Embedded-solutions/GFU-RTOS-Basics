# Makefile · Tag 2 · Echtzeit-Linux
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
LDLIBS  := -pthread -lrt

BINS := priority_inversion_demo

all: $(BINS)

priority_inversion_demo: priority_inversion_demo.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# Der Doppelschlag fuer den Kurs: kaputt, dann geheilt.
demo: priority_inversion_demo
	@echo "### 1) OHNE Priority Inheritance ###"
	sudo ./priority_inversion_demo
	@echo
	@echo "### 2) MIT Priority Inheritance ###"
	sudo ./priority_inversion_demo --inherit

# Selbsttest: prueft, dass der Kontrast tatsaechlich messbar ist.
test: priority_inversion_demo
	@sudo ./selbsttest_pi_demo.sh

clean:
	rm -f $(BINS) *.o

.PHONY: all demo test clean
