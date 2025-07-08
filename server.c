#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include "database.h"

#define PORTA 8080
#define BUF_SIZE 8192

atomic_int contatore_client = 0; //variabile che può essere letta e scritta in modo sicuro anche quando più thread accedono ad essa contemporaneamente

struct ThreadArgs { //la funzione pthread_create accetta un solo parametro void *arg, quindi per passare più dati (o anche solo uno con tipo definito), si mettono in una struct e poi si passa un puntatore a quella struct
    int sock;
};

static void gestisci_client(int client_sock, sqlite3 *db); //static xk è privata x il file c 
static void invia_risposta(int client_sock, const char *status, const char *content_type, const char *body);
static char *estrai_body(char *request);
static void route_get_enti(int sock, sqlite3 *db);
static void route_post_enti(int sock, sqlite3 *db, const char *body);  //body è il json 
static void route_get_articoli(int sock, sqlite3 *db);
static void route_post_donazioni(int sock, sqlite3 *db, const char *body);
static void route_post_checkout(int sock, sqlite3 *db, const char *body);
static void route_get_blog(int sock, sqlite3 *db);
static void route_post_blog(int sock, sqlite3 *db, const char *body);
void *thread_client(void *arg);


//questa funzione è una misura di sicurezza
static void serve_static_file(int sock, const char *path) { //path è url richiesto dal browser. 
    //costruisce percorso 
    char file[512];
    if (strcmp(path, "/") == 0)               //Se il path richiesto è esattamente / (cioè l’utente ha visitato localhost:8080/)
//allora carichiamo "index.html".
        strcpy(file, "index.html");          //root ->index
    else //Se invece il path è un altro (es: /stile.css, /foto.png)
//allora costruisce il nome del file aggiungendo un punto davanti:
        snprintf(file, sizeof(file), ".%s", path); // ./paypal.html, ./stile.css, ...

    FILE *fp = fopen(file, "rb");
    if (!fp) {                              //file non trovato
        invia_risposta(sock, "404 Not Found", "text/plain", "File non trovato");
        return;
    }

    //determina di che tipo è il file
    const char *ctype = "text/plain";
    if (strstr(file, ".html")) ctype = "text/html";  
    else if (strstr(file, ".css")) ctype = "text/css";
    else if (strstr(file, ".js"))  ctype = "application/javascript";
    else if (strstr(file, ".png")) ctype = "image/png";
    else if (strstr(file, ".jpg") || strstr(file, ".jpeg")) ctype = "image/jpeg";

    fseek(fp, 0, SEEK_END); //misura quanto è lungo il file 
    long len = ftell(fp);
    rewind(fp);

    //invia header
    char header[256];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
        ctype, len);
    send(sock, header, strlen(header), 0);

    //invia corpo (binary-safe)
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        send(sock, buf, n, 0);

    fclose(fp);
}

int main(void) {
    int server_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    //inizializza il DB una volta sola per creare le tabelle
    sqlite3 *db;
    if (open_db(&db) || init_db(db)) {
        fprintf(stderr, "[ERRORE] Inizializzazione DB fallita\n");
        return EXIT_FAILURE;
    }
    close_db(db);

    //socket
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORTA);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_sock);
        return EXIT_FAILURE;
    }

    if (listen(server_sock, 50) < 0) {
        perror("listen");
        close(server_sock);
        return EXIT_FAILURE;
    }

    printf("[ShareCare] Server multithread (pthread) su http://localhost:%d\n", PORTA);

    while (1) {
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len); //bloccante
        if (client_sock < 0) {
            perror("accept");
            continue;
        }
        char ip_client[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_client, sizeof(ip_client)); //converte in formato di rete leggibile
        int numero = atomic_fetch_add(&contatore_client, 1) + 1;
        time_t ora = time(NULL);
        char *orario = ctime(&ora);
        orario[strcspn(orario, "\n")] = 0;
        printf("[CONNESSIONE %d] %s da %s:%d\n", numero, orario, ip_client, ntohs(client_addr.sin_port));

        pthread_t tid;
        struct ThreadArgs *args = malloc(sizeof(struct ThreadArgs));
        args->sock = client_sock;

        pthread_create(&tid, NULL, thread_client, args);
        pthread_detach(tid);
    }

    close(server_sock);
    return EXIT_SUCCESS;
}

