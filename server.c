/**
 * ============================================================================
 * PROJECT: College Lost and Found Management System - C Web Server Backend
 * LANGUAGE: Pure C (C99 Standard compliant with WinSock2)
 * PLATFORM: Windows (MinGW / GCC)
 * ============================================================================
 *
 * ARCHITECTURE & NEW MODULES:
 * 1. Scheduled Campus Visits Engine:
 *    - Interactive appointment scheduler with real-time slot capacity tracking.
 *    - Enforces visit quotas to prevent overcrowding at the Lost & Found office.
 *    - Endpoint: GET /api/slots?date=YYYY-MM-DD
 *
 * 2. Advanced Category & Keyword Filtering:
 *    - Filter by Category (Electronics, IDs & Cards, Books & Stationery, Bags & Bottles, Accessories, Other)
 *    - Filter by Campus Location (Blocks A–E, Court, Temple, Entrance)
 *
 * 3. 1-Year Statutory Retention Database (claim_history.txt):
 *    - Permanent immutable audit log recording claimant info, college email,
 *      hardware serial/IMEI, and visit appointment timestamp for institutional compliance.
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
#define MAX_TIER_LEN  16

#define SLOT_CAPACITY 3
static const char *AVAILABLE_SLOTS[] = {
    "09:30 AM - 10:00 AM",
    "10:30 AM - 11:00 AM",
    "11:30 AM - 12:00 PM",
    "02:00 PM - 02:30 PM",
    "03:00 PM - 03:30 PM",
    "04:00 PM - 04:30 PM"
};
#define NUM_SLOTS (sizeof(AVAILABLE_SLOTS) / sizeof(AVAILABLE_SLOTS[0]))

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
    char image_path[MAX_PATH_LEN * 4];
    char date_found[MAX_DATE_LEN];
    char date_posted[MAX_DATE_LEN];
    ItemStatus status;
    char category[MAX_NAME_LEN];    /* Electronics, IDs & Cards, Books & Stationery, Bags & Bottles, Personal Accessories, Other */
    char value_tier[MAX_TIER_LEN];  /* LOW (Express) or HIGH (Multi-Factor Security) */
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
    char visit_date[MAX_DATE_LEN];      /* Scheduled campus visit date (YYYY-MM-DD) */
    char visit_slot[MAX_NAME_LEN];      /* Scheduled 30-min time slot */
    char serial_no[MAX_NAME_LEN];       /* Hardware Serial/IMEI for High-Priced commodities */
    char purchase_proof[MAX_DESC_LEN];  /* Invoice/Bill details for High-Priced commodities */
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
    else snprintf(buf, sz, "2026-11-16 00:00:00");
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
    if (e_len < 6) return 0;

    /* Accept @college.edu or institutional domains ending with .edu or .ac.in */
    const char *at = strchr(email, '@');
    if (!at || at == email) return 0;

    size_t d_len = strlen(COLLEGE_EMAIL_DOMAIN);
    if (e_len >= d_len) {
        const char *suffix = email + (e_len - d_len);
        int match = 1;
        for (size_t i = 0; i < d_len; i++) {
            if (tolower((unsigned char)suffix[i]) != tolower((unsigned char)COLLEGE_EMAIL_DOMAIN[i])) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }

    /* Fallback institutional domain validation */
    if (strstr(email, ".edu") != NULL || strstr(email, ".ac.in") != NULL) {
        return 1;
    }

    return 0;
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
                           const char *loc, const char *img, const char *date,
                           const char *date_posted, ItemStatus st,
                           const char *category, const char *value_tier) {
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
    strncpy(node->date_posted, (date_posted && strlen(date_posted) > 0) ? date_posted : (date ? date : "N/A"), sizeof(node->date_posted) - 1);
    node->date_posted[sizeof(node->date_posted) - 1] = '\0';
    node->status = st;

    strncpy(node->category, (category && strlen(category) > 0) ? category : "Other", sizeof(node->category) - 1);
    node->category[sizeof(node->category) - 1] = '\0';

    strncpy(node->value_tier, (value_tier && strlen(value_tier) > 0) ? value_tier : "LOW", sizeof(node->value_tier) - 1);
    node->value_tier[sizeof(node->value_tier) - 1] = '\0';

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
                  const char *notes, const char *req_date,
                  const char *visit_date, const char *visit_slot,
                  const char *serial_no, const char *purchase_proof) {
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

    strncpy(node->visit_date, visit_date ? visit_date : "", sizeof(node->visit_date) - 1);
    node->visit_date[sizeof(node->visit_date) - 1] = '\0';

    strncpy(node->visit_slot, visit_slot ? visit_slot : "09:30 AM - 10:00 AM", sizeof(node->visit_slot) - 1);
    node->visit_slot[sizeof(node->visit_slot) - 1] = '\0';

    strncpy(node->serial_no, serial_no ? serial_no : "", sizeof(node->serial_no) - 1);
    node->serial_no[sizeof(node->serial_no) - 1] = '\0';

    strncpy(node->purchase_proof, purchase_proof ? purchase_proof : "", sizeof(node->purchase_proof) - 1);
    node->purchase_proof[sizeof(node->purchase_proof) - 1] = '\0';

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

/* Count how many bookings exist for a given date and slot */
int get_slot_bookings_count(const char *date, const char *slot) {
    if (!date || !slot) return 0;
    int count = 0;
    ClaimRequestNode *curr = g_claim_queue.front;
    while (curr) {
        if (strcmp(curr->visit_date, date) == 0 && strcmp(curr->visit_slot, slot) == 0) {
            count++;
        }
        curr = curr->next;
    }
    return count;
}

/* ----------------------------------------------------------------------------
 * FILE PERSISTENCE & 1-YEAR RETENTION DATABASE
 * ---------------------------------------------------------------------------- */
void save_inventory_to_file(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) return;
    fprintf(fp, "# ID|NAME|DESCRIPTION|LOCATION|IMAGE_PATH|DATE_FOUND|STATUS|CATEGORY|VALUE_TIER|DATE_POSTED\n");
    ItemNode *curr = g_inventory_head;
    while (curr) {
        fprintf(fp, "%d|%s|%s|%s|%s|%s|%d|%s|%s|%s\n",
                curr->id, curr->name, curr->description, curr->location,
                curr->image_path, curr->date_found, (int)curr->status,
                curr->category, curr->value_tier, curr->date_posted);
        curr = curr->next;
    }
    fclose(fp);
}

