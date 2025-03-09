# Compiler and flags
CC = gcc
CFLAGS = -Wall -g

# Targets
LIBRARY = libksocket.a
INIT_EXEC = initksocket
USER1_EXEC = user1
USER2_EXEC = user2

# Source files
LIB_SRC = ksocket.c
LIB_HEADER = ksocket.h
INIT_SRC = initksocket.c
USER1_SRC = user1.c
USER2_SRC = user2.c

# Object files for library
LIB_OBJ = ksocket.o

# Build all targets
all: $(LIBRARY) $(USER1_EXEC) $(USER2_EXEC)

# Build the static library
$(LIBRARY): $(LIB_OBJ)
	ar rcs $@ $^

# Compile user1 executable
$(USER1_EXEC): $(USER1_SRC) $(LIBRARY)
	$(CC) $(CFLAGS) -o $@ $^ -L. -lksocket

# Compile user2 executable
$(USER2_EXEC): $(USER2_SRC) $(LIBRARY)
	$(CC) $(CFLAGS) -o $@ $^ -L. -lksocket

# Compile object files for library
%.o: %.c $(LIB_HEADER)
	$(CC) $(CFLAGS) -c $<

# Clean up build files
clean:
	rm -f *.o $(LIBRARY) $(USER1_EXEC) $(USER2_EXEC)
