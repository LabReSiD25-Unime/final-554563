
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
        "CREATE TABLE IF NOT EXISTS Ente_Benefico ("  //enti benefici
        "ID_Ente INTEGER PRIMARY KEY AUTOINCREMENT," 
        "Nome TEXT NOT NULL," 
        "Descrizione TEXT," 
        "Sede TEXT);" 

        

        "CREATE TABLE IF NOT EXISTS Articolo_Solidale ("  //articoli con foto
        "ID_Articolo INTEGER PRIMARY KEY AUTOINCREMENT," 
        "Nome TEXT NOT NULL," 
        "Descrizione TEXT," 
        "Prezzo REAL," 
        "Quantita INTEGER," 
        "Foto TEXT," 
        "ID_Ente INTEGER," 
        "FOREIGN KEY(ID_Ente) REFERENCES Ente_Benefico(ID_Ente));"

        
        "CREATE TABLE IF NOT EXISTS PostBlog ("
        "  ID_Post   INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  Nome      TEXT    NOT NULL,"
        "  Messaggio TEXT    NOT NULL,"
        "  Data      TEXT DEFAULT CURRENT_TIMESTAMP"
        ");";

    char *err = NULL; //prepara un puntatore dove SQLite potrà copiare un messaggio di errore se qualcosa va storto
    int rc = sqlite3_exec(db, sql, 0, 0, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQLite error: %s\n", err);
        sqlite3_free(err);
        return 1;
    }
    return 0;
}


//JSON UTILS
static char *json_array(sqlite3 *db, const char *sql) { //costruire e restituire una stringa JSON contenente i risultati della query
    sqlite3_stmt *stmt; //riceve un puntatore al db e alla query 
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return strdup("[]"); //prepara la query SQL (sql) per l'esecuzione.

//Se fallisce, restituisce un array JSON vuoto.

    size_t size = 8192; //alloca memoria x un buffer json 
    char *json = malloc(size);
    if (!json) return strdup("[]");

    strcpy(json, "[");
    int first = 1; //per sapere quando aggiungere la virgola tra gli oggetti JSON

    while (sqlite3_step(stmt) == SQLITE_ROW) { //scorre i risultati della query
        const unsigned char *item = sqlite3_column_text(stmt, 0); //estrae il valore della prima colonna della riga corrente.
        //ci si aspetta che la colonna contenga una stringa JSON parziale già formattata da SQLite
        if (!item) continue;

        if (!first) strcat(json, ","); //Aggiunge la virgola tra gli oggetti JSON, ma non prima del primo oggetto.
        else
        first = 0;

        if (strlen(json) + strlen((const char *)item) + 2 > size) { //verifica se lo spazio nel buffer json è sufficiente a contenere il nuovo oggetto JSON più , e ]
            size *= 2;
            char *new_json = realloc(json, size); //se non lo è, raddoppia la dimensione del buffer con realloc.


            if (!new_json) {
                sqlite3_finalize(stmt);
                free(json);                //se realloc fallisce, libera il buffer json e restituisce un array JSON vuoto
                return strdup("[]"); 
            }
            json = new_json;
        }

        strcat(json, (const char *)item);//aggiunge l'oggetto JSON parziale al buffer json
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

    sqlite3_stmt *st; //Dichiara un puntatore a uno "statement" preparato
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return strdup("[]");//restituisce json vuoto se la preparazione fallisce

    size_t cap = 8192; //size_t è un tipo senza segno adatto per rappresentare dimensioni/memorie.
    char *json = malloc(cap);    //Alloca un buffer dinamico in memoria heap, grande 8192 byte
    if (!json) { sqlite3_finalize(st); return strdup("[]"); } //se non cìè abbastanza memoria restituisce un array JSON vuoto

    strcpy(json, "["); //costruisce manualmente una stringa JSON che rappresenta un array di oggetti, ognuno dei quali è un articolo solidale con attributi 
    int first = 1;

    while (sqlite3_step(st) == SQLITE_ROW) { //Ogni riga rappresenta un articolo
        if (!first) strcat(json, ",");
        first = 0;
//estrae le colonne una per una.

//ogni sqlite3_column_* recupera il campo giusto 

        int id          = sqlite3_column_int   (st, 0);
        const char *nome= (const char *)sqlite3_column_text(st, 1);
        const char *desc= (const char *)sqlite3_column_text(st, 2);
        double prezzo   = sqlite3_column_double(st, 3);
        int quantita    = sqlite3_column_int   (st, 4);
        const char *foto= (const char *)sqlite3_column_text(st, 5);

        char rec[1024]; //crea una stringa che rappresenta l'articolo in json 
        snprintf(rec, sizeof(rec),
            "{\"ID_Articolo\":%d,\"Nome\":\"%s\",\"Descrizione\":\"%s\"," \
            "\"Prezzo\":%.2f,\"Quantita\":%d,\"Foto\":\"%s\"}",
            id, nome, desc, prezzo, quantita, foto);

        //rialloca se serve
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
//gli indici indicano quale parametro viene estratto 


//insert utils
int insert_ente(sqlite3 *db, const char *nome, const char *descr, const char *sede) {
    const char *sql = "INSERT INTO Ente_Benefico (Nome,Descrizione,Sede) VALUES (?,?,?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 1;
    sqlite3_bind_text(stmt, 1, nome, -1, SQLITE_TRANSIENT); //transient x maggiore sicurezza 
    sqlite3_bind_text(stmt, 2, descr, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sede, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc != SQLITE_DONE;
}

int insert_donazione(sqlite3 *db, int id_utente, int id_ente, double importo) {    const char *sql = "INSERT INTO Donazione (ID_Utente, ID_Ente, Importo) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt; 
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 1; 
    sqlite3_bind_int(stmt, 1, id_utente); //rimpiazza  ? con i parametri 
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
    int rc = sqlite3_step(st);  //return code 
    sqlite3_finalize(st);
    return rc != SQLITE_DONE;      //Restituisci 1 se rc è diverso da SQLITE_DONE; altrimenti restituisci 0 
}
