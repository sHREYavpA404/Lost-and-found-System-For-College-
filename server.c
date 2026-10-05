/**
 * ============================================================================
 * PROJECT: College Lost and Found Management System - C Web Server Backend
 * LANGUAGE: Pure C (C99 Standard compliant with WinSock2)
 * PLATFORM: Windows (MinGW / GCC)
 * ============================================================================
 *
 * ARCHITECTURE:
 * - Serves the modern HTML/JS web portal at http://localhost:8080
 * - Exposes high-performance REST JSON API endpoints:
 *     GET  /api/health
 *     GET  /api/items
 *     POST /api/items
 *     DELETE /api/items?id=...
 *     GET  /api/claims
 *     POST /api/claims
 *     POST /api/claims/process
 *     GET  /api/history
 *     POST /api/admin/login
 *
 * DATA STRUCTURES:
 * 1. Linked List: Stores and manages active lost items inventory dynamically.
 * 2. FIFO Queue: Manages pending claim requests waiting list.
 * 3. File I/O: Persists items to items.txt and maintains the 1-Year Retention
 *    audit database in claim_history.txt.
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define PORT 8080
#define BUFFER_SIZE 65536
#define DEFAULT_ADMIN_PASSWORD "admin123"
#define COLLEGE_EMAIL_DOMAIN   "@college.edu"

#define ITEMS_FILE         "items.txt"
#define CLAIMS_QUEUE_FILE  "claims_queue.txt"
#define CLAIM_HISTORY_FILE "claim_history.txt"
#define WEB_HTML_FILE      "web/index.html"

#define MAX_NAME_LEN  64
#define MAX_DESC_LEN  256
#define MAX_LOC_LEN   128
#define MAX_PATH_LEN  260
#define MAX_DATE_LEN  32
#define MAX_USN_LEN   32
#define MAX_PHONE_LEN 20
#define MAX_EMAIL_LEN 80

typedef enum {
    STATUS_AVAILABLE = 0,
    STATUS_PENDING_CLAIM = 1,
    STATUS_CLAIMED = 2
} ItemStatus;

const char* status_to_str(ItemStatus status) {
    switch (status) {
        case STATUS_AVAILABLE:     return "AVAILABLE";
        case STATUS_PENDING_CLAIM: return "PENDING CLAIM";
        case STATUS_CLAIMED:       return "CLAIMED";
        default:                   return "UNKNOWN";
    }
}

/* ----------------------------------------------------------------------------
 * DATA STRUCTURE: Linked List for Items Inventory
 * ---------------------------------------------------------------------------- */
typedef struct ItemNode {
    int id;
    char name[MAX_NAME_LEN];
    char description[MAX_DESC_LEN];
    char location[MAX_LOC_LEN];
    char image_path[MAX_PATH_LEN];
    char date_found[MAX_DATE_LEN];
    ItemStatus status;
    struct ItemNode *next;
} ItemNode;

/* ----------------------------------------------------------------------------
 * DATA STRUCTURE: Claimant & FIFO Queue
 * ---------------------------------------------------------------------------- */
typedef struct {
    char name[MAX_NAME_LEN];
    char usn[MAX_USN_LEN];
    char phone[MAX_PHONE_LEN];
    char email[MAX_EMAIL_LEN];
    char claim_date[MAX_DATE_LEN];
} Claimant;

typedef struct ClaimRequestNode {
    int request_id;
    int item_id;
    Claimant claimant;
    char notes[MAX_DESC_LEN];
    char request_date[MAX_DATE_LEN];
    struct ClaimRequestNode *next;
} ClaimRequestNode;

typedef struct {
    ClaimRequestNode *front;
    ClaimRequestNode *rear;
    int count;
} ClaimQueue;

/* Global state */
static ItemNode *g_inventory_head = NULL;
static ClaimQueue g_claim_queue = { NULL, NULL, 0 };
static int g_next_item_id = 101;
static int g_next_request_id = 1001;

/* ----------------------------------------------------------------------------
 * HELPERS & VALIDATION
 * ---------------------------------------------------------------------------- */
void get_current_timestamp(char *buf, size_t sz) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t) strftime(buf, sz, "%Y-%m-%d %H:%M:%S", t);
    else snprintf(buf, sz, "2026-01-01 00:00:00");
}

