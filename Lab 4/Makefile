CC = gcc
CFLAGS = -Wall -fPIC
LDFLAGS = -pthread

all: libksocket.a initksocket user1 user2

libksocket.a: ksocket.o
	ar rcs $@ $^

ksocket.o: ksocket.c ksocket.h
	$(CC) $(CFLAGS) -c $<

initksocket.o: initksocket.c ksocket.h
	$(CC) $(CFLAGS) -c $<

initksocket: initksocket.o libksocket.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -L. -lksocket

user1: user1.c libksocket.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -L. -lksocket

user2: user2.c libksocket.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -L. -lksocket

clean:
	rm -f *.o *.a initksocket user1 user2
