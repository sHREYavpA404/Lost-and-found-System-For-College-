/**
 * ============================================================================
 * PROJECT: College Lost and Found Management System
 * LANGUAGE: Pure C (C99 Standard compliant)
 * PLATFORM: Cross-platform (Windows & Linux/macOS compatible)
 * ============================================================================
 *
 * CORE DATA STRUCTURES:
 * 1. LINKED LIST:
 *    - Dynamically stores and manages the inventory of lost & found items.
 *    - Allows O(1) insertions at head/tail, dynamic memory expansion without
 *      fixed array bounds, and easy traversal / search / deletion.
 *
 * 2. QUEUE (FIFO - First In, First Out):
 *    - Implemented as a singly-linked FIFO Queue (front and rear pointers).
 *    - Manages pending claim requests submitted by students or front-desk staff.
 *    - Guarantees fair, first-come-first-served processing of claims.
 *
 * DATA PERSISTENCE & 1-YEAR RETENTION DATABASE:
 * - Active Items File: "items.txt"
 *   Stores active and pending items in structured delimited format.
 * - Claim History Database: "claim_history.txt"
 *   Acts as the official 1-Year Retention Archive for college audit compliance.
 *   Every time an item is claimed and handed over, full claimant details
 *   (Name, USN, Phone, College Email) and item details are permanently logged
 *   with UTC/Local timestamps for statutory audit and dispute resolution.
 * - Queue Backup File: "claims_queue.txt"
 *   Ensures pending claims are not lost across application restarts.
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
#endif
/* Configuration Constants */
#define DEFAULT_ADMIN_PASSWORD "admin123"
#define COLLEGE_EMAIL_DOMAIN   "@college.edu"
#define ITEMS_FILE             "items.txt"
#define CLAIMS_QUEUE_FILE      "claims_queue.txt"
#define CLAIM_HISTORY_FILE     "claim_history.txt"

#define MAX_NAME_LEN        64
#define MAX_DESC_LEN        256
#define MAX_LOC_LEN         128
#define MAX_PATH_LEN        260
#define MAX_DATE_LEN        32
#define MAX_USN_LEN         32
#define MAX_PHONE_LEN       20
#define MAX_EMAIL_LEN       80
#define MAX_PASS_LEN        32
#define BUFFER_SIZE         512

/* Status Enum for Items */
typedef enum {
    STATUS_AVAILABLE = 0,    /* Item is in storage, available to be claimed */
    STATUS_PENDING_CLAIM = 1,/* A claim request is queued and awaiting verification */
    STATUS_CLAIMED = 2       /* Item has been verified and handed over */
} ItemStatus;

/* Convert ItemStatus to String representation */
const char* status_to_string(ItemStatus status) {
    switch (status) {
        case STATUS_AVAILABLE:     return "AVAILABLE";
        case STATUS_PENDING_CLAIM: return "PENDING CLAIM";
        case STATUS_CLAIMED:       return "CLAIMED";
        default:                   return "UNKNOWN";
    }
}

/* ----------------------------------------------------------------------------
 * DATA STRUCTURE: Item Node (Linked List for Inventory)
 * ---------------------------------------------------------------------------- */
typedef struct ItemNode {
    int id;                         /* Unique Item Identification Number */
    char name[MAX_NAME_LEN];        /* Name of the item */
    char description[MAX_DESC_LEN]; /* Description / color / distinguishing marks */
    char location[MAX_LOC_LEN];     /* Location found (Block + Room or Area) */
    char image_path[MAX_PATH_LEN];  /* File path to item picture */
    char date_found[MAX_DATE_LEN];  /* Date found (YYYY-MM-DD) */
    ItemStatus status;              /* Current availability status */
    struct ItemNode *next;          /* Pointer to the next item node */
} ItemNode;

/* ----------------------------------------------------------------------------
 * DATA STRUCTURE: Claimant Information
 * ---------------------------------------------------------------------------- */