void *thread_client(void *arg) {
    struct ThreadArgs *args = (struct ThreadArgs *)arg;
    int client_sock = args->sock;
    free(args);

    //ogni thread apre il proprio db!
    sqlite3 *db;
    if (open_db(&db)) {
        fprintf(stderr, "[ERRORE] DB thread non disponibile\n");
        close(client_sock);
        pthread_exit(NULL);
    }

    gestisci_client(client_sock, db);

    close_db(db);
    close(client_sock);
    printf("[FINE CONNESSIONE] Client socket %d\n", client_sock);
    pthread_exit(NULL);
}

//gestione richiesta
static void gestisci_client(int client_sock, sqlite3 *db) {
    char buffer[BUF_SIZE];
    int bytes = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) return;
    buffer[bytes] = '\0';

    //estrai metodo e path
    char metodo[8], path[256];
    sscanf(buffer, "%7s %255s", metodo, path);
    //rimuove eventuale query string ?... da path
    char *q = strchr(path, '?');
    if (q) *q = '\0';

    printf("[DEBUG] Richiesta: %s %s\n", metodo, path);

    //FILE STATICI
    if (strcmp(metodo, "GET") == 0 &&
        (strstr(path, ".html") || strstr(path, ".css") || strstr(path, ".js") ||
         strstr(path, ".png")  || strstr(path, ".jpg") || strcmp(path, "/") == 0)) {
        serve_static_file(client_sock, path);
        return;       
    }

    char *body = estrai_body(buffer);

    //ROUTING
    if (strcmp(metodo, "GET") == 0 && strcmp(path, "/enti") == 0) {
        route_get_enti(client_sock, db);

    } else if (strcmp(metodo, "POST") == 0 && strcmp(path, "/enti") == 0) {
        route_post_enti(client_sock, db, body);

    } else if (strcmp(metodo, "GET") == 0 && strcmp(path, "/articoli") == 0) {
        route_get_articoli(client_sock, db);

    } else if (strcmp(metodo, "POST") == 0 && strcmp(path, "/donazioni") == 0) {
        route_post_donazioni(client_sock, db, body);

    } else if (strcmp(metodo, "POST") == 0 && strcmp(path, "/checkout") == 0) {
        route_post_checkout(client_sock, db, body);

    } else if (strcmp(metodo,"GET")==0 && strcmp(path,"/blog")==0) {
        route_get_blog(client_sock, db);
    } else if (strcmp(metodo,"POST")==0 && strcmp(path,"/blog")==0) {
        route_post_blog(client_sock, db, body);

    } else {
        invia_risposta(client_sock, "404 Not Found", "text/plain", "Risorsa non trovata");
    }
}

//Utils
static char *estrai_body(char *request) { //prende in ingresso una richiesta HTTP e restituisce il corpo della richiesta
    char *body = strstr(request, "\r\n\r\n"); //cerca la prima occorrenza di "\r\n\r\n" che segna la fine dell'header HTTP e l'inizio del corpo
    return body ? body + 4 : NULL; //ritorna un puntatore al primo carattere dopo la sequenza cioè l'inizio del body.
}

//serve per inviare una risposta HTTP completa (header + corpo) a un client connesso
static void invia_risposta(int client_sock, const char *status, const char *content_type, const char *body) {
    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        status, content_type, body ? strlen(body) : 0); //se body non è NULL, allora calcola strlen(body), altrimenti restituisci 0
    send(client_sock, header, strlen(header), 0); //invia i dati dell'header HTTP al client
    if (body) send(client_sock, body, strlen(body), 0); //invia il corpo della risposta HTTP al client, solo se è presente body
    printf("[DEBUG] Risposta inviata: %s\n", status);
}

