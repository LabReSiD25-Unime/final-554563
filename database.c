#ifndef SHARECARE_DATABASE_H
#define SHARECARE_DATABASE_H

#include <sqlite3.h>

int open_db(sqlite3 **db);
void close_db(sqlite3 *db);
int init_db(sqlite3 *db);

/* Serializza in JSON (malloc, free a carico del chiamante) */
char *get_enti_json(sqlite3 *db);
char *get_articoli_json(sqlite3 *db);
char *get_blog_json(sqlite3 *db);

/* Inserimenti */
int insert_ente(sqlite3 *db, const char *nome, const char *descr, const char *sede);
int insert_donazione(sqlite3 *db, int id_utente, int id_ente, double importo);
int insert_post(sqlite3 *db, const char *nome, const char *msg);

#endif // SHARECARE_DATABASE_H

// ============================
// File: database.c
// ============================
#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int open_db(sqlite3 **db) {
    return sqlite3_open("sharecare.db", db);
}

void close_db(sqlite3 *db) {
    sqlite3_close(db);
}

int init_db(sqlite3 *db) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS Ente_Benefico ("  // Enti benefici
        "ID_Ente INTEGER PRIMARY KEY AUTOINCREMENT," 
        "Nome TEXT NOT NULL," 
        "Descrizione TEXT," 
        "Sede TEXT);" 

        "CREATE TABLE IF NOT EXISTS Utente ("  // utenti per donazioni
        "ID_Utente INTEGER PRIMARY KEY AUTOINCREMENT," 
        "Nome TEXT, Cognome TEXT, Email TEXT UNIQUE);" 

        "CREATE TABLE IF NOT EXISTS Donazione ("  // donazioni
        "ID_Donazione INTEGER PRIMARY KEY AUTOINCREMENT," 
        "ID_Utente INTEGER," 
        "ID_Ente INTEGER," 
        "Importo REAL," 
        "Data TEXT DEFAULT CURRENT_TIMESTAMP," 
        "FOREIGN KEY(ID_Utente) REFERENCES Utente(ID_Utente)," 
        "FOREIGN KEY(ID_Ente) REFERENCES Ente_Benefico(ID_Ente));" 

        "CREATE TABLE IF NOT EXISTS Articolo_Solidale ("  // articoli con foto
        "ID_Articolo INTEGER PRIMARY KEY AUTOINCREMENT," 
        "Nome TEXT NOT NULL," 
        "Descrizione TEXT," 
        "Prezzo REAL," 
        "Quantita INTEGER," 
        "Foto TEXT," 
        "ID_Ente INTEGER," 
        "FOREIGN KEY(ID_Ente) REFERENCES Ente_Benefico(ID_Ente));"

        // Nuova tabella Transazione
        "CREATE TABLE IF NOT EXISTS Transazione ("
        "ID_Tx     INTEGER PRIMARY KEY AUTOINCREMENT,"
        "Tipo      TEXT,"
        "ID_Ente   INTEGER,"
        "ID_Articolo INTEGER,"
        "Email     TEXT,"
        "Importo   REAL,"
        "Indirizzo TEXT,"
        "Data      TEXT DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS PostBlog ("
        "  ID_Post   INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  Nome      TEXT    NOT NULL,"
        "  Messaggio TEXT    NOT NULL,"
        "  Data      TEXT DEFAULT CURRENT_TIMESTAMP"
        ");";

    char *err = NULL;
    int rc = sqlite3_exec(db, sql, 0, 0, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQLite error: %s\n", err);
        sqlite3_free(err);
        return 1;
    }
    return 0;
}

// ---------------- JSON UTILS ----------------
static char *json_array(sqlite3 *db, const char *sql) {
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return strdup("[]");

    size_t size = 8192;
    char *json = malloc(size);
    if (!json) return strdup("[]");

    strcpy(json, "[");
    int first = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *item = sqlite3_column_text(stmt, 0);
        if (!item) continue;

        if (!first) strcat(json, ",");
        first = 0;

        if (strlen(json) + strlen((const char *)item) + 2 > size) {
            size *= 2;
            char *new_json = realloc(json, size);
            if (!new_json) {
                sqlite3_finalize(stmt);
                free(json);
                return strdup("[]");
            }
            json = new_json;
        }

        strcat(json, (const char *)item);
    }

    strcat(json, "]");
    sqlite3_finalize(stmt);
    return json;
}