typedef struct {
    char name[MAX_NAME_LEN];        /* Claimant's full name */
    char usn[MAX_USN_LEN];          /* University Seat Number (Student ID) */
    char phone[MAX_PHONE_LEN];      /* Contact telephone number */
    char email[MAX_EMAIL_LEN];      /* College-issued email address */
    char claim_date[MAX_DATE_LEN];  /* Date & time claim was logged */
} Claimant;

/* ----------------------------------------------------------------------------
 * DATA STRUCTURE: Claim Request Node & Queue (FIFO Waiting List)
 * ---------------------------------------------------------------------------- */
typedef struct ClaimRequestNode {
    int request_id;                 /* Unique claim request ID */
    int item_id;                    /* Target item ID */
    Claimant claimant;              /* Claimant personal details */
    char notes[MAX_DESC_LEN];       /* Proof of ownership / description provided by claimant */
    char request_date[MAX_DATE_LEN];/* Timestamp of request submission */
    struct ClaimRequestNode *next;  /* Pointer to next request in queue */
} ClaimRequestNode;

typedef struct {
    ClaimRequestNode *front;        /* Head of queue (processed next) */
    ClaimRequestNode *rear;         /* Tail of queue (new requests added here) */
    int count;                      /* Number of pending requests in queue */
} ClaimQueue;
/* Global state variables */
static ItemNode *g_inventory_head = NULL;
static ClaimQueue g_claim_queue = { NULL, NULL, 0 };
static char g_admin_password[MAX_PASS_LEN] = DEFAULT_ADMIN_PASSWORD;
static int g_next_item_id = 101;
static int g_next_request_id = 1001;

/* ----------------------------------------------------------------------------
 * FUNCTION DECLARATIONS (Modular Architecture)
 * ---------------------------------------------------------------------------- */

/* Utility and Input Helpers */
void get_current_date_str(char *dest, size_t max_size);
void get_current_timestamp_str(char *dest, size_t max_size);
void trim_whitespace(char *str);
int  read_line(char *dest, size_t max_len);
int  read_int_range(int min_val, int max_val);
void read_masked_password(char *dest, size_t max_len);
void clear_screen(void);
void pause_console(void);

/* Validation Helpers */
int validate_college_email(const char *email, const char *required_domain);
int validate_phone(const char *phone);
int validate_usn(const char *usn);
void prompt_location(char *dest, size_t max_len);
/* Linked List Operations (Item Inventory) */
ItemNode* create_item_node(int id, const char *name, const char *desc,
                           const char *loc, const char *img, const char *date, ItemStatus st);
void insert_item_sorted(ItemNode **head_ref, ItemNode *new_node);
ItemNode* find_item_by_id(ItemNode *head, int id);
int delete_item_by_id(ItemNode **head_ref, int id);
void display_all_items(const ItemNode *head, int only_available);
void display_item_detailed(const ItemNode *item);
void search_items_by_keyword(const ItemNode *head, const char *keyword);
void filter_items_by_location(const ItemNode *head, const char *loc_query);
void free_inventory(ItemNode **head_ref);
/* Queue Operations (Claim Requests Waiting List) */
void queue_init(ClaimQueue *q);
int queue_enqueue(ClaimQueue *q, int req_id, int item_id, const Claimant *claimant,
                  const char *notes, const char *req_date);
int queue_dequeue(ClaimQueue *q, ClaimRequestNode *out_node);
void queue_display(const ClaimQueue *q);
ClaimRequestNode* queue_find_by_req_id(const ClaimQueue *q, int req_id);
void queue_free(ClaimQueue *q);

/* File I/O & 1-Year Retention Database Operations */
void load_inventory_from_file(const char *filename);
void save_inventory_to_file(const char *filename);
void load_queue_from_file(const char *filename);
void save_queue_to_file(const char *filename);
void append_claim_to_history(const ItemNode *item, const Claimant *claimant,
                             const char *action_notes, const char *filename);