//ROUTE HANDLERS
//Chiama la funzione get_enti_json, che esegue una query SQL su db e costruisce una stringa JSON con i dati degli enti
static void route_get_enti(int sock, sqlite3 *db) {
    char *json = get_enti_json(db);
    invia_risposta(sock, "200 OK", "application/json", json);
    free(json);
}

//gestisce una richiesta HTTP POST alla route /enti del server REST, che serve per inserire un nuovo ente benefico nel database.

static void route_post_enti(int sock, sqlite3 *db, const char *body) {
    if (!body) {
        invia_risposta(sock, "400 Bad Request", "text/plain", "Body mancante"); //se il corpo della richiesta è assente (cioè NULL), restituisce al client un errore 400, perché non si possono inserire dati senza informazioni.
        return;
    }
    char nome[128] = "", descr[256] = "", sede[128] = ""; 
    //usa sscanf per estrarre i valori direttamente dalla stringa body in formato JSON.
    // parsing naive: cerca "Nome":"..."
    sscanf(body, "{\"Nome\":\"%127[^\"]\",\"Descrizione\":\"%255[^\"]\",\"Sede\":\"%127[^\"]", nome, descr, sede);

    if (strlen(nome) == 0) {
        invia_risposta(sock, "400 Bad Request", "text/plain", "Nome obbligatorio");
        return;
    }
    if (insert_ente(db, nome, descr, sede)) {
        invia_risposta(sock, "500 Internal Server Error", "text/plain", "Errore inserimento ente");
    } else {
        invia_risposta(sock, "201 Created", "application/json", "{\"msg\":\"Ente creato\"}");
    }
}

static void route_get_articoli(int sock, sqlite3 *db) {
    char *json = get_articoli_json(db);
    invia_risposta(sock, "200 OK", "application/json", json);
    free(json);
}

static void route_post_donazioni(int sock, sqlite3 *db, const char *body) {
    if (!body) {
        invia_risposta(sock, "400 Bad Request", "text/plain", "Body mancante");
        return;
    }
    /*sscanf è usato per leggere dati da una stringa (body), estraendone i valori secondo un formato predefinito.

Il formato passato ("{\"ID_Utente\":%d,\"ID_Ente\":%d,\"Importo\":%lf") dice al programma di cercare tre valori*/
    int id_utente = 0, id_ente = 0;
    double importo = 0;
    sscanf(body, "{\"ID_Utente\":%d,\"ID_Ente\":%d,\"Importo\":%lf", &id_utente, &id_ente, &importo);

    if (id_utente == 0 || id_ente == 0 || importo <= 0) {
        invia_risposta(sock, "400 Bad Request", "text/plain", "Campi obbligatori mancanti");
        return;
    }
    if (insert_donazione(db, id_utente, id_ente, importo)) {
        invia_risposta(sock, "500 Internal Server Error", "text/plain", "Errore donazione");
    } else {
        invia_risposta(sock, "201 Created", "application/json", "{\"msg\":\"Donazione registrata\"}");
    }
}

