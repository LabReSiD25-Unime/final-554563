#ifndef SHARECARE_DATABASE_H
#define SHARECARE_DATABASE_H

#include <sqlite3.h>

int open_db(sqlite3 **db); //serve per far sì che la funzione possa restituire un database aperto modificando il puntatore passato
void close_db(sqlite3 *db);
int init_db(sqlite3 *db);

//serializza in JSON
char *get_enti_json(sqlite3 *db);
char *get_articoli_json(sqlite3 *db);   
char *get_blog_json(sqlite3 *db);

//inserimenti
int insert_ente(sqlite3 *db, const char *nome, const char *descr, const char *sede); 
int insert_donazione(sqlite3 *db, int id_utente, int id_ente, double importo);         
int   insert_post(sqlite3 *db, const char *nome, const char *msg);

#endif 
