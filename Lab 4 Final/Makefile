CC = gcc
CFLAGS = -Wall -g -pthread

all: initksocket user1 user2

# First build the shared library
libksocket.a: ksocket.o
	ar rcs libksocket.a ksocket.o

# Compile individual object files
ksocket.o: ksocket.c ksocket.h
	$(CC) $(CFLAGS) -c ksocket.c -o ksocket.o

initksocket.o: initksocket.c ksocket.h
	$(CC) $(CFLAGS) -c initksocket.c -o initksocket.o

user1.o: user1.c ksocket.h
	$(CC) $(CFLAGS) -c user1.c -o user1.o

user2.o: user2.c ksocket.h
	$(CC) $(CFLAGS) -c user2.c -o user2.o

# Compile executables - note that we don't link initksocket.o with user executables
initksocket: initksocket.o libksocket.a
	$(CC) $(CFLAGS) -o initksocket initksocket.o libksocket.a -pthread

user1: user1.o libksocket.a
	$(CC) $(CFLAGS) -o user1 user1.o libksocket.a -pthread

user2: user2.o libksocket.a
	$(CC) $(CFLAGS) -o user2 user2.o libksocket.a -pthread
run:
	./initksocket &
	./user1 &
	./user2

clean:
	rm -f *.o libksocket.a initksocket user1 user2 output.txt

.PHONY: all clean