static void route_post_checkout(int sock, sqlite3 *db, const char *body) {
    /* esempio body JSON:
       {
         "tipo":"donazione"|"acquisto",
         "id_ente":3,
         "id_articolo":5,        // facoltativo
         "email":"utente@example.com",
         "password":"...",       // ignorata, solo per finta UI
         "importo":25.0,
         "indirizzo":"Via Roma 1, 00100 Roma" // solo x acquisto
       }
    */
    char tipo[16] = "";
    int  id_ente = 0, id_art = 0;
    char email[128] = "", indirizzo[256] = "";
    double importo = 0;

    sscanf(body,
      "{\"tipo\":\"%15[^\"]\",\"id_ente\":%d,\"id_articolo\":%d,"
      "\"email\":\"%127[^\"]\",\"importo\":%lf,\"indirizzo\":\"%255[^\"]",
      tipo,&id_ente,&id_art,email,&importo,indirizzo);

    const char *sql =
      "INSERT INTO Transazione (Tipo,ID_Ente,ID_Articolo,Email,Importo,Indirizzo)"
      "VALUES (?,?,?,?,?,?);";
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db,sql,-1,&st,NULL)!=SQLITE_OK){invia_risposta(sock,"500","text/plain","DB error");return;}
    sqlite3_bind_text(st,1,tipo,-1,SQLITE_TRANSIENT);
    sqlite3_bind_int (st,2,id_ente);
    if (strcmp(tipo,"acquisto")==0) sqlite3_bind_int(st,3,id_art); else sqlite3_bind_null(st,3);
    sqlite3_bind_text(st,4,email,-1,SQLITE_TRANSIENT);
    sqlite3_bind_double(st,5,importo);
    if (strcmp(tipo,"acquisto")==0) sqlite3_bind_text(st,6,indirizzo,-1,SQLITE_TRANSIENT); else sqlite3_bind_null(st,6);
    sqlite3_step(st); sqlite3_finalize(st);

    invia_risposta(sock,"200 OK","application/json","{\"msg\":\"ok\"}");
}

static void route_get_blog(int sock, sqlite3 *db) {
    char *json = get_blog_json(db);
    invia_risposta(sock, "200 OK", "application/json", json);
    free(json);
}

static void route_post_blog(int sock, sqlite3 *db, const char *body) {
    char nome[128] = "", msg[1024] = "";
    if (!body ||
        sscanf(body, "{\"Nome\":\"%127[^\"]\",\"Messaggio\":\"%1023[^\"]", nome, msg) != 2) {
        invia_risposta(sock, "400 Bad Request", "text/plain", "JSON non valido");
        return;
    }
    if (insert_post(db, nome, msg))
        invia_risposta(sock, "500 Internal Server Error", "text/plain", "Errore DB");
    else
        invia_risposta(sock, "201 Created", "application/json", "{\"msg\":\"Post salvato\"}");
}

