void display_claim_history_file(const char *filename);

/* Sub-system Modules */
void add_item(void);
void process_claim_from_queue(void);
void direct_claim_walkin(void);
void student_submit_claim_request(void);
void student_check_claim_status(void);

/* User Interfaces */
void admin_menu(void);
void student_menu(void);
void main_menu(void);
void display_about_and_retention(void);

/* ============================================================================
 * IMPLEMENTATION: Utility & Input Helpers
 * ============================================================================ */

void get_current_date_str(char *dest, size_t max_size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t != NULL) {
        strftime(dest, max_size, "%Y-%m-%d", t);
    } else {
        snprintf(dest, max_size, "2026-01-01");
    }
}

void get_current_timestamp_str(char *dest, size_t max_size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t != NULL) {
        strftime(dest, max_size, "%Y-%m-%d %H:%M:%S", t);
    } else {
        snprintf(dest, max_size, "2026-01-01 00:00:00");
    }
}

void trim_whitespace(char *str) {
    if (!str) return;
    /* Trim trailing */
    int len = (int)strlen(str);
    while (len > 0 && (isspace((unsigned char)str[len - 1]) || str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
    /* Trim leading */
    int start = 0;
    while (str[start] && isspace((unsigned char)str[start])) {
        start++;
    }
    if (start > 0) {
        memmove(str, str + start, len - start + 1);
    }
}

int read_line(char *dest, size_t max_len) {
    if (!dest || max_len == 0) return 0;
    if (fgets(dest, (int)max_len, stdin) == NULL) {
        dest[0] = '\0';
        return 0;
    }
    trim_whitespace(dest);
    return (int)strlen(dest);
}

int read_int_range(int min_val, int max_val) {
    char buffer[128];
    int val = 0;
    while (1) {
        printf("Enter selection (%d - %d): ", min_val, max_val);
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            continue;
        }
        trim_whitespace(buffer);
        if (strlen(buffer) == 0) continue;

        char *endptr;
        val = (int)strtol(buffer, &endptr, 10);
        if (endptr != buffer && *endptr == '\0' && val >= min_val && val <= max_val) {
            return val;
        }
        printf("[!] Invalid input. Please enter a whole number between %d and %d.\n", min_val, max_val);
    }
}

void read_masked_password(char *dest, size_t max_len) {
    size_t idx = 0;
    dest[0] = '\0';

#ifdef _WIN32
    if (!_isatty(_fileno(stdin))) {
        if (fgets(dest, (int)max_len, stdin) != NULL) {
            trim_whitespace(dest);
        }
        return;
    }

    int ch;
    while (1) {
        ch = _getch();
        if (ch == '\r' || ch == '\n') {
            break;
        } else if (ch == '\b') { /* Backspace */
            if (idx > 0) {
                idx--;
                printf("\b \b");
            }
        } else if (ch == 0 || ch == 224) { /* Extended keys */
            _getch(); /* Ignore next code */
        } else if (idx < max_len - 1 && isprint(ch)) {
            dest[idx++] = (char)ch;
            printf("*");
        }
    }
    dest[idx] = '\0';
    printf("\n");
#else
    if (!isatty(STDIN_FILENO)) {
        if (fgets(dest, max_len, stdin) != NULL) {
            trim_whitespace(dest);
        }
        return;
    }
 struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    if (fgets(dest, max_len, stdin) != NULL) {
        trim_whitespace(dest);
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
#endif
}

void clear_screen(void) {
#ifdef _WIN32
    if (_isatty(_fileno(stdout))) system("cls");
#else
    if (isatty(STDOUT_FILENO)) system("clear");
#endif
}

void pause_console(void) {
#ifdef _WIN32
    if (!_isatty(_fileno(stdin))) return;
#else
    if (!isatty(STDIN_FILENO)) return;
#endif
    printf("\nPress Enter to continue...");
    char ch;
    while ((ch = (char)getchar()) != '\n' && ch != EOF);
}

/* ============================================================================
 * IMPLEMENTATION: Validation Helpers
 * ============================================================================ */

/**
 * Validates whether an email address is properly formatted and ends with
 * the required college domain (e.g., @college.edu).
 */
int validate_college_email(const char *email, const char *required_domain) {
    if (!email || !required_domain) return 0;
    size_t email_len = strlen(email);
    size_t domain_len = strlen(required_domain);

    if (email_len <= domain_len) return 0;

    /* Must not start with a dot or special char */
    if (!isalnum((unsigned char)email[0])) return 0;

    /* Find the '@' symbol */
    const char *at_pos = strchr(email, '@');
    if (!at_pos || at_pos == email) return 0;

    /* Ensure only one '@' symbol */
    if (strchr(at_pos + 1, '@') != NULL) return 0;

    /* Verify local part before '@' has valid characters */
    for (const char *p = email; p < at_pos; p++) {
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-') {
            return 0;
        }
    }

    /* Check if the suffix ends with required_domain (case-insensitive) */
    const char *suffix = email + (email_len - domain_len);
    for (size_t i = 0; i < domain_len; i++) {
        if (tolower((unsigned char)suffix[i]) != tolower((unsigned char)required_domain[i])) {
            return 0;
        }
    }

    return 1;
}

int validate_phone(const char *phone) {
    if (!phone) return 0;
    size_t len = strlen(phone);
    if (len < 10 || len > 15) return 0;

    size_t start = 0;
    if (phone[0] == '+') start = 1;

    int digit_count = 0;
    for (size_t i = start; i < len; i++) {
        if (isdigit((unsigned char)phone[i])) {
            digit_count++;
        } else if (phone[i] != '-' && phone[i] != ' ') {
            return 0;
        }
    }
    return (digit_count >= 10);
}

int validate_usn(const char *usn) {
    if (!usn) return 0;
    size_t len = strlen(usn);
    if (len < 5 || len > 20) return 0;

    for (size_t i = 0; i < len; i++) {
        if (!isalnum((unsigned char)usn[i])) return 0;
    }
    return 1;
}

/**
 * Prompt location with sub-menu for Blocks A-E (with room number) or landmarks.
 */
void prompt_location(char *dest, size_t max_len) {
    printf("\n----------------------------------------------------\n");
    printf(" SELECT LOCATION WHERE ITEM WAS FOUND:\n");
    printf("----------------------------------------------------\n");
    printf(" 1. Block A (Prompts for Room Number)\n");
    printf(" 2. Block B (Prompts for Room Number)\n");
    printf(" 3. Block C (Prompts for Room Number)\n");
    printf(" 4. Block D (Prompts for Room Number)\n");
    printf(" 5. Block E (Prompts for Room Number)\n");
    printf(" 6. Basketball Court\n");
    printf(" 7. Near Temple\n");
    printf(" 8. Entrance\n");
    printf("----------------------------------------------------\n");

    int choice = read_int_range(1, 8);
    char room[64];

    if (choice >= 1 && choice <= 5) {
        char block_char = (char)('A' + (choice - 1));
        while (1) {
            printf("Enter Room / Lab Number for Block %c (e.g. 101, 304, Lab 2): ", block_char);
            if (read_line(room, sizeof(room)) > 0) {
                break;
            }
            printf("[!] Room number cannot be empty.\n");
        }
        snprintf(dest, max_len, "Block %c, Room %s", block_char, room);
    } else if (choice == 6) {
        snprintf(dest, max_len, "Basketball Court");
    } else if (choice == 7) {
        snprintf(dest, max_len, "Near Temple");
    } else if (choice == 8) {
        snprintf(dest, max_len, "Entrance");
}

/* ============================================================================
 * IMPLEMENTATION: Linked List Operations (Item Inventory)
 * ============================================================================ */

ItemNode* create_item_node(int id, const char *name, const char *desc,
                           const char *loc, const char *img, const char *date, ItemStatus st) {
    ItemNode *node = (ItemNode*)malloc(sizeof(ItemNode));
    if (!node) {
        fprintf(stderr, "[ERROR] Memory allocation failed for ItemNode!\n");
        exit(EXIT_FAILURE);
    }
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

/**
 * Inserts an item into the linked list sorted ascending by Item ID.
 */
void insert_item_sorted(ItemNode **head_ref, ItemNode *new_node) {
    if (!head_ref || !new_node) return;

    if (*head_ref == NULL || (*head_ref)->id >= new_node->id) {
        new_node->next = *head_ref;
        *head_ref = new_node;
        return;
    }

    ItemNode *current = *head_ref;
    while (current->next != NULL && current->next->id < new_node->id) {
        current = current->next;
    }
    new_node->next = current->next;
    current->next = new_node;
}

ItemNode* find_item_by_id(ItemNode *head, int id) {
    ItemNode *curr = head;
    while (curr) {
        if (curr->id == id) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

int delete_item_by_id(ItemNode **head_ref, int id) {
    if (!head_ref || !*head_ref) return 0;

    ItemNode *curr = *head_ref;
    ItemNode *prev = NULL;

    while (curr != NULL && curr->id != id) {
        prev = curr;
        curr = curr->next;
    }

    if (!curr) return 0; /* Not found */

    if (!prev) {
        *head_ref = curr->next;
    } else {
        prev->next = curr->next;
    }

    free(curr);
    return 1;
}

void display_all_items(const ItemNode *head, int only_available) {
    if (!head) {
        printf("\n[!] The lost and found inventory is currently empty.\n");
        return;
    }

    printf("\n%-6s | %-20s | %-22s | %-12s | %-14s\n",
           "ID", "NAME", "LOCATION", "DATE FOUND", "STATUS");
    printf("------------------------------------------------------------------------------------\n");

    int count = 0;
    const ItemNode *curr = head;
    while (curr) {
        if (!only_available || curr->status == STATUS_AVAILABLE) {
            printf("#%-5d | %-20.20s | %-22.22s | %-12.12s | %-14s\n",
                   curr->id, curr->name, curr->location, curr->date_found,
                   status_to_string(curr->status));
            count++;
        }
        curr = curr->next;
    }

    printf("------------------------------------------------------------------------------------\n");
    printf("Total items displayed: %d\n", count);
}

void display_item_detailed(const ItemNode *item) {
    if (!item) return;
    printf("\n====================================================\n");
    printf("               ITEM DETAILS (ID: #%d)               \n", item->id);
    printf("====================================================\n");
    printf(" Name        : %s\n", item->name);
    printf(" Description : %s\n", item->description);
    printf(" Location    : %s\n", item->location);
    printf(" Date Found  : %s\n", item->date_found);
    printf(" Image Path  : %s\n", item->image_path);
    printf(" Status      : %s\n", status_to_string(item->status));
    printf("====================================================\n");
}

void search_items_by_keyword(const ItemNode *head, const char *keyword) {
    if (!head || !keyword || strlen(keyword) == 0) {
        printf("\n[!] No search term provided.\n");
        return;
    }

    char term_lower[128];
    strncpy(term_lower, keyword, sizeof(term_lower) - 1);
    term_lower[sizeof(term_lower) - 1] = '\0';
    for (size_t i = 0; term_lower[i]; i++) term_lower[i] = (char)tolower((unsigned char)term_lower[i]);

    printf("\n=== SEARCH RESULTS FOR: '%s' ===\n", keyword);
    printf("%-6s | %-20s | %-22s | %-12s | %-14s\n",
           "ID", "NAME", "LOCATION", "DATE FOUND", "STATUS");
    printf("------------------------------------------------------------------------------------\n");

    int found_count = 0;
    const ItemNode *curr = head;
    while (curr) {
        char name_l[MAX_NAME_LEN], desc_l[MAX_DESC_LEN], loc_l[MAX_LOC_LEN];
        strncpy(name_l, curr->name, sizeof(name_l) - 1); name_l[sizeof(name_l) - 1] = '\0';
        strncpy(desc_l, curr->description, sizeof(desc_l) - 1); desc_l[sizeof(desc_l) - 1] = '\0';
        strncpy(loc_l, curr->location, sizeof(loc_l) - 1); loc_l[sizeof(loc_l) - 1] = '\0';

        for (size_t i = 0; name_l[i]; i++) name_l[i] = (char)tolower((unsigned char)name_l[i]);
        for (size_t i = 0; desc_l[i]; i++) desc_l[i] = (char)tolower((unsigned char)desc_l[i]);
        for (size_t i = 0; loc_l[i]; i++) loc_l[i] = (char)tolower((unsigned char)loc_l[i]);

        if (strstr(name_l, term_lower) || strstr(desc_l, term_lower) || strstr(loc_l, term_lower)) {
            printf("#%-5d | %-20.20s | %-22.22s | %-12.12s | %-14s\n",
                   curr->id, curr->name, curr->location, curr->date_found,
                   status_to_string(curr->status));
            found_count++;
        }
        curr = curr->next;
    }

    printf("------------------------------------------------------------------------------------\n");
    printf("Found %d matching item(s).\n", found_count);
}


void filter_items_by_location(const ItemNode *head, const char *loc_query) {
    if (!head || !loc_query) return;

    printf("\n=== ITEMS FILTERED BY: '%s' ===\n", loc_query);
    printf("%-6s | %-20s | %-22s | %-12s | %-14s\n",
           "ID", "NAME", "LOCATION", "DATE FOUND", "STATUS");
    printf("------------------------------------------------------------------------------------\n");

    int count = 0;
    const ItemNode *curr = head;
    while (curr) {
        if (strstr(curr->location, loc_query) != NULL) {
            printf("#%-5d | %-20.20s | %-22.22s | %-12.12s | %-14s\n",
                   curr->id, curr->name, curr->location, curr->date_found,
                   status_to_string(curr->status));
            count++;
        }
        curr = curr->next;
    }

    printf("------------------------------------------------------------------------------------\n");
    printf("Matches in '%s': %d\n", loc_query, count);
}

void free_inventory(ItemNode **head_ref) {
    if (!head_ref) return;
    ItemNode *curr = *head_ref;
    while (curr) {
        ItemNode *temp = curr;
        curr = curr->next;
        free(temp);
    }
    *head_ref = NULL;
}

/* ============================================================================
 * IMPLEMENTATION: Queue Operations (Claim Requests Waiting List)
 * ============================================================================ */

void queue_init(ClaimQueue *q) {
    if (!q) return;
    q->front = NULL;
    q->rear = NULL;
    q->count = 0;
}

int queue_enqueue(ClaimQueue *q, int req_id, int item_id, const Claimant *claimant,
                  const char *notes, const char *req_date) {
    if (!q || !claimant) return 0;

    ClaimRequestNode *node = (ClaimRequestNode*)malloc(sizeof(ClaimRequestNode));
    if (!node) {
        fprintf(stderr, "[ERROR] Memory allocation failed for ClaimRequestNode!\n");
        return 0;
    }

    node->request_id = req_id;
    node->item_id = item_id;
    node->claimant = *claimant;

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

int queue_dequeue(ClaimQueue *q, ClaimRequestNode *out_node) {
    if (!q || q->front == NULL) return 0;

    ClaimRequestNode *temp = q->front;
    if (out_node) {
        *out_node = *temp;
        out_node->next = NULL;
    }

    q->front = q->front->next;
    if (q->front == NULL) {
        q->rear = NULL;
    }
    q->count--;

    free(temp);
    return 1;
}

void queue_display(const ClaimQueue *q) {
    if (!q || q->count == 0) {
        printf("\n[*] The Claim Requests Waiting Queue is currently empty. No pending claims.\n");
        return;
    }

    printf("\n================================================================================\n");
    printf("             PENDING CLAIM REQUESTS QUEUE (FIFO WAITING LIST - %d TOTAL)         \n", q->count);
    printf("================================================================================\n");
    printf("%-8s | %-8s | %-18s | %-12s | %-20s\n",
           "REQ ID", "ITEM ID", "CLAIMANT NAME", "USN", "REQUEST DATE");
    printf("--------------------------------------------------------------------------------\n");

    const ClaimRequestNode *curr = q->front;
    int pos = 1;
    while (curr) {
        printf("#%-7d | #%-7d | %-18.18s | %-12.12s | %-20.20s (Pos: %d)\n",
               curr->request_id, curr->item_id, curr->claimant.name,
               curr->claimant.usn, curr->request_date, pos++);
        curr = curr->next;
    }
    printf("================================================================================\n");
}

ClaimRequestNode* queue_find_by_req_id(const ClaimQueue *q, int req_id) {
    if (!q) return NULL;
    ClaimRequestNode *curr = q->front;
    while (curr) {
        if (curr->request_id == req_id) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

void queue_free(ClaimQueue *q) {
    if (!q) return;
    ClaimRequestNode *curr = q->front;
    while (curr) {
        ClaimRequestNode *temp = curr;
        curr = curr->next;
        free(temp);
    }
    q->front = NULL;
    q->rear = NULL;
    q->count = 0;
}

/* ============================================================================
 * IMPLEMENTATION: File I/O & 1-Year Retention Database Operations
 * ============================================================================ */

/**
 * Loads inventory from ITEMS_FILE.
 * File format per line:
 * ID|NAME|DESCRIPTION|LOCATION|IMAGE_PATH|DATE_FOUND|STATUS
 */
void load_inventory_from_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        /* File doesn't exist yet; initial run will create it */
        return;
    }

    free_inventory(&g_inventory_head);
    char line[BUFFER_SIZE * 2];
    int max_id = 100;

    while (fgets(line, sizeof(line), fp)) {
        trim_whitespace(line);
        if (strlen(line) == 0 || line[0] == '#') continue;

        int id = 0, st_int = 0;
        char name[MAX_NAME_LEN] = {0};
        char desc[MAX_DESC_LEN] = {0};
        char loc[MAX_LOC_LEN] = {0};
        char img[MAX_PATH_LEN] = {0};
        char date[MAX_DATE_LEN] = {0};

        /* Parse pipe-delimited values safely */
        char *token = strtok(line, "|");
        if (token) id = atoi(token);

        token = strtok(NULL, "|");
        if (token) strncpy(name, token, sizeof(name) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(desc, token, sizeof(desc) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(loc, token, sizeof(loc) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(img, token, sizeof(img) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(date, token, sizeof(date) - 1);

        token = strtok(NULL, "|");
        if (token) st_int = atoi(token);

        if (id > 0) {
            ItemNode *node = create_item_node(id, name, desc, loc, img, date, (ItemStatus)st_int);
            insert_item_sorted(&g_inventory_head, node);
            if (id > max_id) max_id = id;
        }
    }
    fclose(fp);
    g_next_item_id = max_id + 1;
}

/**
 * Persists all inventory items to ITEMS_FILE.
 */
void save_inventory_to_file(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "[ERROR] Cannot open '%s' for writing inventory!\n", filename);
        return;
    }

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

/**
 * Loads pending claim requests from CLAIMS_QUEUE_FILE.
 * Format per line:
 * REQ_ID|ITEM_ID|CLAIMANT_NAME|USN|PHONE|EMAIL|CLAIM_DATE|NOTES|REQ_DATE
 */
void load_queue_from_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return;

    queue_free(&g_claim_queue);
    char line[BUFFER_SIZE * 2];
    int max_req = 1000;

    while (fgets(line, sizeof(line), fp)) {
        trim_whitespace(line);
        if (strlen(line) == 0 || line[0] == '#') continue;

        int req_id = 0, item_id = 0;
        Claimant c = {0};
        char notes[MAX_DESC_LEN] = {0};
        char req_date[MAX_DATE_LEN] = {0};

        char *token = strtok(line, "|");
        if (token) req_id = atoi(token);

        token = strtok(NULL, "|");
        if (token) item_id = atoi(token);

        token = strtok(NULL, "|");
        if (token) strncpy(c.name, token, sizeof(c.name) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(c.usn, token, sizeof(c.usn) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(c.phone, token, sizeof(c.phone) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(c.email, token, sizeof(c.email) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(c.claim_date, token, sizeof(c.claim_date) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(notes, token, sizeof(notes) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(req_date, token, sizeof(req_date) - 1);

        if (req_id > 0 && item_id > 0) {
            queue_enqueue(&g_claim_queue, req_id, item_id, &c, notes, req_date);
            if (req_id > max_req) max_req = req_id;
        }
    }
    fclose(fp);
    g_next_request_id = max_req + 1;
}

/**
 * Persists pending claim queue to CLAIMS_QUEUE_FILE.
 */
void save_queue_to_file(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "[ERROR] Cannot open '%s' for saving queue!\n", filename);
        return;
    }

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

/**
 * ============================================================================
 * CLAIM HISTORY DATABASE & 1-YEAR RETENTION POLICY
 * ============================================================================
 * [1-YEAR AUDIT RETENTION EXPLANATION]:
 * This function appends each verified claim transaction to "claim_history.txt".
 * In college administration, lost items (electronics, IDs, wallets, keys) carry
 * financial and legal responsibilities.
 *
 * This file serves as the permanent 1-Year Retention Archive:
 * 1. Legal & Dispute Verification: If an item is mistakenly claimed by the wrong
 *    individual, the audit trail retains the claimant's USN, Phone, and verified
 *    College Email for disciplinary or legal investigation.
 * 2. Automated / Periodic Archival: Each entry records an ISO timestamp.
 *    College IT batch scripts or annual audits review entries older than 365 days
 *    for cold-storage archival or safe purging in accordance with data privacy laws.
 * 3. Non-Destructive Storage: Active items can be marked CLAIMED or cleared from
 *    the active view, but the claimant database is append-only, preserving an
 *    immutable log throughout the academic year.
 * ============================================================================
 */
void append_claim_to_history(const ItemNode *item, const Claimant *claimant,
                             const char *action_notes, const char *filename) {
    if (!item || !claimant) return;

    FILE *fp = fopen(filename, "a");
    if (!fp) {
        fprintf(stderr, "[ERROR] Unable to open Claim History file '%s' for appending!\n", filename);
        return;
    }

    char current_time[MAX_DATE_LEN];
    get_current_timestamp_str(current_time, sizeof(current_time));

    fprintf(fp, "================================================================================\n");
    fprintf(fp, "TRANSACTION RECORD: CLAIM & DISBURSEMENT AUDIT LOG\n");
    fprintf(fp, "Log Timestamp       : %s\n", current_time);
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
    fprintf(fp, "  - Admin Remarks   : %s\n", (action_notes && strlen(action_notes) > 0) ? action_notes : "Identity verified via college ID card.");
    fprintf(fp, "================================================================================\n\n");

    fclose(fp);
    printf("[+] Claimant history permanently written to '%s' (1-Year Retention Archive).\n", filename);
}

void display_claim_history_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("\n[*] No claim history recorded yet. The file '%s' is empty.\n", filename);
        return;
    }



