CC = gcc #imposta il compilatore 
CFLAGS = -Wall -Wextra -I/usr/include #abilita tutti gli avvisi e include le librerie necessarie
OBJ = server.o database.o #file oggetto che contiene il codice compilato

all: sharecare
#aggiunge le librerie necessarie per SQLite3 e pthread
sharecare: $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ `pkg-config --libs sqlite3` -lpthread 

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

#pulisce
clean:
	rm -f *.o sharecare