/*
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠠⠀⠀⠀⠀⠠⣀⣠⣤⣤⣤⣤⣤⣄⡠⠤⠄⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⠤⠄⠒⠒⠒⠂⠂⠀⠀⠀⠀⠀⠀⠀⠀⣠⣾⣿⣿⣿⣿⣿⣿⣿⣿⠃⠀⠀⠈⠳⠤⢀⡀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⣠⠊⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢹⣿⣿⣿⣿⣿⣿⣿⣿⣿⡆⠀⠀⠀⠀⠀⠀⠈⠳⡄⠀⠀⠀⠀
⠀⠀⠀⠀⠀⡠⠒⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣠⣾⣿⣿⣿⣿⣿⣿⣿⣿⠟⠁⠀⠀⠀⠀⠀⠀⠀⠀⠘⣄⠀⠀⠀
⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠘⢿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠑⢄⠀
⠀⠀⠀⢀⢖⣿⡷⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣼⣿⣿⣿⣿⣿⣿⣿⡿⠃⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⠀
⠀⠀⠀⢸⡾⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣴⣿⣿⣿⣿⣿⣿⣿⠟⠋⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢇⠀
⠀⠀⠀⣴⣛⣿⣿⠇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢺⣿⣿⣿⣿⣿⣿⣿⢥⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡸⠀
⠀⠀⣾⣾⣿⣿⣧⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⣀⣤⠽⠿⠿⠿⠿⠿⣧⣄⡈⠛⠛⠛⣛⣛⣳⣶⣶⣦⣄⡀⠀⠀⠀⠀⡎⠀⠀
⠀⠘⡿⣿⣿⣿⣿⣿⣦⣄⠀⠀⣀⣠⠤⠤⣤⣖⡺⢟⣩⠥⢒⡊⠭⠭⠝⢑⣒⠲⠤⢭⢖⣪⠭⠜⠓⢒⣒⠒⠒⢒⣛⢷⣄⠀⢀⡇⠀⠀
⠀⢀⣽⣿⣿⣿⣿⣿⣿⣿⣠⠞⠉⢀⣤⣿⡭⠷⡺⠚⠉⢩⣾⣩⡿⠻⣦⡀⠀⠀⠀⠁⠲⡠⠒⠁⠀⣴⣈⡿⠷⣦⠀⠈⠈⠙⠻⣄⠀⠀
⠀⢸⣿⣿⣿⣿⣿⡭⠟⠉⠁⠀⠀⠘⠓⠒⠲⡉⠀⠀⠀⢸⣿⣬⣷⣶⣿⡇⠀⠀⠀⠀⠈⠀⠀⠀⢸⣿⣧⣿⣶⣿⠇⠀⠀⠀⠀⣸⠀⠀
⠀⠀⠈⠓⣿⠶⠟⠁⠀⠀⠀⠀⠀⠘⠛⠂⠀⠈⠑⠠⢀⠈⢛⣿⣿⠛⠋⣀⡀⣀⠠⠐⠂⠀⢀⣀⠀⠙⠻⠿⢿⣍⡀⠤⠤⠒⢊⡍⠀⠀
⠀⠀⠀⡴⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⢠⣄⣤⣾⣿⣷⣤⠀⠀⠀⠀⠀⠀⣀⡤⡎⠀⠀⠀
⠀⠀⡸⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣠⡤⠤⠶⠦⢤⣤⣀⠀⠀⢠⣿⣿⣿⣤⣿⣿⣿⣿⣇⣀⣀⣤⢶⠛⠁⢀⡏⠀⠀⠀
⠀⢰⠃⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠸⣅⡀⠐⠦⣤⣤⣀⣈⠉⠙⠻⣿⣿⣿⣿⣿⣿⣿⡿⠉⠀⢀⣀⣠⣤⠴⢻⠀⠀⠀⠀
⠀⢸⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠙⠳⠶⢤⣄⣉⠉⠛⠛⠛⠻⠻⣿⣿⣿⠿⠿⠿⠛⠛⠋⠉⠁⣀⣴⠏⠀⠀⠀⠀
⠀⡏⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠰⢦⣀⠀⠀⠀⠀⠀⠀⠈⠉⠓⠒⠦⠤⠤⠤⠤⠤⠤⠤⠤⠤⠤⠶⠒⠋⠉⡘⠀⠀⠀⠀⠀
⢀⣧⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⡇⠀⠀⠀⠀⠀
⢸⠀⠙⠦⣄⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡔⠑⠒⠦⢤⣄⠀⠀⠀⠀⠀⠀⠀⣀⠤⠤⠤⢤⣀⣀⣀⠴⠚⠉⠀⢸⠀⠀⠀⠀⠀
⢸⠀⠀⠀⠈⠉⠛⠒⠦⠤⢤⣀⣀⣀⣀⣀⣀⣀⣰⠁⠀⠀⠀⠀⠈⠑⡤⠒⠒⢢⠖⣉⠀⠀⠀⠀⠀⠉⠁⠀⠀⠀⠀⠀⠈⠳⣄⠀⠀⠀
⠸⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⠀⠀⠀⠀⠀⠀⠩⠇⠀⠀⠀⠧⠀⠀⠀⠀⠀⠀⠁⠀⠀⠀⠀⠀⠀⠀⠀⠈⢦⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⠀⠀⠀⠀⠀⡀⠜⠒⠤⠀⠐⠒⠤⡀⠀⠀⠀⡰⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠱⡄
*/