char *get_enti_json(sqlite3 *db) {
    const char *sql =
        "SELECT json_object("
        "'ID_Ente', ID_Ente, "
        "'Nome', Nome, "
        "'Descrizione', COALESCE(Descrizione, ''), "
        "'Sede', COALESCE(Sede, ''), "
        "'Logo', COALESCE(Logo, '')"
        ") FROM Ente_Benefico;";
    return json_array(db, sql);
}

char *get_articoli_json(sqlite3 *db) {
    const char *sql =
        "SELECT ID_Articolo, Nome, COALESCE(Descrizione,''), "
        "Prezzo, Quantita, COALESCE(Foto,'') "
        "FROM Articolo_Solidale;";

    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return strdup("[]");

    size_t cap = 8192;
    char *json = malloc(cap);          /* buffer dinamico */
    if (!json) { sqlite3_finalize(st); return strdup("[]"); }

    strcpy(json, "[");
    int first = 1;

    while (sqlite3_step(st) == SQLITE_ROW) {
        if (!first) strcat(json, ",");
        first = 0;

        int id          = sqlite3_column_int   (st, 0);
        const char *nome= (const char *)sqlite3_column_text(st, 1);
        const char *desc= (const char *)sqlite3_column_text(st, 2);
        double prezzo   = sqlite3_column_double(st, 3);
        int quantita    = sqlite3_column_int   (st, 4);
        const char *foto= (const char *)sqlite3_column_text(st, 5);

        char rec[1024];
        snprintf(rec, sizeof(rec),
            "{\"ID_Articolo\":%d,\"Nome\":\"%s\",\"Descrizione\":\"%s\"," \
            "\"Prezzo\":%.2f,\"Quantita\":%d,\"Foto\":\"%s\"}",
            id, nome, desc, prezzo, quantita, foto);

        /* rialloca se serve */
        if (strlen(json) + strlen(rec) + 2 > cap) {
            cap *= 2;
            json = realloc(json, cap);
            if (!json) { sqlite3_finalize(st); return strdup("[]"); }
        }
        strcat(json, rec);
    }
    strcat(json, "]");
    sqlite3_finalize(st);
    return json;
}

char *get_blog_json(sqlite3 *db) {
    const char *sql =
        "SELECT json_object("
        "'ID_Post', ID_Post, "
        "'Nome', Nome, "
        "'Messaggio', COALESCE(Messaggio, ''), "
        "'Data', COALESCE(Data, '')"
        ") FROM PostBlog;";
    return json_array(db, sql);
}

// ---------------- INSERT UTILS ----------------
int insert_ente(sqlite3 *db, const char *nome, const char *descr, const char *sede) {
    const char *sql = "INSERT INTO Ente_Benefico (Nome,Descrizione,Sede) VALUES (?,?,?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 1;
    sqlite3_bind_text(stmt, 1, nome, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, descr, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sede, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc != SQLITE_DONE;
}

int insert_donazione(sqlite3 *db, int id_utente, int id_ente, double importo) {    const char *sql = "INSERT INTO Donazione (ID_Utente, ID_Ente, Importo) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 1;
    sqlite3_bind_int(stmt, 1, id_utente);
    sqlite3_bind_int(stmt, 2, id_ente);
    sqlite3_bind_double(stmt, 3, importo);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc != SQLITE_DONE;
}

int insert_post(sqlite3 *db, const char *nome, const char *msg) {
    const char *sql = "INSERT INTO PostBlog (Nome, Messaggio) VALUES (?,?);";
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 1;
    sqlite3_bind_text(st, 1, nome, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, msg,  -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc != SQLITE_DONE;      // 0 = OK
}