void trim(char *str) {
    if (!str) return;
    int len = (int)strlen(str);
    while (len > 0 && (isspace((unsigned char)str[len - 1]) || str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
    int start = 0;
    while (str[start] && isspace((unsigned char)str[start])) start++;
    if (start > 0) memmove(str, str + start, len - start + 1);
}

int validate_college_email(const char *email) {
    if (!email) return 0;
    size_t e_len = strlen(email);
    size_t d_len = strlen(COLLEGE_EMAIL_DOMAIN);
    if (e_len <= d_len) return 0;
    const char *suffix = email + (e_len - d_len);
    for (size_t i = 0; i < d_len; i++) {
        if (tolower((unsigned char)suffix[i]) != tolower((unsigned char)COLLEGE_EMAIL_DOMAIN[i])) return 0;
    }
    return 1;
}

/* Escape JSON string */
void escape_json(const char *src, char *dest, size_t max_dest) {
    size_t d = 0;
    for (size_t s = 0; src[s] && d < max_dest - 2; s++) {
        if (src[s] == '"' || src[s] == '\\') {
            if (d < max_dest - 3) {
                dest[d++] = '\\';
                dest[d++] = src[s];
            }
        } else if (src[s] == '\n') {
            if (d < max_dest - 3) {
                dest[d++] = '\\';
                dest[d++] = 'n';
            }
        } else if (src[s] == '\r') {
            /* ignore */
        } else {
            dest[d++] = src[s];
        }
    }
    dest[d] = '\0';
}

/* Parse a string field from simple flat JSON {"key":"val"} */
int get_json_string(const char *json, const char *key, char *out, size_t max_out) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return 0;
    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    if (*p != '"') return 0;
    p++; /* skip opening quote */
    size_t i = 0;
    while (*p && *p != '"' && i < max_out - 1) {
        if (*p == '\\' && *(p + 1)) p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

int get_json_int(const char *json, const char *key, int default_val) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return default_val;
    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    return atoi(p);
}

/* ----------------------------------------------------------------------------
 * LINKED LIST OPERATIONS
 * ---------------------------------------------------------------------------- */
ItemNode* create_item_node(int id, const char *name, const char *desc,
                           const char *loc, const char *img, const char *date, ItemStatus st) {
    ItemNode *node = (ItemNode*)malloc(sizeof(ItemNode));
    if (!node) return NULL;
    node->id = id;
    strncpy(node->name, name ? name : "Unknown", sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
    strncpy(node->description, desc ? desc : "None", sizeof(node->description) - 1);
    node->description[sizeof(node->description) - 1] = '\0';
    strncpy(node->location, loc ? loc : "Unknown", sizeof(node->location) - 1);
    node->location[sizeof(node->location) - 1] = '\0';
    strncpy(node->image_path, img ? img : "None", sizeof(node->image_path) - 1);
    node->image_path[sizeof(node->image_path) - 1] = '\0';
    strncpy(node->date_found, date ? date : "N/A", sizeof(node->date_found) - 1);
    node->date_found[sizeof(node->date_found) - 1] = '\0';
    node->status = st;
    node->next = NULL;
    return node;
}

void insert_item_sorted(ItemNode **head_ref, ItemNode *new_node) {
    if (!head_ref || !new_node) return;
    if (*head_ref == NULL || (*head_ref)->id >= new_node->id) {
        new_node->next = *head_ref;
        *head_ref = new_node;
        return;
    }
    ItemNode *curr = *head_ref;
    while (curr->next != NULL && curr->next->id < new_node->id) {
        curr = curr->next;
    }
    new_node->next = curr->next;
    curr->next = new_node;
}

ItemNode* find_item(ItemNode *head, int id) {
    ItemNode *curr = head;
    while (curr) {
        if (curr->id == id) return curr;
        curr = curr->next;
    }
    return NULL;
}

int delete_item(ItemNode **head_ref, int id) {
    if (!head_ref || !*head_ref) return 0;
    ItemNode *curr = *head_ref;
    ItemNode *prev = NULL;
    while (curr && curr->id != id) {
        prev = curr;
        curr = curr->next;
    }
    if (!curr) return 0;
    if (!prev) *head_ref = curr->next;
    else prev->next = curr->next;
    free(curr);
    return 1;
}

/* ----------------------------------------------------------------------------
 * FIFO QUEUE OPERATIONS
 * ---------------------------------------------------------------------------- */
int enqueue_claim(ClaimQueue *q, int req_id, int item_id, const Claimant *c,
                  const char *notes, const char *req_date) {
    if (!q || !c) return 0;
    ClaimRequestNode *node = (ClaimRequestNode*)malloc(sizeof(ClaimRequestNode));
    if (!node) return 0;
    node->request_id = req_id;
    node->item_id = item_id;
    node->claimant = *c;
    strncpy(node->notes, notes ? notes : "", sizeof(node->notes) - 1);
    node->notes[sizeof(node->notes) - 1] = '\0';
    strncpy(node->request_date, req_date ? req_date : "N/A", sizeof(node->request_date) - 1);
    node->request_date[sizeof(node->request_date) - 1] = '\0';
    node->next = NULL;

    if (q->rear == NULL) {
        q->front = node;
        q->rear = node;
    } else {
        q->rear->next = node;
        q->rear = node;
    }
    q->count++;
    return 1;
}

int dequeue_claim_by_id(ClaimQueue *q, int req_id, ClaimRequestNode *out_node) {
    if (!q || !q->front) return 0;
    ClaimRequestNode *curr = q->front;
    ClaimRequestNode *prev = NULL;

    while (curr && curr->request_id != req_id) {
        prev = curr;
        curr = curr->next;
    }
    if (!curr) return 0;

    if (out_node) {
        *out_node = *curr;
        out_node->next = NULL;
    }

    if (!prev) q->front = curr->next;
    else prev->next = curr->next;

    if (q->rear == curr) q->rear = prev;
    q->count--;
    free(curr);
    return 1;
}

/* ----------------------------------------------------------------------------
 * FILE PERSISTENCE & 1-YEAR RETENTION DATABASE
 * ---------------------------------------------------------------------------- */
void save_inventory_to_file(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) return;
    fprintf(fp, "# ID|NAME|DESCRIPTION|LOCATION|IMAGE_PATH|DATE_FOUND|STATUS\n");
    ItemNode *curr = g_inventory_head;
    while (curr) {
        fprintf(fp, "%d|%s|%s|%s|%s|%s|%d\n",
                curr->id, curr->name, curr->description, curr->location,
                curr->image_path, curr->date_found, (int)curr->status);
        curr = curr->next;
    }
    fclose(fp);
}

void load_inventory_from_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        /* Add initial sample data if file does not exist */
        ItemNode *n1 = create_item_node(101, "Titan Blue Watch",
            "Dark blue leather strap, round silver dial, scratch on bezel.",
            "Block B, Room 204", "C:/images/watch.jpg", "2026-10-01", STATUS_AVAILABLE);
        insert_item_sorted(&g_inventory_head, n1);

        ItemNode *n2 = create_item_node(102, "Scientific Calculator Casio FX-991EX",
            "Black casing with sticker 'SS' on the protective slip cover.",
            "Basketball Court", "C:/images/calc.jpg", "2026-10-02", STATUS_AVAILABLE);
        insert_item_sorted(&g_inventory_head, n2);

        ItemNode *n3 = create_item_node(103, "Dell Wireless Mouse",
            "Matte black optical mouse, missing battery cover clip.",
            "Block C, Room 302", "None", "2026-10-03", STATUS_AVAILABLE);
        insert_item_sorted(&g_inventory_head, n3);

        save_inventory_to_file(filename);
        return;
    }

    char line[1024];
    int max_id = 100;
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0 || line[0] == '#') continue;
        int id = 0, st_int = 0;
        char name[MAX_NAME_LEN] = {0}, desc[MAX_DESC_LEN] = {0}, loc[MAX_LOC_LEN] = {0};
        char img[MAX_PATH_LEN] = {0}, date[MAX_DATE_LEN] = {0};

        char *tok = strtok(line, "|"); if (tok) id = atoi(tok);
        tok = strtok(NULL, "|"); if (tok) strncpy(name, tok, sizeof(name) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(desc, tok, sizeof(desc) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(loc, tok, sizeof(loc) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(img, tok, sizeof(img) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(date, tok, sizeof(date) - 1);
        tok = strtok(NULL, "|"); if (tok) st_int = atoi(tok);

        if (id > 0) {
            ItemNode *node = create_item_node(id, name, desc, loc, img, date, (ItemStatus)st_int);
            insert_item_sorted(&g_inventory_head, node);
            if (id > max_id) max_id = id;
        }
    }
    fclose(fp);
    g_next_item_id = max_id + 1;
}

void save_queue_to_file(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) return;
    fprintf(fp, "# REQ_ID|ITEM_ID|CLAIMANT_NAME|USN|PHONE|EMAIL|CLAIM_DATE|NOTES|REQ_DATE\n");
    ClaimRequestNode *curr = g_claim_queue.front;
    while (curr) {
        fprintf(fp, "%d|%d|%s|%s|%s|%s|%s|%s|%s\n",
                curr->request_id, curr->item_id, curr->claimant.name,
                curr->claimant.usn, curr->claimant.phone, curr->claimant.email,
                curr->claimant.claim_date, curr->notes, curr->request_date);
        curr = curr->next;
    }
    fclose(fp);
}

void load_queue_from_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return;
    char line[1024];
    int max_req = 1000;
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0 || line[0] == '#') continue;
        int req_id = 0, item_id = 0;
        Claimant c = {0};
        char notes[MAX_DESC_LEN] = {0}, req_date[MAX_DATE_LEN] = {0};

        char *tok = strtok(line, "|"); if (tok) req_id = atoi(tok);
        tok = strtok(NULL, "|"); if (tok) item_id = atoi(tok);
        tok = strtok(NULL, "|"); if (tok) strncpy(c.name, tok, sizeof(c.name) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(c.usn, tok, sizeof(c.usn) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(c.phone, tok, sizeof(c.phone) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(c.email, tok, sizeof(c.email) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(c.claim_date, tok, sizeof(c.claim_date) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(notes, tok, sizeof(notes) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(req_date, tok, sizeof(req_date) - 1);

        if (req_id > 0 && item_id > 0) {
            enqueue_claim(&g_claim_queue, req_id, item_id, &c, notes, req_date);
            if (req_id > max_req) max_req = req_id;
        }
    }
    fclose(fp);
    g_next_request_id = max_req + 1;
}

/**
 * Appends claim record to 1-Year Retention Archive (claim_history.txt).
 */
void append_claim_to_history(const ItemNode *item, const Claimant *claimant, const char *remarks) {
    FILE *fp = fopen(CLAIM_HISTORY_FILE, "a");
    if (!fp) return;
    char now_str[MAX_DATE_LEN];
    get_current_timestamp(now_str, sizeof(now_str));

    fprintf(fp, "================================================================================\n");
    fprintf(fp, "TRANSACTION RECORD: CLAIM & DISBURSEMENT AUDIT LOG\n");
    fprintf(fp, "Log Timestamp       : %s\n", now_str);
    fprintf(fp, "[RETENTION POLICY]  : Retain this record for 1-Year (365 days) from log date.\n");
    fprintf(fp, "--------------------------------------------------------------------------------\n");
    fprintf(fp, "ITEM INFORMATION:\n");
    fprintf(fp, "  - Item ID         : #%d\n", item->id);
    fprintf(fp, "  - Item Name       : %s\n", item->name);
    fprintf(fp, "  - Description     : %s\n", item->description);
    fprintf(fp, "  - Found Location  : %s\n", item->location);
    fprintf(fp, "  - Date Found      : %s\n", item->date_found);
    fprintf(fp, "  - Image Path      : %s\n", item->image_path);
    fprintf(fp, "CLAIMANT DETAILS (BORROWER / RECIPIENT):\n");
    fprintf(fp, "  - Full Name       : %s\n", claimant->name);
    fprintf(fp, "  - College USN     : %s\n", claimant->usn);
    fprintf(fp, "  - Contact Phone   : %s\n", claimant->phone);
    fprintf(fp, "  - Verified Email  : %s\n", claimant->email);
    fprintf(fp, "  - Claim Timestamp : %s\n", claimant->claim_date);
    fprintf(fp, "OFFICE VERIFICATION:\n");
    fprintf(fp, "  - Handover Status : CLAIMED & VERIFIED BY ADMIN\n");
    fprintf(fp, "  - Admin Remarks   : %s\n", (remarks && strlen(remarks) > 0) ? remarks : "Physical College ID Card Verified");
    fprintf(fp, "================================================================================\n\n");
    fclose(fp);
}

/* ----------------------------------------------------------------------------
 * HTTP RESPONSE HELPERS
 * ---------------------------------------------------------------------------- */
void send_http_response(SOCKET sock, int status_code, const char *status_text,
                        const char *content_type, const char *body, size_t body_len) {
    char header[1024];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n",
        status_code, status_text, content_type, body_len);

    send(sock, header, (int)strlen(header), 0);
    if (body && body_len > 0) {
        send(sock, body, (int)body_len, 0);
    }
}

void send_cors_preflight(SOCKET sock) {
    const char *resp =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n";
    send(sock, resp, (int)strlen(resp), 0);
}

/* ----------------------------------------------------------------------------
 * HTTP REQUEST DISPATCHER
 * ---------------------------------------------------------------------------- */
void handle_client_request(SOCKET client_sock) {
    char request_buf[BUFFER_SIZE];
    int bytes_received = recv(client_sock, request_buf, sizeof(request_buf) - 1, 0);
    if (bytes_received <= 0) return;
    request_buf[bytes_received] = '\0';

    char method[16] = {0};
    char path[256] = {0};
    sscanf(request_buf, "%15s %255s", method, path);

    /* Locate HTTP Body */
    char *body_ptr = strstr(request_buf, "\r\n\r\n");
    if (body_ptr) body_ptr += 4;
    else body_ptr = "";

    /* OPTIONS Pre-flight */
    if (strcmp(method, "OPTIONS") == 0) {
        send_cors_preflight(client_sock);
        return;
    }

    /* GET / or /index.html: Serve Web Interface */
    if (strcmp(method, "GET") == 0 && (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
        FILE *fp = fopen(WEB_HTML_FILE, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long fsize = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            char *html_content = (char*)malloc(fsize + 1);
            if (html_content) {
                fread(html_content, 1, fsize, fp);
                html_content[fsize] = '\0';
                send_http_response(client_sock, 200, "OK", "text/html", html_content, fsize);
                free(html_content);
            }
            fclose(fp);
        } else {
            const char *err = "<h1>404: web/index.html not found</h1>";
            send_http_response(client_sock, 404, "Not Found", "text/html", err, strlen(err));
        }
        return;
    }

    /* GET /api/health */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/health") == 0) {
        const char *json = "{\"status\":\"ok\",\"service\":\"LostAndFound_C_Engine\"}";
        send_http_response(client_sock, 200, "OK", "application/json", json, strlen(json));
        return;
    }

    /* GET /api/items: Serialize Linked List to JSON */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/items") == 0) {
        char *json_buf = (char*)malloc(BUFFER_SIZE * 2);
        if (!json_buf) return;
        strcpy(json_buf, "[\n");

        ItemNode *curr = g_inventory_head;
        int first = 1;
        while (curr) {
            char esc_name[MAX_NAME_LEN * 2];
            char esc_desc[MAX_DESC_LEN * 2];
            char esc_loc[MAX_LOC_LEN * 2];
            char esc_img[MAX_PATH_LEN * 2];
            escape_json(curr->name, esc_name, sizeof(esc_name));
            escape_json(curr->description, esc_desc, sizeof(esc_desc));
            escape_json(curr->location, esc_loc, sizeof(esc_loc));
            escape_json(curr->image_path, esc_img, sizeof(esc_img));

            char item_json[2048];
            snprintf(item_json, sizeof(item_json),
                "%s  {\"id\":%d,\"name\":\"%s\",\"description\":\"%s\",\"location\":\"%s\","
                "\"image_path\":\"%s\",\"date_found\":\"%s\",\"status\":\"%s\"}",
                first ? "" : ",\n",
                curr->id, esc_name, esc_desc, esc_loc, esc_img, curr->date_found, status_to_str(curr->status));
            strcat(json_buf, item_json);
            first = 0;
            curr = curr->next;
        }
        strcat(json_buf, "\n]");
        send_http_response(client_sock, 200, "OK", "application/json", json_buf, strlen(json_buf));
        free(json_buf);
        return;
    }

    /* POST /api/items: Add item to Linked List */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/items") == 0) {
        char name[MAX_NAME_LEN] = {0}, desc[MAX_DESC_LEN] = {0}, loc[MAX_LOC_LEN] = {0};
        char img[MAX_PATH_LEN] = {0}, date[MAX_DATE_LEN] = {0};

        get_json_string(body_ptr, "name", name, sizeof(name));
        get_json_string(body_ptr, "description", desc, sizeof(desc));
        get_json_string(body_ptr, "location", loc, sizeof(loc));
        get_json_string(body_ptr, "image_path", img, sizeof(img));
        get_json_string(body_ptr, "date_found", date, sizeof(date));

        int new_id = g_next_item_id++;
        ItemNode *node = create_item_node(new_id, name, desc, loc, img, date, STATUS_AVAILABLE);
        insert_item_sorted(&g_inventory_head, node);
        save_inventory_to_file(ITEMS_FILE);

        char resp[256];
        snprintf(resp, sizeof(resp), "{\"status\":\"created\",\"id\":%d}", new_id);
        send_http_response(client_sock, 201, "Created", "application/json", resp, strlen(resp));
        return;
    }

    /* DELETE /api/items?id=... */
    if (strcmp(method, "DELETE") == 0 && strncmp(path, "/api/items", 10) == 0) {
        char *q = strstr(path, "id=");
        if (q) {
            int del_id = atoi(q + 3);
            if (delete_item(&g_inventory_head, del_id)) {
                save_inventory_to_file(ITEMS_FILE);
                const char *res = "{\"status\":\"deleted\"}";
                send_http_response(client_sock, 200, "OK", "application/json", res, strlen(res));
                return;
            }
        }
        const char *res = "{\"error\":\"Item not found\"}";
        send_http_response(client_sock, 404, "Not Found", "application/json", res, strlen(res));
        return;
    }

    /* GET /api/claims: Return Queue */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/claims") == 0) {
        char *json_buf = (char*)malloc(BUFFER_SIZE);
        if (!json_buf) return;
        strcpy(json_buf, "[\n");

        ClaimRequestNode *curr = g_claim_queue.front;
        int first = 1;
        while (curr) {
            char esc_name[MAX_NAME_LEN * 2], esc_notes[MAX_DESC_LEN * 2];
            escape_json(curr->claimant.name, esc_name, sizeof(esc_name));
            escape_json(curr->notes, esc_notes, sizeof(esc_notes));

            char q_json[1024];
            snprintf(q_json, sizeof(q_json),
                "%s  {\"request_id\":%d,\"item_id\":%d,\"notes\":\"%s\",\"request_date\":\"%s\","
                "\"claimant\":{\"name\":\"%s\",\"usn\":\"%s\",\"phone\":\"%s\",\"email\":\"%s\",\"claim_date\":\"%s\"}}",
                first ? "" : ",\n",
                curr->request_id, curr->item_id, esc_notes, curr->request_date,
                esc_name, curr->claimant.usn, curr->claimant.phone, curr->claimant.email, curr->claimant.claim_date);
            strcat(json_buf, q_json);
            first = 0;
            curr = curr->next;
        }
        strcat(json_buf, "\n]");
        send_http_response(client_sock, 200, "OK", "application/json", json_buf, strlen(json_buf));
        free(json_buf);
        return;
    }

    /* POST /api/claims: Enqueue claim request */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/claims") == 0) {
        int item_id = get_json_int(body_ptr, "item_id", 0);
        char notes[MAX_DESC_LEN] = {0};
        Claimant c = {0};

        get_json_string(body_ptr, "name", c.name, sizeof(c.name));
        get_json_string(body_ptr, "usn", c.usn, sizeof(c.usn));
        get_json_string(body_ptr, "phone", c.phone, sizeof(c.phone));
        get_json_string(body_ptr, "email", c.email, sizeof(c.email));
        get_json_string(body_ptr, "notes", notes, sizeof(notes));

        /* Strict Email Validation (@college.edu) */
        if (!validate_college_email(c.email)) {
            const char *err = "{\"error\":\"Invalid email: must end with @college.edu\"}";
            send_http_response(client_sock, 400, "Bad Request", "application/json", err, strlen(err));
            return;
        }

        get_current_timestamp(c.claim_date, sizeof(c.claim_date));
        char req_date[MAX_DATE_LEN];
        get_current_timestamp(req_date, sizeof(req_date));

        int req_id = g_next_request_id++;
        enqueue_claim(&g_claim_queue, req_id, item_id, &c, notes, req_date);
        save_queue_to_file(CLAIMS_QUEUE_FILE);

        /* Update item status in Linked List */
        ItemNode *itm = find_item(g_inventory_head, item_id);
        if (itm) {
            itm->status = STATUS_PENDING_CLAIM;
            save_inventory_to_file(ITEMS_FILE);
        }

        char resp[256];
        snprintf(resp, sizeof(resp), "{\"status\":\"enqueued\",\"request_id\":%d,\"position\":%d}",
                 req_id, g_claim_queue.count);
        send_http_response(client_sock, 201, "Created", "application/json", resp, strlen(resp));
        return;
    }

    /* POST /api/claims/process: Approve or Reject */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/claims/process") == 0) {
        int req_id = get_json_int(body_ptr, "request_id", 0);
        char action[32] = {0};
        char remarks[MAX_DESC_LEN] = {0};
        get_json_string(body_ptr, "action", action, sizeof(action));
        get_json_string(body_ptr, "remarks", remarks, sizeof(remarks));

        ClaimRequestNode dequeued;
        if (dequeue_claim_by_id(&g_claim_queue, req_id, &dequeued)) {
            save_queue_to_file(CLAIMS_QUEUE_FILE);
            ItemNode *itm = find_item(g_inventory_head, dequeued.item_id);

            if (strcmp(action, "APPROVE") == 0) {
                if (itm) itm->status = STATUS_CLAIMED;
                save_inventory_to_file(ITEMS_FILE);

                if (itm) {
                    append_claim_to_history(itm, &dequeued.claimant, remarks);
                }
            } else {
                if (itm) itm->status = STATUS_AVAILABLE;
                save_inventory_to_file(ITEMS_FILE);
            }

            const char *res = "{\"status\":\"processed\"}";
            send_http_response(client_sock, 200, "OK", "application/json", res, strlen(res));
            return;
        }

        const char *err = "{\"error\":\"Claim request not found in queue\"}";
        send_http_response(client_sock, 404, "Not Found", "application/json", err, strlen(err));
        return;
    }

    /* GET /api/history: Read claim_history.txt into JSON */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/history") == 0) {
        FILE *fp = fopen(CLAIM_HISTORY_FILE, "r");
        if (!fp) {
            const char *empty = "[]";
            send_http_response(client_sock, 200, "OK", "application/json", empty, strlen(empty));
            return;
        }

        char *json_buf = (char*)malloc(BUFFER_SIZE * 2);
        strcpy(json_buf, "[\n");
        char line[512];
        int first = 1;

        char log_time[MAX_DATE_LEN] = {0};
        char item_id_str[32] = {0};
        char item_name[MAX_NAME_LEN] = {0};
        char location[MAX_LOC_LEN] = {0};
        char c_name[MAX_NAME_LEN] = {0};
        char c_usn[MAX_USN_LEN] = {0};
        char c_phone[MAX_PHONE_LEN] = {0};
        char c_email[MAX_EMAIL_LEN] = {0};
        char remarks[MAX_DESC_LEN] = {0};

        while (fgets(line, sizeof(line), fp)) {
            trim(line);
            if (strncmp(line, "Log Timestamp       :", 21) == 0) {
                strncpy(log_time, line + 22, sizeof(log_time) - 1); trim(log_time);
            } else if (strstr(line, "- Item ID         :")) {
                char *p = strchr(line, '#');
                if (p) { strncpy(item_id_str, p + 1, sizeof(item_id_str) - 1); trim(item_id_str); }
            } else if (strstr(line, "- Item Name       :")) {
                char *p = strchr(line, ':');
                if (p) { strncpy(item_name, p + 1, sizeof(item_name) - 1); trim(item_name); }
            } else if (strstr(line, "- Found Location  :")) {
                char *p = strchr(line, ':');
                if (p) { strncpy(location, p + 1, sizeof(location) - 1); trim(location); }
            } else if (strstr(line, "- Full Name       :")) {
                char *p = strchr(line, ':');
                if (p) { strncpy(c_name, p + 1, sizeof(c_name) - 1); trim(c_name); }
            } else if (strstr(line, "- College USN     :")) {
                char *p = strchr(line, ':');
                if (p) { strncpy(c_usn, p + 1, sizeof(c_usn) - 1); trim(c_usn); }
            } else if (strstr(line, "- Contact Phone   :")) {
                char *p = strchr(line, ':');
                if (p) { strncpy(c_phone, p + 1, sizeof(c_phone) - 1); trim(c_phone); }
            } else if (strstr(line, "- Verified Email  :")) {
                char *p = strchr(line, ':');
                if (p) { strncpy(c_email, p + 1, sizeof(c_email) - 1); trim(c_email); }
            } else if (strstr(line, "- Admin Remarks   :")) {
                char *p = strchr(line, ':');
                if (p) { strncpy(remarks, p + 1, sizeof(remarks) - 1); trim(remarks); }
            } else if (strncmp(line, "====", 4) == 0 && strlen(log_time) > 0) {
                char entry[1024];
                snprintf(entry, sizeof(entry),
                    "%s  {\"log_time\":\"%s\",\"item_id\":%d,\"item_name\":\"%s\",\"location\":\"%s\","
                    "\"remarks\":\"%s\",\"claimant\":{\"name\":\"%s\",\"usn\":\"%s\",\"phone\":\"%s\",\"email\":\"%s\"}}",
                    first ? "" : ",\n",
                    log_time, atoi(item_id_str), item_name, location, remarks,
                    c_name, c_usn, c_phone, c_email);
                strcat(json_buf, entry);
                first = 0;
                log_time[0] = '\0';
            }
        }
        fclose(fp);
        strcat(json_buf, "\n]");
        send_http_response(client_sock, 200, "OK", "application/json", json_buf, strlen(json_buf));
        free(json_buf);
        return;
    }

    /* POST /api/admin/login */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/admin/login") == 0) {
        char pw[64] = {0};
        get_json_string(body_ptr, "password", pw, sizeof(pw));
        if (strcmp(pw, DEFAULT_ADMIN_PASSWORD) == 0) {
            const char *res = "{\"status\":\"authenticated\"}";
            send_http_response(client_sock, 200, "OK", "application/json", res, strlen(res));
        } else {
            const char *err = "{\"error\":\"Invalid password\"}";
            send_http_response(client_sock, 401, "Unauthorized", "application/json", err, strlen(err));
        }
        return;
    }

    /* Default 404 */
    const char *err = "{\"error\":\"Resource not found\"}";
    send_http_response(client_sock, 404, "Not Found", "application/json", err, strlen(err));
}

