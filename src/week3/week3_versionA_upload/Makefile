CC ?= gcc
CFLAGS ?= -O2 -std=gnu11 -Wall -Wextra -Wshadow -fno-pie
LDFLAGS ?= -no-pie

COMMON = aes.o

.PHONY: all clean

all: victim attacker

victim: victim.o $(COMMON)
	$(CC) $(CFLAGS) $(LDFLAGS) -pthread -o $@ $^

attacker: attacker.o
	$(CC) $(CFLAGS) $(LDFLAGS) -pthread -o $@ $^

aes.o: aes.c aes.h
	$(CC) $(CFLAGS) -c -o $@ $<

victim.o: victim.c aes.h shared.h timing.h
	$(CC) $(CFLAGS) -c -o $@ $<

attacker.o: attacker.c shared.h timing.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f victim attacker *.o *.csv ttable_shared.bin