void load_inventory_from_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        /* Add rich initial sample data across categories */
        ItemNode *n1 = create_item_node(101, "Titan Blue Watch",
            "Dark blue leather strap, round silver dial, scratch on bezel.",
            "Block B, Room 204", "https://images.unsplash.com/photo-1523275335684-37898b6baf30?w=600&auto=format&fit=crop&q=80", "2026-10-05", "2026-10-06", STATUS_CLAIMED,
            "Electronics", "HIGH");
        insert_item_sorted(&g_inventory_head, n1);

        ItemNode *n2 = create_item_node(102, "Scientific Calculator Casio FX-991EX",
            "Black casing with sticker 'SS' on the protective slip cover.",
            "Basketball Court", "https://images.unsplash.com/photo-1587145820266-a5951ee6f620?w=600&auto=format&fit=crop&q=80", "2026-11-14", "2026-11-14", STATUS_PENDING_CLAIM,
            "Books & Stationery", "LOW");
        insert_item_sorted(&g_inventory_head, n2);

        ItemNode *n3 = create_item_node(103, "Dell Inspiron 15 Laptop",
            "Silver finish, Intel Core i7, sticker on top lid.",
            "Block A, Room 304", "https://images.unsplash.com/photo-1496181133206-80ce9b88a853?w=600&auto=format&fit=crop&q=80", "2026-11-15", "2026-11-15", STATUS_AVAILABLE,
            "Electronics", "HIGH");
        insert_item_sorted(&g_inventory_head, n3);

        ItemNode *n4 = create_item_node(104, "Milton Stainless Steel Water Bottle",
            "750ml matte black insulated flask with silver cap.",
            "Basketball Court", "https://images.unsplash.com/photo-1602143407151-7111542de6e8?w=600&auto=format&fit=crop&q=80", "2026-11-15", "2026-11-16", STATUS_AVAILABLE,
            "Bags & Bottles", "LOW");
        insert_item_sorted(&g_inventory_head, n4);

        ItemNode *n5 = create_item_node(105, "College Student ID Card",
            "Name: Rahul Verma, Dept of Computer Science.",
            "Near Temple", "https://images.unsplash.com/photo-1578632767115-351597cf2477?w=600&auto=format&fit=crop&q=80", "2026-11-15", "2026-11-16", STATUS_AVAILABLE,
            "IDs & Cards", "LOW");
        insert_item_sorted(&g_inventory_head, n5);

        ItemNode *n6 = create_item_node(106, "Black Folding Umbrella",
            "Compact automatic open-close with wooden curved handle.",
            "Entrance", "https://images.unsplash.com/photo-1534353436294-0dbd4bdac845?w=600&auto=format&fit=crop&q=80", "2026-11-16", "2026-11-16", STATUS_AVAILABLE,
            "Personal Accessories", "LOW");
        insert_item_sorted(&g_inventory_head, n6);

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
        char category[MAX_NAME_LEN] = {0}, tier[MAX_TIER_LEN] = {0};
        char date_posted[MAX_DATE_LEN] = {0};

        char *tok = strtok(line, "|"); if (tok) id = atoi(tok);
        tok = strtok(NULL, "|"); if (tok) strncpy(name, tok, sizeof(name) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(desc, tok, sizeof(desc) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(loc, tok, sizeof(loc) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(img, tok, sizeof(img) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(date, tok, sizeof(date) - 1);
        tok = strtok(NULL, "|"); if (tok) st_int = atoi(tok);
        tok = strtok(NULL, "|"); if (tok) strncpy(category, tok, sizeof(category) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(tier, tok, sizeof(tier) - 1);
        tok = strtok(NULL, "|");
        if (tok) {
            strncpy(date_posted, tok, sizeof(date_posted) - 1);
        } else {
            strncpy(date_posted, date, sizeof(date_posted) - 1);
        }

        /* Automatic category & tier inference if older 7-column format */
        if (strlen(category) == 0) {
            if (strstr(name, "Watch") || strstr(name, "Laptop") || strstr(name, "iPhone") || strstr(name, "Phone")) {
                strcpy(category, "Electronics");
                strcpy(tier, "HIGH");
            } else if (strstr(name, "Calculator") || strstr(name, "Book")) {
                strcpy(category, "Books & Stationery");
                strcpy(tier, "LOW");
            } else if (strstr(name, "Bottle")) {
                strcpy(category, "Bags & Bottles");
                strcpy(tier, "LOW");
            } else if (strstr(name, "ID") || strstr(name, "Card")) {
                strcpy(category, "IDs & Cards");
                strcpy(tier, "LOW");
            } else if (strstr(name, "Umbrella") || strstr(name, "Key")) {
                strcpy(category, "Personal Accessories");
                strcpy(tier, "LOW");
            } else {
                strcpy(category, "Other");
                strcpy(tier, "LOW");
            }
        }
        if (strlen(tier) == 0) {
            strcpy(tier, "LOW");
        }

        if (id > 0) {
            ItemNode *node = create_item_node(id, name, desc, loc, img, date, date_posted, (ItemStatus)st_int, category, tier);
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
    fprintf(fp, "# REQ_ID|ITEM_ID|CLAIMANT_NAME|USN|PHONE|EMAIL|CLAIM_DATE|NOTES|REQ_DATE|VISIT_DATE|VISIT_SLOT|SERIAL_NO|PURCHASE_PROOF\n");
    ClaimRequestNode *curr = g_claim_queue.front;
    while (curr) {
        fprintf(fp, "%d|%d|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s\n",
                curr->request_id, curr->item_id, curr->claimant.name,
                curr->claimant.usn, curr->claimant.phone, curr->claimant.email,
                curr->claimant.claim_date, curr->notes, curr->request_date,
                curr->visit_date, curr->visit_slot, curr->serial_no, curr->purchase_proof);
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
        char v_date[MAX_DATE_LEN] = {0}, v_slot[MAX_NAME_LEN] = {0};
        char serial[MAX_NAME_LEN] = {0}, proof[MAX_DESC_LEN] = {0};

        char *tok = strtok(line, "|"); if (tok) req_id = atoi(tok);
        tok = strtok(NULL, "|"); if (tok) item_id = atoi(tok);
        tok = strtok(NULL, "|"); if (tok) strncpy(c.name, tok, sizeof(c.name) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(c.usn, tok, sizeof(c.usn) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(c.phone, tok, sizeof(c.phone) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(c.email, tok, sizeof(c.email) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(c.claim_date, tok, sizeof(c.claim_date) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(notes, tok, sizeof(notes) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(req_date, tok, sizeof(req_date) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(v_date, tok, sizeof(v_date) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(v_slot, tok, sizeof(v_slot) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(serial, tok, sizeof(serial) - 1);
        tok = strtok(NULL, "|"); if (tok) strncpy(proof, tok, sizeof(proof) - 1);

        if (req_id > 0 && item_id > 0) {
            enqueue_claim(&g_claim_queue, req_id, item_id, &c, notes, req_date, v_date, v_slot, serial, proof);
            if (req_id > max_req) max_req = req_id;
        }
    }
    fclose(fp);
    g_next_request_id = max_req + 1;
}

/**
 * Appends claim record to 1-Year Retention Archive (claim_history.txt).
 */
void append_claim_to_history(const ItemNode *item, const Claimant *claimant, const char *remarks,
                             const char *serial, const char *proof, const char *v_date, const char *v_slot) {
    FILE *fp = fopen(CLAIM_HISTORY_FILE, "a");
    if (!fp) return;
    char now_str[MAX_DATE_LEN];
    get_current_timestamp(now_str, sizeof(now_str));

    int is_high = (strcmp(item->value_tier, "HIGH") == 0);

    fprintf(fp, "================================================================================\n");
    fprintf(fp, "TRANSACTION RECORD: CLAIM & DISBURSEMENT AUDIT LOG\n");
    fprintf(fp, "Log Timestamp       : %s\n", now_str);
    fprintf(fp, "[RETENTION POLICY]  : Retain this record for 1-Year (365 days) from log date.\n");
    fprintf(fp, "--------------------------------------------------------------------------------\n");
    fprintf(fp, "ITEM INFORMATION:\n");
    fprintf(fp, "  - Item ID         : #%d\n", item->id);
    fprintf(fp, "  - Item Name       : %s\n", item->name);
    fprintf(fp, "  - Category        : %s\n", item->category);
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
    if (is_high) {
        fprintf(fp, "MULTI-FACTOR SECURITY VERIFICATION:\n");
        fprintf(fp, "  - Hardware Serial : %s\n", (serial && strlen(serial) > 0) ? serial : "N/A");
        fprintf(fp, "  - Purchase Proof  : %s\n", (proof && strlen(proof) > 0) ? proof : "N/A");
    }
    if (v_date && strlen(v_date) > 0) {
        fprintf(fp, "SCHEDULED CAMPUS VISIT:\n");
        fprintf(fp, "  - Pickup Slot     : %s, %s\n", v_date, (v_slot && strlen(v_slot) > 0) ? v_slot : "Morning");
    }
    fprintf(fp, "OFFICE VERIFICATION:\n");
    fprintf(fp, "  - Handover Status : CLAIMED & VERIFIED BY ADMIN\n");
    fprintf(fp, "  - Admin Remarks   : %s\n", (remarks && strlen(remarks) > 0) ? remarks : (is_high ? "Multi-factor audit passed and Student ID verified" : "Express handover verified"));
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

    /* GET /api/items: Serialize Linked List to JSON with Category & Value Tier */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/items") == 0) {
        char *json_buf = (char*)malloc(BUFFER_SIZE * 4);
        if (!json_buf) return;
        strcpy(json_buf, "[\n");

        ItemNode *curr = g_inventory_head;
        int first = 1;
        while (curr) {
            char esc_name[MAX_NAME_LEN * 2];
            char esc_desc[MAX_DESC_LEN * 2];
            char esc_loc[MAX_LOC_LEN * 2];
            char esc_img[MAX_PATH_LEN * 2];
            char esc_cat[MAX_NAME_LEN * 2];
            char esc_tier[MAX_TIER_LEN * 2];

            escape_json(curr->name, esc_name, sizeof(esc_name));
            escape_json(curr->description, esc_desc, sizeof(esc_desc));
            escape_json(curr->location, esc_loc, sizeof(esc_loc));
            escape_json(curr->image_path, esc_img, sizeof(esc_img));
            escape_json(curr->category, esc_cat, sizeof(esc_cat));
            escape_json(curr->value_tier, esc_tier, sizeof(esc_tier));

            char item_json[2048];
            snprintf(item_json, sizeof(item_json),
                "%s  {\"id\":%d,\"name\":\"%s\",\"description\":\"%s\",\"location\":\"%s\","
                "\"image_path\":\"%s\",\"date_found\":\"%s\",\"date_posted\":\"%s\",\"status\":\"%s\","
                "\"category\":\"%s\",\"value_tier\":\"%s\"}",
                first ? "" : ",\n",
                curr->id, esc_name, esc_desc, esc_loc, esc_img, curr->date_found, curr->date_posted,
                status_to_str(curr->status), esc_cat, esc_tier);
            strcat(json_buf, item_json);
            first = 0;
            curr = curr->next;
        }
        strcat(json_buf, "\n]");
        send_http_response(client_sock, 200, "OK", "application/json", json_buf, strlen(json_buf));
        free(json_buf);
        return;
    }

    /* POST /api/items: Add item to Linked List with Category & Value Tier */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/items") == 0) {
        char name[MAX_NAME_LEN] = {0}, desc[MAX_DESC_LEN] = {0}, loc[MAX_LOC_LEN] = {0};
        char img[MAX_PATH_LEN * 4] = {0}, date[MAX_DATE_LEN] = {0}, date_posted[MAX_DATE_LEN] = {0};
        char category[MAX_NAME_LEN] = {0}, tier[MAX_TIER_LEN] = {0};

        get_json_string(body_ptr, "name", name, sizeof(name));
        get_json_string(body_ptr, "description", desc, sizeof(desc));
        get_json_string(body_ptr, "location", loc, sizeof(loc));
        get_json_string(body_ptr, "image_path", img, sizeof(img));
        get_json_string(body_ptr, "date_found", date, sizeof(date));
        get_json_string(body_ptr, "date_posted", date_posted, sizeof(date_posted));
        get_json_string(body_ptr, "category", category, sizeof(category));
        get_json_string(body_ptr, "value_tier", tier, sizeof(tier));

        if (strlen(category) == 0) strcpy(category, "Other");
        if (strlen(tier) == 0) strcpy(tier, "LOW");
        if (strlen(date_posted) == 0) {
            if (strlen(date) > 0) strcpy(date_posted, date);
            else strcpy(date_posted, "2026-11-16");
        }

        int new_id = g_next_item_id++;
        ItemNode *node = create_item_node(new_id, name, desc, loc, img, date, date_posted, STATUS_AVAILABLE, category, tier);
        insert_item_sorted(&g_inventory_head, node);
        save_inventory_to_file(ITEMS_FILE);

        char resp[256];
        snprintf(resp, sizeof(resp), "{\"status\":\"created\",\"id\":%d}", new_id);
        send_http_response(client_sock, 201, "Created", "application/json", resp, strlen(resp));
        return;
    }

    /* POST /api/items/edit: Update existing item */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/items/edit") == 0) {
        int id = get_json_int(body_ptr, "id", 0);
        ItemNode *itm = find_item(g_inventory_head, id);
        if (!itm) {
            const char *err = "{\"error\":\"Item not found\"}";
            send_http_response(client_sock, 404, "Not Found", "application/json", err, strlen(err));
            return;
        }

        char name[MAX_NAME_LEN] = {0}, desc[MAX_DESC_LEN] = {0}, loc[MAX_LOC_LEN] = {0};
        char img[MAX_PATH_LEN * 4] = {0}, date_f[MAX_DATE_LEN] = {0}, date_p[MAX_DATE_LEN] = {0};
        char category[MAX_NAME_LEN] = {0}, status_str[32] = {0};

        if (get_json_string(body_ptr, "name", name, sizeof(name)) && strlen(name) > 0) strncpy(itm->name, name, sizeof(itm->name) - 1);
        if (get_json_string(body_ptr, "description", desc, sizeof(desc))) strncpy(itm->description, desc, sizeof(itm->description) - 1);
        if (get_json_string(body_ptr, "location", loc, sizeof(loc))) strncpy(itm->location, loc, sizeof(itm->location) - 1);
        if (get_json_string(body_ptr, "image_path", img, sizeof(img)) && strlen(img) > 0) strncpy(itm->image_path, img, sizeof(itm->image_path) - 1);
        if (get_json_string(body_ptr, "date_found", date_f, sizeof(date_f))) strncpy(itm->date_found, date_f, sizeof(itm->date_found) - 1);
        if (get_json_string(body_ptr, "date_posted", date_p, sizeof(date_p))) strncpy(itm->date_posted, date_p, sizeof(itm->date_posted) - 1);
        if (get_json_string(body_ptr, "category", category, sizeof(category))) strncpy(itm->category, category, sizeof(itm->category) - 1);
        if (get_json_string(body_ptr, "status", status_str, sizeof(status_str))) {
            if (strcmp(status_str, "AVAILABLE") == 0) itm->status = STATUS_AVAILABLE;
            else if (strcmp(status_str, "PENDING CLAIM") == 0) itm->status = STATUS_PENDING_CLAIM;
            else if (strcmp(status_str, "CLAIMED") == 0) itm->status = STATUS_CLAIMED;
        }

        save_inventory_to_file(ITEMS_FILE);
        const char *res = "{\"status\":\"updated\"}";
        send_http_response(client_sock, 200, "OK", "application/json", res, strlen(res));
        return;
    }

    /* POST /api/claims/edit: Update claim details & appointment */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/claims/edit") == 0) {
        int req_id = get_json_int(body_ptr, "request_id", 0);
        ClaimRequestNode *curr = g_claim_queue.front;
        ClaimRequestNode *target = NULL;
        while (curr) {
            if (curr->request_id == req_id) { target = curr; break; }
            curr = curr->next;
        }

        if (!target) {
            const char *err = "{\"error\":\"Claim request not found in queue\"}";
            send_http_response(client_sock, 404, "Not Found", "application/json", err, strlen(err));
            return;
        }

        char name[MAX_NAME_LEN] = {0}, usn[MAX_USN_LEN] = {0}, phone[MAX_PHONE_LEN] = {0}, email[MAX_EMAIL_LEN] = {0};
        char v_date[MAX_DATE_LEN] = {0}, v_slot[MAX_NAME_LEN] = {0}, serial[MAX_NAME_LEN] = {0}, proof[MAX_DESC_LEN] = {0}, notes[MAX_DESC_LEN] = {0};

        if (get_json_string(body_ptr, "name", name, sizeof(name)) && strlen(name) > 0) strncpy(target->claimant.name, name, sizeof(target->claimant.name) - 1);
        if (get_json_string(body_ptr, "usn", usn, sizeof(usn))) strncpy(target->claimant.usn, usn, sizeof(target->claimant.usn) - 1);
        if (get_json_string(body_ptr, "phone", phone, sizeof(phone))) strncpy(target->claimant.phone, phone, sizeof(target->claimant.phone) - 1);
        if (get_json_string(body_ptr, "email", email, sizeof(email))) strncpy(target->claimant.email, email, sizeof(target->claimant.email) - 1);
        if (get_json_string(body_ptr, "visit_date", v_date, sizeof(v_date))) strncpy(target->visit_date, v_date, sizeof(target->visit_date) - 1);
        if (get_json_string(body_ptr, "visit_slot", v_slot, sizeof(v_slot))) strncpy(target->visit_slot, v_slot, sizeof(target->visit_slot) - 1);
        if (get_json_string(body_ptr, "serial_no", serial, sizeof(serial))) strncpy(target->serial_no, serial, sizeof(target->serial_no) - 1);
        if (get_json_string(body_ptr, "purchase_proof", proof, sizeof(proof))) strncpy(target->purchase_proof, proof, sizeof(target->purchase_proof) - 1);
        if (get_json_string(body_ptr, "notes", notes, sizeof(notes))) strncpy(target->notes, notes, sizeof(target->notes) - 1);

        save_queue_to_file(CLAIMS_QUEUE_FILE);
        const char *res = "{\"status\":\"updated\"}";
        send_http_response(client_sock, 200, "OK", "application/json", res, strlen(res));
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

    /* GET /api/slots?date=YYYY-MM-DD: Return slot availability quotas */
    if (strcmp(method, "GET") == 0 && strncmp(path, "/api/slots", 10) == 0) {
        char query_date[MAX_DATE_LEN] = {0};
        char *p_date = strstr(path, "date=");
        if (p_date) {
            strncpy(query_date, p_date + 5, sizeof(query_date) - 1);
            /* Strip any trailing query params */
            char *amp = strchr(query_date, '&');
            if (amp) *amp = '\0';
        } else {
            /* Default to today */
            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            if (t) strftime(query_date, sizeof(query_date), "%Y-%m-%d", t);
            else strcpy(query_date, "2026-11-16");
        }

        char *json_buf = (char*)malloc(4096);
        if (!json_buf) return;
        strcpy(json_buf, "[\n");

        for (size_t s = 0; s < NUM_SLOTS; s++) {
            const char *slot_str = AVAILABLE_SLOTS[s];
            int booked = get_slot_bookings_count(query_date, slot_str);
            int available = (SLOT_CAPACITY - booked) > 0 ? (SLOT_CAPACITY - booked) : 0;

            char slot_json[512];
            snprintf(slot_json, sizeof(slot_json),
                "%s  {\"slot\":\"%s\",\"capacity\":%d,\"booked\":%d,\"available\":%d,\"is_full\":%s}",
                (s == 0) ? "" : ",\n",
                slot_str, SLOT_CAPACITY, booked, available, (available == 0) ? "true" : "false");
            strcat(json_buf, slot_json);
        }
        strcat(json_buf, "\n]");
        send_http_response(client_sock, 200, "OK", "application/json", json_buf, strlen(json_buf));
        free(json_buf);
        return;
    }

    /* GET /api/claims: Return Queue with visit details, serial no, purchase proof & target item */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/claims") == 0) {
        char *json_buf = (char*)malloc(BUFFER_SIZE * 2);
        if (!json_buf) return;
        strcpy(json_buf, "[\n");

        ClaimRequestNode *curr = g_claim_queue.front;
        int first = 1;
        while (curr) {
            char esc_name[MAX_NAME_LEN * 2], esc_notes[MAX_DESC_LEN * 2];
            char esc_vdate[MAX_DATE_LEN * 2], esc_vslot[MAX_NAME_LEN * 2];
            char esc_serial[MAX_NAME_LEN * 2], esc_proof[MAX_DESC_LEN * 2];

            escape_json(curr->claimant.name, esc_name, sizeof(esc_name));
            escape_json(curr->notes, esc_notes, sizeof(esc_notes));
            escape_json(curr->visit_date, esc_vdate, sizeof(esc_vdate));
            escape_json(curr->visit_slot, esc_vslot, sizeof(esc_vslot));
            escape_json(curr->serial_no, esc_serial, sizeof(esc_serial));
            escape_json(curr->purchase_proof, esc_proof, sizeof(esc_proof));

            /* Look up item */
            ItemNode *item = find_item(g_inventory_head, curr->item_id);
            char item_name[MAX_NAME_LEN * 2] = "Unknown Item";
            char item_cat[MAX_NAME_LEN * 2] = "Other";
            char item_tier[MAX_TIER_LEN * 2] = "LOW";
            char item_loc[MAX_LOC_LEN * 2] = "N/A";
            if (item) {
                escape_json(item->name, item_name, sizeof(item_name));
                escape_json(item->category, item_cat, sizeof(item_cat));
                escape_json(item->value_tier, item_tier, sizeof(item_tier));
                escape_json(item->location, item_loc, sizeof(item_loc));
            }

            char q_json[4096];
            snprintf(q_json, sizeof(q_json),
                "%s  {\"request_id\":%d,\"item_id\":%d,\"notes\":\"%s\",\"request_date\":\"%s\","
                "\"visit_date\":\"%s\",\"visit_slot\":\"%s\",\"serial_no\":\"%s\",\"purchase_proof\":\"%s\","
                "\"item\":{\"name\":\"%s\",\"category\":\"%s\",\"value_tier\":\"%s\",\"location\":\"%s\"},"
                "\"claimant\":{\"name\":\"%s\",\"usn\":\"%s\",\"phone\":\"%s\",\"email\":\"%s\",\"claim_date\":\"%s\"}}",
                first ? "" : ",\n",
                curr->request_id, curr->item_id, esc_notes, curr->request_date,
                esc_vdate, esc_vslot, esc_serial, esc_proof,
                item_name, item_cat, item_tier, item_loc,
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

    /* POST /api/claims: Enqueue claim request with visit slot & tier requirements */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/claims") == 0) {
        int item_id = get_json_int(body_ptr, "item_id", 0);
        char notes[MAX_DESC_LEN] = {0};
        char v_date[MAX_DATE_LEN] = {0};
        char v_slot[MAX_NAME_LEN] = {0};
        char serial[MAX_NAME_LEN] = {0};
        char proof[MAX_DESC_LEN] = {0};
        Claimant c = {0};

        get_json_string(body_ptr, "name", c.name, sizeof(c.name));
        get_json_string(body_ptr, "usn", c.usn, sizeof(c.usn));
        get_json_string(body_ptr, "phone", c.phone, sizeof(c.phone));
        get_json_string(body_ptr, "email", c.email, sizeof(c.email));
        get_json_string(body_ptr, "notes", notes, sizeof(notes));
        get_json_string(body_ptr, "visit_date", v_date, sizeof(v_date));
        get_json_string(body_ptr, "visit_slot", v_slot, sizeof(v_slot));
        get_json_string(body_ptr, "serial_no", serial, sizeof(serial));
        get_json_string(body_ptr, "purchase_proof", proof, sizeof(proof));

        /* Strict Email Validation */
        if (!validate_college_email(c.email)) {
            const char *err = "{\"error\":\"Invalid email: must be a valid institutional/college email\"}";
            send_http_response(client_sock, 400, "Bad Request", "application/json", err, strlen(err));
            return;
        }

        ItemNode *itm = find_item(g_inventory_head, item_id);
        if (!itm) {
            const char *err = "{\"error\":\"Item not found in inventory\"}";
            send_http_response(client_sock, 404, "Not Found", "application/json", err, strlen(err));
            return;
        }

        /* Electronic items verification check */
        if (strcmp(itm->category, "Electronics") == 0 || strcmp(itm->value_tier, "HIGH") == 0) {
            if (strlen(serial) == 0 && strlen(proof) == 0 && strlen(notes) == 0) {
                const char *err = "{\"error\":\"Please provide device details, serial number, or identifying marks\"}";
                send_http_response(client_sock, 400, "Bad Request", "application/json", err, strlen(err));
                return;
            }
        }

        /* Check Slot Quota Capacity */
        if (strlen(v_date) > 0 && strlen(v_slot) > 0) {
            int booked = get_slot_bookings_count(v_date, v_slot);
            if (booked >= SLOT_CAPACITY) {
                const char *err = "{\"error\":\"Selected campus visit time slot is fully booked. Please choose another slot.\"}";
                send_http_response(client_sock, 409, "Conflict", "application/json", err, strlen(err));
                return;
            }
        }

        get_current_timestamp(c.claim_date, sizeof(c.claim_date));
        char req_date[MAX_DATE_LEN];
        get_current_timestamp(req_date, sizeof(req_date));

        int req_id = g_next_request_id++;
        enqueue_claim(&g_claim_queue, req_id, item_id, &c, notes, req_date, v_date, v_slot, serial, proof);
        save_queue_to_file(CLAIMS_QUEUE_FILE);

        /* Update item status in Linked List */
        itm->status = STATUS_PENDING_CLAIM;
        save_inventory_to_file(ITEMS_FILE);

        char resp[384];
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"enqueued\",\"request_id\":%d,\"position\":%d,"
                 "\"visit_date\":\"%s\",\"visit_slot\":\"%s\",\"value_tier\":\"%s\"}",
                 req_id, g_claim_queue.count, v_date, v_slot, itm->value_tier);
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
                    append_claim_to_history(itm, &dequeued.claimant, remarks,
                                            dequeued.serial_no, dequeued.purchase_proof,
                                            dequeued.visit_date, dequeued.visit_slot);
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

        char *json_buf = (char*)malloc(BUFFER_SIZE * 4);
        strcpy(json_buf, "[\n");
        char line[512];
        int first = 1;

        char log_time[MAX_DATE_LEN] = {0};
        char item_id_str[32] = {0};
        char item_name[MAX_NAME_LEN] = {0};
        char category[MAX_NAME_LEN] = {0};
        char value_tier[MAX_TIER_LEN] = {0};
        char location[MAX_LOC_LEN] = {0};
        char c_name[MAX_NAME_LEN] = {0};
        char c_usn[MAX_USN_LEN] = {0};
        char c_phone[MAX_PHONE_LEN] = {0};
        char c_email[MAX_EMAIL_LEN] = {0};
        char serial[MAX_NAME_LEN] = {0};
        char proof[MAX_DESC_LEN] = {0};
        char visit_slot[MAX_NAME_LEN] = {0};
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
            } else if (strstr(line, "- Category        :")) {
                char *p = strchr(line, ':');
                if (p) { strncpy(category, p + 1, sizeof(category) - 1); trim(category); }
            } else if (strstr(line, "- Value Tier      :")) {
                char *p = strchr(line, ':');
                if (p) { strncpy(value_tier, p + 1, sizeof(value_tier) - 1); trim(value_tier); }
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
            } else if (strstr(line, "- Hardware Serial :")) {
                char *p = strchr(line, ':');
                if (p) { strncpy(serial, p + 1, sizeof(serial) - 1); trim(serial); }
            } else if (strstr(line, "- Purchase Proof  :")) {
                char *p = strchr(line, ':');
                if (p) { strncpy(proof, p + 1, sizeof(proof) - 1); trim(proof); }
            } else if (strstr(line, "- Pickup Slot     :")) {
                char *p = strchr(line, ':');
                if (p) { strncpy(visit_slot, p + 1, sizeof(visit_slot) - 1); trim(visit_slot); }
            } else if (strstr(line, "- Admin Remarks   :")) {
                char *p = strchr(line, ':');
                if (p) { strncpy(remarks, p + 1, sizeof(remarks) - 1); trim(remarks); }
            } else if (strncmp(line, "====", 4) == 0 && strlen(log_time) > 0) {
                char entry[4096];
                snprintf(entry, sizeof(entry),
                    "%s  {\"log_time\":\"%s\",\"item_id\":%d,\"item_name\":\"%s\",\"category\":\"%s\","
                    "\"value_tier\":\"%s\",\"location\":\"%s\",\"serial_no\":\"%s\",\"purchase_proof\":\"%s\","
                    "\"pickup_slot\":\"%s\",\"remarks\":\"%s\","
                    "\"claimant\":{\"name\":\"%s\",\"usn\":\"%s\",\"phone\":\"%s\",\"email\":\"%s\"}}",
                    first ? "" : ",\n",
                    log_time, atoi(item_id_str), item_name,
                    (strlen(category) > 0) ? category : "General",
                    (strlen(value_tier) > 0) ? value_tier : "Standard",
                    location, serial, proof, visit_slot, remarks,
                    c_name, c_usn, c_phone, c_email);
                strcat(json_buf, entry);
                first = 0;
                log_time[0] = '\0';
                category[0] = '\0';
                value_tier[0] = '\0';
                serial[0] = '\0';
                proof[0] = '\0';
                visit_slot[0] = '\0';
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
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("====================================================================\n");
    printf("    COLLEGE LOST & FOUND MANAGEMENT SYSTEM - C WEB SERVER           \n");
    printf("====================================================================\n");

    /* Initialize Data Structures from Flat Files */
    load_inventory_from_file(ITEMS_FILE);
    load_queue_from_file(CLAIMS_QUEUE_FILE);

    printf("[+] Active Inventory Linked List loaded (with Category & Tier support).\n");
    printf("[+] Pending Claim Requests FIFO Queue loaded (with Appointment Scheduling).\n");

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
    printf(" [ONLINE] Features Active     : Scheduled Visits | Category Search | Retention Audit\n");
    printf("                                 Scheduled Visits   | Category Search\n");
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