/* ----------------------------------------------------------------------------
 * MAIN SERVER LOOP
 * ---------------------------------------------------------------------------- */
int main(void) {
    printf("====================================================================\n");
    printf("    COLLEGE LOST & FOUND MANAGEMENT SYSTEM - C WEB SERVER           \n");
    printf("====================================================================\n");

    /* Initialize Data Structures from Flat Files */
    load_inventory_from_file(ITEMS_FILE);
    load_queue_from_file(CLAIMS_QUEUE_FILE);

    printf("[+] Active Inventory Linked List loaded.\n");
    printf("[+] Pending Claim Requests FIFO Queue loaded.\n");

    /* Initialize WinSock2 */
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[ERROR] WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == INVALID_SOCKET) {
        fprintf(stderr, "[ERROR] Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] Bind failed on port %d: %d\n", PORT, WSAGetLastError());
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    if (listen(server_sock, 10) == SOCKET_ERROR) {
        fprintf(stderr, "[ERROR] Listen failed: %d\n", WSAGetLastError());
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    printf("\n====================================================================\n");
    printf(" [ONLINE] Web Server running at: http://localhost:%d/\n", PORT);
    printf(" [ONLINE] Serving Frontend     : %s\n", WEB_HTML_FILE);
    printf(" [ONLINE] REST API Endpoint    : http://localhost:%d/api/items\n", PORT);
    printf(" [ONLINE] Admin Password       : %s\n", DEFAULT_ADMIN_PASSWORD);
    printf("====================================================================\n");
    printf(" Press Ctrl+C in console to stop server.\n\n");

    while (1) {
        struct sockaddr_in client_addr;
        int client_addr_len = sizeof(client_addr);
        SOCKET client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_sock == INVALID_SOCKET) continue;

        handle_client_request(client_sock);
        closesocket(client_sock);
    }

    closesocket(server_sock);
    WSACleanup();
    return 0;
}
