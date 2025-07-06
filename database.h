#ifndef SHARECARE_DATABASE_H
#define SHARECARE_DATABASE_H

#include <sqlite3.h>

int open_db(sqlite3 **db);
void close_db(sqlite3 *db);
int init_db(sqlite3 *db);

/* Serializza in JSON (malloc, free a carico del chiamante) */
char *get_enti_json(sqlite3 *db);
char *get_articoli_json(sqlite3 *db);   // <--- QUESTA RIGA SERVE

/* Blog */
char *get_blog_json(sqlite3 *db);

/* Inserimenti */
int insert_ente(sqlite3 *db, const char *nome, const char *descr, const char *sede);   // <--- SERVE
int insert_donazione(sqlite3 *db, int id_utente, int id_ente, double importo);         // <--- SERVE
int   insert_post(sqlite3 *db, const char *nome, const char *msg);

#endif // SHARECARE_DATABASE_H