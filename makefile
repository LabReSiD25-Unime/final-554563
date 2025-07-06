CC = gcc
CFLAGS = -Wall -Wextra -I/usr/include
LDFLAGS =
OBJ = server.o database.o

all: sharecare

sharecare: $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ `pkg-config --libs sqlite3` -lpthread

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o sharecare
