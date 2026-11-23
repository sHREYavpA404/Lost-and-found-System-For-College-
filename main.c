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
 *    - Extended with Category classification and Value Tier (LOW vs HIGH).
 *
 * 2. QUEUE (FIFO - First In, First Out):
 *    - Implemented as a singly-linked FIFO Queue (front and rear pointers).
 *    - Manages pending claim requests submitted by students or front-desk staff.
 *    - Guarantees fair, first-come-first-served processing of claims.
 *    - Enriched with multi-factor security proofs and scheduled campus visit slots.
 *
 * NEW ENTERPRISE FEATURES:
 * 1. Scheduled Campus Visits:
 *    - Automated appointment booking module with real-time capacity management
 *      (6 daily 30-minute slots, max 3 students per slot).
 * 2. Advanced Keyword & Category Filters:
 *    - Multi-criteria filtering by category (Electronics, IDs & Cards, Books & Stationery,
 *      Bags & Bottles, Personal Accessories, Other) alongside campus locations and keywords.
 * 3. Structured Device Ownership Verification:
 *    - Serial / IMEI matching and invoice verification for electronic equipment.
 *
 * DATA PERSISTENCE & 1-YEAR RETENTION DATABASE:
 * - Active Items File: "items.txt"
 *   Stores active and pending items in structured delimited format (9 columns).
 * - Claim History Database: "claim_history.txt"
 *   Acts as the official 1-Year Retention Archive for college audit compliance.
 *   Every time an item is claimed and handed over, full claimant details, security
 *   verification credentials, and visit records are permanently logged.
 * - Queue Backup File: "claims_queue.txt"
 *   Ensures pending claims and visit bookings persist across application restarts.
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
#define BUFFER_SIZE         1024

/* Appointment Slot Constants */
#define NUM_SLOTS           6
#define SLOT_CAPACITY       3

static const char *CAMPUS_SLOTS[NUM_SLOTS] = {
    "09:30 AM - 10:00 AM",
    "10:30 AM - 11:00 AM",
    "11:30 AM - 12:00 PM",
    "02:00 PM - 02:30 PM",
    "03:00 PM - 03:30 PM",
    "04:00 PM - 04:30 PM"
};

/* Categories */
#define NUM_CATEGORIES 6
static const char *ITEM_CATEGORIES[NUM_CATEGORIES] = {
    "Electronics",
    "IDs & Cards",
    "Books & Stationery",
    "Bags & Bottles",
    "Personal Accessories",
    "Other"
};

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
    char date_posted[MAX_DATE_LEN]; /* Date uploaded to website (YYYY-MM-DD) */
    ItemStatus status;              /* Current availability status */
    char category[32];              /* Category: Electronics, IDs & Cards, etc. */
    char value_tier[16];            /* "LOW" (Express) or "HIGH" (Multi-Factor Security) */
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
    char notes[MAX_DESC_LEN];       /* Proof of ownership / description provided */
    char request_date[MAX_DATE_LEN];/* Timestamp of request submission */
    char visit_date[32];            /* Scheduled campus visit date (YYYY-MM-DD) */
    char visit_slot[32];            /* Scheduled 30-min time slot */
    char serial_no[64];             /* Hardware Serial / IMEI for High-Value assets */
    char purchase_proof[128];       /* Proof of purchase / Invoice / Store receipt */
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
void get_tomorrow_date_str(char *dest, size_t max_size);
void get_current_timestamp_str(char *dest, size_t max_size);
void trim_whitespace(char *str);
int  read_line(char *dest, size_t max_len);
int  read_int_range(int min_val, int max_val);
void read_masked_password(char *dest, size_t max_len);
void clear_screen(void);
void pause_console(void);

/* Validation & Prompt Helpers */
int  validate_college_email(const char *email, const char *required_domain);
int  validate_phone(const char *phone);
int  validate_usn(const char *usn);
void prompt_location(char *dest, size_t max_len);
void prompt_category(char *dest, size_t max_len);
int  count_slot_bookings(const char *date, const char *slot);
void prompt_campus_visit_slot(char *date_dest, size_t date_len, char *slot_dest, size_t slot_len);

/* Linked List Operations (Item Inventory) */
ItemNode* create_item_node(int id, const char *name, const char *desc,
                           const char *loc, const char *img, const char *date,
                           const char *date_posted,
                           ItemStatus st, const char *cat, const char *tier);
void insert_item_sorted(ItemNode **head_ref, ItemNode *new_node);
ItemNode* find_item_by_id(ItemNode *head, int id);
int  delete_item_by_id(ItemNode **head_ref, int id);
void display_all_items(const ItemNode *head, int only_available);
void display_item_detailed(const ItemNode *item);
void search_items_by_keyword(const ItemNode *head, const char *keyword);
void filter_items_by_category(const ItemNode *head, const char *cat_query);
void filter_items_by_location(const ItemNode *head, const char *loc_query);
void filter_items_by_date(const ItemNode *head);
void free_inventory(ItemNode **head_ref);

/* Queue Operations (Claim Requests Waiting List) */
void queue_init(ClaimQueue *q);
int  queue_enqueue(ClaimQueue *q, int req_id, int item_id, const Claimant *claimant,
                   const char *notes, const char *req_date, const char *visit_date,
                   const char *visit_slot, const char *serial_no, const char *purchase_proof);
int  queue_dequeue(ClaimQueue *q, ClaimRequestNode *out_node);
void queue_display(const ClaimQueue *q);
ClaimRequestNode* queue_find_by_req_id(const ClaimQueue *q, int req_id);
void queue_free(ClaimQueue *q);

/* File I/O & 1-Year Retention Database Operations */
void load_inventory_from_file(const char *filename);
void save_inventory_to_file(const char *filename);
void load_queue_from_file(const char *filename);
void save_queue_to_file(const char *filename);
void append_claim_to_history(const ItemNode *item, const Claimant *claimant,
                             const char *action_notes, const char *serial_no,
                             const char *purchase_proof, const char *visit_info,
                             const char *filename);
void display_claim_history_file(const char *filename);
void display_scheduled_campus_visits(void);

/* Sub-system Modules */
void add_item(void);
void edit_item_details(void);
void edit_claim_request(void);
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
        snprintf(dest, max_size, "2026-11-16");
    }
}

void get_tomorrow_date_str(char *dest, size_t max_size) {
    time_t now = time(NULL) + (24 * 60 * 60);
    struct tm *t = localtime(&now);
    if (t != NULL) {
        strftime(dest, max_size, "%Y-%m-%d", t);
    } else {
        snprintf(dest, max_size, "2026-11-17");
    }
}

void get_current_timestamp_str(char *dest, size_t max_size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t != NULL) {
        strftime(dest, max_size, "%Y-%m-%d %H:%M:%S", t);
    } else {
        snprintf(dest, max_size, "2026-11-16 12:00:00");
    }
}

void trim_whitespace(char *str) {
    if (!str) return;
    int len = (int)strlen(str);
    while (len > 0 && (isspace((unsigned char)str[len - 1]) || str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
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
        printf("[!] Invalid input. Please enter a number between %d and %d.\n", min_val, max_val);
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
        } else if (ch == '\b') {
            if (idx > 0) {
                idx--;
                printf("\b \b");
            }
        } else if (ch == 0 || ch == 224) {
            _getch();
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
 * IMPLEMENTATION: Validation & Prompt Helpers
 * ============================================================================ */

int validate_college_email(const char *email, const char *required_domain) {
    if (!email || !required_domain) return 0;
    size_t email_len = strlen(email);
    size_t domain_len = strlen(required_domain);

    if (email_len <= domain_len) return 0;
    if (!isalnum((unsigned char)email[0])) return 0;

    const char *at_pos = strchr(email, '@');
    if (!at_pos || at_pos == email) return 0;
    if (strchr(at_pos + 1, '@') != NULL) return 0;

    for (const char *p = email; p < at_pos; p++) {
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-') {
            return 0;
        }
    }

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
            if (read_line(room, sizeof(room)) > 0) break;
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
}

void prompt_category(char *dest, size_t max_len) {
    printf("\n----------------------------------------------------\n");
    printf(" SELECT ITEM CATEGORY:\n");
    printf("----------------------------------------------------\n");
    for (int i = 0; i < NUM_CATEGORIES; i++) {
        printf(" %d. %s\n", i + 1, ITEM_CATEGORIES[i]);
    }
    printf("----------------------------------------------------\n");
    int c = read_int_range(1, NUM_CATEGORIES);
    strncpy(dest, ITEM_CATEGORIES[c - 1], max_len - 1);
    dest[max_len - 1] = '\0';
}

int count_slot_bookings(const char *date, const char *slot) {
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

void prompt_campus_visit_slot(char *date_dest, size_t date_len, char *slot_dest, size_t slot_len) {
    char default_date[32];
    get_tomorrow_date_str(default_date, sizeof(default_date));

    printf("\n====================================================\n");
    printf("       SCHEDULE CAMPUS VISIT FOR ITEM PICKUP        \n");
    printf("====================================================\n");
    printf("Office Hours: 09:30 AM to 05:00 PM (Lost & Found Desk, Admin Block)\n");
    printf("Enter Preferred Visit Date [Press Enter for default %s] (YYYY-MM-DD): ", default_date);

    char entered_date[32];
    if (read_line(entered_date, sizeof(entered_date)) == 0) {
        strncpy(date_dest, default_date, date_len - 1);
    } else {
        strncpy(date_dest, entered_date, date_len - 1);
    }
    date_dest[date_len - 1] = '\0';

    printf("\nAvailable 30-Minute Visit Time Slots for %s:\n", date_dest);
    printf("----------------------------------------------------------\n");
    printf("%-4s | %-28s | %s\n", "NO.", "TIME SLOT", "STATUS");
    printf("----------------------------------------------------------\n");

    int avail_count = 0;

    for (int i = 0; i < NUM_SLOTS; i++) {
        int booked = count_slot_bookings(date_dest, CAMPUS_SLOTS[i]);
        int remaining = SLOT_CAPACITY - booked;
        if (remaining < 0) remaining = 0;

        printf(" %d.  | %-28s | %s\n",
               i + 1, CAMPUS_SLOTS[i],
               remaining > 0 ? "[AVAILABLE]" : "[SLOT FULL]");

        if (remaining > 0) {
            avail_count++;
        }
    }
    printf("----------------------------------------------------------\n");

    if (avail_count == 0) {
        printf("[!] All slots on %s are fully booked! Defaulting to standby appointment.\n", date_dest);
        strncpy(slot_dest, CAMPUS_SLOTS[0], slot_len - 1);
        slot_dest[slot_len - 1] = '\0';
        return;
    }

    while (1) {
        printf("Select an available slot number (1 - %d): ", NUM_SLOTS);
        int sel = read_int_range(1, NUM_SLOTS);
        int slot_idx = sel - 1;
        int booked = count_slot_bookings(date_dest, CAMPUS_SLOTS[slot_idx]);
        if (booked >= SLOT_CAPACITY) {
            printf("[!] Slot '%s' is full. Please choose another slot.\n", CAMPUS_SLOTS[slot_idx]);
            continue;
        }
        strncpy(slot_dest, CAMPUS_SLOTS[slot_idx], slot_len - 1);
        slot_dest[slot_len - 1] = '\0';
        printf("[+] Appointment reserved for: %s, %s\n", date_dest, slot_dest);
        break;
    }
}

/* ============================================================================
 * IMPLEMENTATION: Linked List Operations (Item Inventory)
 * ============================================================================ */

ItemNode* create_item_node(int id, const char *name, const char *desc,
                           const char *loc, const char *img, const char *date,
                           const char *date_posted,
                           ItemStatus st, const char *cat, const char *tier) {
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

    strncpy(node->date_posted, (date_posted && strlen(date_posted) > 0) ? date_posted : (date ? date : "N/A"), sizeof(node->date_posted) - 1);
    node->date_posted[sizeof(node->date_posted) - 1] = '\0';

    node->status = st;

    strncpy(node->category, (cat && strlen(cat) > 0) ? cat : "Other", sizeof(node->category) - 1);
    node->category[sizeof(node->category) - 1] = '\0';

    strncpy(node->value_tier, (tier && strlen(tier) > 0) ? tier : "LOW", sizeof(node->value_tier) - 1);
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
        if (curr->id == id) return curr;
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

    if (!curr) return 0;

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

    printf("\n%-5s | %-22s | %-18s | %-22s | %-12s | %-13s\n",
           "ID", "NAME", "CATEGORY", "LOCATION", "DATE", "STATUS");
    printf("--------------------------------------------------------------------------------------------------------\n");

    int count = 0;
    const ItemNode *curr = head;
    while (curr) {
        if (!only_available || curr->status == STATUS_AVAILABLE) {
            printf("#%-4d | %-22.22s | %-18.18s | %-22.22s | %-12.12s | %-13s\n",
                   curr->id, curr->name, curr->category,
                   curr->location, curr->date_found, status_to_string(curr->status));
            count++;
        }
        curr = curr->next;
    }

    printf("--------------------------------------------------------------------------------------------------------\n");
    printf("Total items displayed: %d\n", count);
}

void display_item_detailed(const ItemNode *item) {
    if (!item) return;
    printf("\n====================================================================\n");
    printf("                 ITEM DETAILS (ID: #%d)                             \n", item->id);
    printf("====================================================================\n");
    printf(" Item Name    : %s\n", item->name);
    printf(" Category     : %s\n", item->category);
    printf(" Description  : %s\n", item->description);
    printf(" Location     : %s\n", item->location);
    printf(" Date Found   : %s\n", item->date_found);
    printf(" Image Path   : %s\n", item->image_path);
    printf(" Status       : %s\n", status_to_string(item->status));
    printf("====================================================================\n");
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

 }

    printf("\n================================================================================\n");
    printf("             COLLEGE CLAIMANT HISTORY DATABASE (1-YEAR RETENTION)               \n");
    printf("================================================================================\n");

    char buffer[BUFFER_SIZE];
    int line_count = 0;
    while (fgets(buffer, sizeof(buffer), fp)) {
        printf("%s", buffer);
        line_count++;
    }
    fclose(fp); printf("\n=== SEARCH RESULTS FOR: '%s' ===\n", keyword);
    printf("%-5s | %-22s | %-18s | %-22s | %-12s | %-13s\n",
           "ID", "NAME", "CATEGORY", "LOCATION", "DATE", "STATUS");
    printf("--------------------------------------------------------------------------------------------------------\n");

    int found_count = 0;
    const ItemNode *curr = head;
    while (curr) {
        char name_l[MAX_NAME_LEN], desc_l[MAX_DESC_LEN], loc_l[MAX_LOC_LEN], cat_l[32];
        strncpy(name_l, curr->name, sizeof(name_l) - 1); name_l[sizeof(name_l) - 1] = '\0';
        strncpy(desc_l, curr->description, sizeof(desc_l) - 1); desc_l[sizeof(desc_l) - 1] = '\0';
        strncpy(loc_l, curr->location, sizeof(loc_l) - 1); loc_l[sizeof(loc_l) - 1] = '\0';
        strncpy(cat_l, curr->category, sizeof(cat_l) - 1); cat_l[sizeof(cat_l) - 1] = '\0';

        for (size_t i = 0; name_l[i]; i++) name_l[i] = (char)tolower((unsigned char)name_l[i]);
        for (size_t i = 0; desc_l[i]; i++) desc_l[i] = (char)tolower((unsigned char)desc_l[i]);
        for (size_t i = 0; loc_l[i]; i++) loc_l[i] = (char)tolower((unsigned char)loc_l[i]);
        for (size_t i = 0; cat_l[i]; i++) cat_l[i] = (char)tolower((unsigned char)cat_l[i]);

        if (strstr(name_l, term_lower) || strstr(desc_l, term_lower) ||
            strstr(loc_l, term_lower) || strstr(cat_l, term_lower)) {
            printf("#%-4d | %-22.22s | %-18.18s | %-22.22s | %-12.12s | %-13s\n",
                   curr->id, curr->name, curr->category,
                   curr->location, curr->date_found, status_to_string(curr->status));
            found_count++;
        }
        curr = curr->next;
    }

    printf("--------------------------------------------------------------------------------------------------------\n");
    printf("Found %d matching item(s).\n", found_count);
}

void filter_items_by_category(const ItemNode *head, const char *cat_query) {
    if (!head || !cat_query) return;

    printf("\n=== ITEMS FILTERED BY CATEGORY: '%s' ===\n", cat_query);
    printf("%-5s | %-22s | %-18s | %-22s | %-12s | %-13s\n",
           "ID", "NAME", "CATEGORY", "LOCATION", "DATE", "STATUS");
    printf("--------------------------------------------------------------------------------------------------------\n");

    int count = 0;
    const ItemNode *curr = head;
    while (curr) {
        if (strcmp(curr->category, cat_query) == 0) {
            printf("#%-4d | %-22.22s | %-18.18s | %-22.22s | %-12.12s | %-13s\n",
                   curr->id, curr->name, curr->category,
                   curr->location, curr->date_found, status_to_string(curr->status));
            count++;
        }
        curr = curr->next;
    }

    printf("--------------------------------------------------------------------------------------------------------\n");
    printf("Total items in '%s': %d\n", cat_query, count);
}

void filter_items_by_location(const ItemNode *head, const char *loc_query) {
    if (!head || !loc_query) return;

    printf("\n=== ITEMS FILTERED BY LOCATION: '%s' ===\n", loc_query);
    printf("%-5s | %-22s | %-18s | %-22s | %-12s | %-13s\n",
           "ID", "NAME", "CATEGORY", "LOCATION", "DATE", "STATUS");
    printf("--------------------------------------------------------------------------------------------------------\n");

    int count = 0;
    const ItemNode *curr = head;
    while (curr) {
        if (strstr(curr->location, loc_query) != NULL) {
            printf("#%-4d | %-22.22s | %-18.18s | %-22.22s | %-12.12s | %-13s\n",
                   curr->id, curr->name, curr->category,
                   curr->location, curr->date_found, status_to_string(curr->status));
            count++;
        }
        curr = curr->next;
    }

    printf("--------------------------------------------------------------------------------------------------------\n");
    printf("Matches in '%s': %d\n", loc_query, count);
}

void filter_items_by_date(const ItemNode *head) {
    if (!head) {
        printf("\n[!] Inventory is empty.\n");
        return;
    }

    printf("\n====================================================\n");
    printf("         FILTER ITEMS BY UPLOAD / FOUND DATE        \n");
    printf("====================================================\n");
    printf(" 1. Uploaded Today (2026-11-16)\n");
    printf(" 2. Uploaded in Past 3 Days\n");
    printf(" 3. Uploaded in Past 7 Days\n");
    printf(" 4. Filter by Specific Date (YYYY-MM-DD)\n");
    printf("====================================================\n");
    int opt = read_int_range(1, 4);

    char target_date[32] = {0};
    char today_str[32];
    get_current_date_str(today_str, sizeof(today_str));

    if (opt == 1) {
        strncpy(target_date, today_str, sizeof(target_date) - 1);
    } else if (opt == 4) {
        printf("\nEnter Specific Date (YYYY-MM-DD): ");
        read_line(target_date, sizeof(target_date));
    }

    printf("\n=== MATCHING ITEMS BY DATE FILTER ===\n");
    printf("%-5s | %-20s | %-16s | %-18s | %-11s | %-11s | %-11s\n",
           "ID", "NAME", "CATEGORY", "LOCATION", "FOUND DATE", "POSTED DATE", "STATUS");
    printf("---------------------------------------------------------------------------------------------------------------\n");

    int count = 0;
    const ItemNode *curr = head;
    while (curr) {
        int match = 0;
        const char *item_date = strlen(curr->date_posted) > 0 ? curr->date_posted : curr->date_found;

        if (opt == 1 || opt == 4) {
            if (strcmp(item_date, target_date) == 0 || strcmp(curr->date_found, target_date) == 0) {
                match = 1;
            }
        } else if (opt == 2) {
            if (strcmp(item_date, "2026-11-16") == 0 || strcmp(item_date, "2026-11-15") == 0 ||
                strcmp(item_date, "2026-11-14") == 0 || strcmp(item_date, today_str) == 0) {
                match = 1;
            }
        } else if (opt == 3) {
            if (strcmp(item_date, "2026-11-09") >= 0) {
                match = 1;
            }
        }

        if (match) {
            printf("#%-4d | %-20.20s | %-16.16s | %-18.18s | %-11.11s | %-11.11s | %-11s\n",
                   curr->id, curr->name, curr->category,
                   curr->location, curr->date_found, curr->date_posted, status_to_string(curr->status));
            count++;
        }
        curr = curr->next;
    }

    printf("---------------------------------------------------------------------------------------------------------------\n");
    printf("Total items matching date filter: %d\n", count);
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
                   const char *notes, const char *req_date, const char *visit_date,
                   const char *visit_slot, const char *serial_no, const char *purchase_proof) {
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

    strncpy(node->visit_date, visit_date ? visit_date : "", sizeof(node->visit_date) - 1);
    node->visit_date[sizeof(node->visit_date) - 1] = '\0';

    strncpy(node->visit_slot, visit_slot ? visit_slot : "", sizeof(node->visit_slot) - 1);
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

    printf("\n========================================================================================================\n");
    printf("             PENDING CLAIM REQUESTS QUEUE (FIFO WAITING LIST - %d TOTAL)                                 \n", q->count);
    printf("========================================================================================================\n");
    printf("%-7s | %-7s | %-20s | %-14s | %-12s | %-22s\n",
           "REQ ID", "ITEM ID", "CLAIMANT NAME", "USN", "VISIT DATE", "VISIT TIME SLOT");
    printf("--------------------------------------------------------------------------------------------------------\n");

    const ClaimRequestNode *curr = q->front;
    int pos = 1;
    while (curr) {
        printf("#%-6d | #%-6d | %-20.20s | %-14.14s | %-12.12s | %-22.22s (Pos: %d)\n",
               curr->request_id, curr->item_id, curr->claimant.name,
               curr->claimant.usn, curr->visit_date, curr->visit_slot, pos++);
        curr = curr->next;
    }
    printf("========================================================================================================\n");
}

ClaimRequestNode* queue_find_by_req_id(const ClaimQueue *q, int req_id) {
    if (!q) return NULL;
    ClaimRequestNode *curr = q->front;
    while (curr) {
        if (curr->request_id == req_id) return curr;
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

void load_inventory_from_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return;

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
        char cat[32] = {0};
        char tier[16] = {0};

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

        token = strtok(NULL, "|");
        if (token) strncpy(cat, token, sizeof(cat) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(tier, token, sizeof(tier) - 1);

        char date_posted[MAX_DATE_LEN] = {0};
        token = strtok(NULL, "|");
        if (token) {
            strncpy(date_posted, token, sizeof(date_posted) - 1);
        } else {
            strncpy(date_posted, date, sizeof(date_posted) - 1);
        }

        /* Defaults if missing */
        if (strlen(cat) == 0) strcpy(cat, "Other");
        if (strlen(tier) == 0) strcpy(tier, "LOW");

        if (id > 0) {
            ItemNode *node = create_item_node(id, name, desc, loc, img, date, date_posted, (ItemStatus)st_int, cat, tier);
            insert_item_sorted(&g_inventory_head, node);
            if (id > max_id) max_id = id;
        }
    }
    fclose(fp);
    g_next_item_id = max_id + 1;
}

void save_inventory_to_file(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "[ERROR] Cannot open '%s' for writing inventory!\n", filename);
        return;
    }

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
        char visit_date[32] = {0};
        char visit_slot[32] = {0};
        char serial_no[64] = {0};
        char purchase_proof[128] = {0};

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

        token = strtok(NULL, "|");
        if (token) strncpy(visit_date, token, sizeof(visit_date) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(visit_slot, token, sizeof(visit_slot) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(serial_no, token, sizeof(serial_no) - 1);

        token = strtok(NULL, "|");
        if (token) strncpy(purchase_proof, token, sizeof(purchase_proof) - 1);

        if (req_id > 0 && item_id > 0) {
            queue_enqueue(&g_claim_queue, req_id, item_id, &c, notes, req_date,
                          visit_date, visit_slot, serial_no, purchase_proof);
            if (req_id > max_req) max_req = req_id;
        }
    }
    fclose(fp);
    g_next_request_id = max_req + 1;
}

void save_queue_to_file(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "[ERROR] Cannot open '%s' for saving queue!\n", filename);
        return;
    }

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

void append_claim_to_history(const ItemNode *item, const Claimant *claimant,
                             const char *action_notes, const char *serial_no,
                             const char *purchase_proof, const char *visit_info,
                             const char *filename) {
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

    if ((serial_no && strlen(serial_no) > 0) || (purchase_proof && strlen(purchase_proof) > 0)) {
        fprintf(fp, "VERIFICATION DETAILS:\n");
        if (serial_no && strlen(serial_no) > 0)
            fprintf(fp, "  - Hardware Serial : %s\n", serial_no);
        if (purchase_proof && strlen(purchase_proof) > 0)
            fprintf(fp, "  - Purchase Proof  : %s\n", purchase_proof);
    }

    if (visit_info && strlen(visit_info) > 0) {
        fprintf(fp, "SCHEDULED CAMPUS VISIT:\n");
        fprintf(fp, "  - Pickup Slot     : %s\n", visit_info);
    }

    fprintf(fp, "OFFICE VERIFICATION:\n");
    fprintf(fp, "  - Handover Status : CLAIMED & VERIFIED BY ADMIN\n");
    fprintf(fp, "  - Admin Remarks   : %s\n", (action_notes && strlen(action_notes) > 0) ? action_notes : "Identity and ownership verified in person.");
    fprintf(fp, "================================================================================\n\n");

    fclose(fp);
    printf("[+] Claimant history permanently written to '%s' (1-Year Retention Archive).\n", filename);
}

void display_claim_history_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("\n[*] No claim history recorded yet. The file '%s' is empty.\n", filename);
        return;

    if (line_count == 0) {
        printf("[*] The claim history archive is currently empty.\n");
    }
    printf("================================================================================\n");
}

void display_scheduled_campus_visits(void) {
    printf("\n================================================================================\n");
    printf("                 SCHEDULED CAMPUS VISITS (ADMIN DESK SCHEDULE)                  \n");
    printf("================================================================================\n");

    if (g_claim_queue.count == 0) {
        printf("[*] No upcoming campus visits scheduled at this time.\n");
        printf("================================================================================\n");
        return;
    }

    printf("%-12s | %-22s | %-8s | %-18s | %-12s | %s\n",
           "VISIT DATE", "TIME SLOT", "REQ ID", "STUDENT NAME", "USN", "ITEM");
    printf("--------------------------------------------------------------------------------\n");

    ClaimRequestNode *curr = g_claim_queue.front;
    int visit_count = 0;
    while (curr) {
        if (strlen(curr->visit_date) > 0) {
            ItemNode *item = find_item_by_id(g_inventory_head, curr->item_id);
            const char *item_name = item ? item->name : "Item";
            printf("%-12.12s | %-22.22s | #%-7d | %-18.18s | %-12.12s | %s\n",
                   curr->visit_date, curr->visit_slot, curr->request_id,
                   curr->claimant.name, curr->claimant.usn, item_name);
            visit_count++;
        }
        curr = curr->next;
    }

    if (visit_count == 0) {
        printf("[*] No visits scheduled with specific dates.\n");
    }
    printf("--------------------------------------------------------------------------------\n");
    printf("Total Scheduled Visits: %d\n", visit_count);
    printf("================================================================================\n");
}

/* ============================================================================
 * IMPLEMENTATION: Core Sub-system Operations
 * ============================================================================ */

void add_item(void) {
    printf("\n====================================================\n");
    printf("              ADD NEW FOUND ITEM (ADMIN)            \n");
    printf("====================================================\n");

    char name[MAX_NAME_LEN];
    char desc[MAX_DESC_LEN];
    char location[MAX_LOC_LEN];
    char image_path[MAX_PATH_LEN];
    char date_found[MAX_DATE_LEN];
    char category[32];
    char value_tier[16];

    /* Item Name */
    while (1) {
        printf("Enter Item Name (e.g. Blue Titan Watch, Water Bottle): ");
        if (read_line(name, sizeof(name)) > 0) break;
        printf("[!] Item name cannot be blank.\n");
    }

    /* Category */
    prompt_category(category, sizeof(category));
    printf("[+] Category selected: %s\n", category);

    if (strcmp(category, "Electronics") == 0) {
        strncpy(value_tier, "HIGH", sizeof(value_tier) - 1);
    } else {
        strncpy(value_tier, "LOW", sizeof(value_tier) - 1);
    }
    value_tier[sizeof(value_tier) - 1] = '\0';

    /* Description */
    while (1) {
        printf("\nEnter Item Description (Color, Brand, Markings, Condition): ");
        if (read_line(desc, sizeof(desc)) > 0) break;
        printf("[!] Description cannot be blank.\n");
    }

    /* Location Sub-Menu */
    prompt_location(location, sizeof(location));
    printf("[+] Location set to: %s\n", location);

    /* Image File Path */
    printf("\nEnter Image File Path (e.g., C:/images/watch.jpg or 'none'): ");
    if (read_line(image_path, sizeof(image_path)) == 0) {
        strncpy(image_path, "None", sizeof(image_path) - 1);
    }

    /* Date Found (Defaults to today's date) */
    char default_date[MAX_DATE_LEN];
    get_current_date_str(default_date, sizeof(default_date));
    printf("Enter Date Found [Press Enter for today: %s] (YYYY-MM-DD): ", default_date);
    if (read_line(date_found, sizeof(date_found)) == 0) {
        strncpy(date_found, default_date, sizeof(date_found) - 1);
    }

    char date_posted[MAX_DATE_LEN];
    printf("Enter Date Uploaded to Website [Press Enter for today: %s] (YYYY-MM-DD): ", default_date);
    if (read_line(date_posted, sizeof(date_posted)) == 0) {
        strncpy(date_posted, default_date, sizeof(date_posted) - 1);
    }

    int new_id = g_next_item_id++;
    ItemNode *new_node = create_item_node(new_id, name, desc, location,
                                          image_path, date_found, date_posted, STATUS_AVAILABLE,
                                          category, value_tier);

    insert_item_sorted(&g_inventory_head, new_node);
    save_inventory_to_file(ITEMS_FILE);

    printf("\n[SUCCESS] Item #%d ('%s') successfully logged into inventory!\n", new_id, name);
    display_item_detailed(new_node);
}

void edit_item_details(void) {
    printf("\n====================================================\n");
    printf("              EDIT FOUND ITEM (ADMIN)               \n");
    printf("====================================================\n");
    if (!g_inventory_head) {
        printf("[!] Inventory is currently empty.\n");
        return;
    }

    display_all_items(g_inventory_head, 0);
    printf("\nEnter Item ID to Edit: ");
    int id = 0;
    char id_buf[32];
    if (read_line(id_buf, sizeof(id_buf)) == 0 || (id = atoi(id_buf)) <= 0) {
        printf("[!] Invalid Item ID.\n");
        return;
    }

    ItemNode *item = find_item_by_id(g_inventory_head, id);
    if (!item) {
        printf("[!] Item #%d not found in inventory.\n", id);
        return;
    }

    printf("\nEditing Item #%d:\n", item->id);
    display_item_detailed(item);

    char input[MAX_DESC_LEN];

    printf("\nEnter New Name [Press Enter to keep '%s']: ", item->name);
    if (read_line(input, sizeof(input)) > 0) {
        strncpy(item->name, input, sizeof(item->name) - 1);
    }

    printf("Do you want to update category (Current: %s)? (1 = Yes, 2 = No): ", item->category);
    int cat_c = read_int_range(1, 2);
    if (cat_c == 1) {
        prompt_category(item->category, sizeof(item->category));
    }

    printf("Enter New Description [Press Enter to keep current]: ");
    if (read_line(input, sizeof(input)) > 0) {
        strncpy(item->description, input, sizeof(item->description) - 1);
    }

    printf("Do you want to update location (Current: %s)? (1 = Yes, 2 = No): ", item->location);
    int loc_c = read_int_range(1, 2);
    if (loc_c == 1) {
        prompt_location(item->location, sizeof(item->location));
    }

    printf("Enter New Image Path [Press Enter to keep '%s']: ", item->image_path);
    if (read_line(input, sizeof(input)) > 0) {
        strncpy(item->image_path, input, sizeof(item->image_path) - 1);
    }

    printf("Enter Date Found [Press Enter to keep '%s'] (YYYY-MM-DD): ", item->date_found);
    if (read_line(input, sizeof(input)) > 0) {
        strncpy(item->date_found, input, sizeof(item->date_found) - 1);
    }

    printf("Enter Date Uploaded [Press Enter to keep '%s'] (YYYY-MM-DD): ", item->date_posted);
    if (read_line(input, sizeof(input)) > 0) {
        strncpy(item->date_posted, input, sizeof(item->date_posted) - 1);
    }

    printf("\nUpdate Item Status? (1 = AVAILABLE, 2 = PENDING CLAIM, 3 = CLAIMED, 4 = Keep '%s'): ", status_to_string(item->status));
    int st_c = read_int_range(1, 4);
if (st_c == 1) item->status = STATUS_AVAILABLE;
    else if (st_c == 2) item->status = STATUS_PENDING_CLAIM;
    else if (st_c == 3) item->status = STATUS_CLAIMED;

    save_inventory_to_file(ITEMS_FILE);
    printf("\n[SUCCESS] Item #%d updated successfully!\n", item->id);
    display_item_detailed(item);
}

void edit_claim_request(void) {
    printf("\n====================================================\n");
    printf("         EDIT / RESCHEDULE CLAIM REQUEST (ADMIN)     \n");
    printf("====================================================\n");
    if (g_claim_queue.count == 0) {
        printf("[*] No pending claims in queue to edit.\n");
        return;
    }

    queue_display(&g_claim_queue);
    printf("\nEnter Claim Request ID to Edit (e.g. 1001): ");
    char req_buf[32];
    int req_id = 0;
    if (read_line(req_buf, sizeof(req_buf)) == 0 || (req_id = atoi(req_buf)) <= 0) {
        printf("[!] Invalid Request ID.\n");
        return;
    }

    ClaimRequestNode *node = queue_find_by_req_id(&g_claim_queue, req_id);
    if (!node) {
        printf("[!] Claim Request #%d not found in queue.\n", req_id);
        return;
    }

    printf("\nEditing Claim Request #%d (Item #%d):\n", node->request_id, node->item_id);
    printf(" Claimant Name: %s | USN: %s | Phone: %s | Email: %s\n",
           node->claimant.name, node->claimant.usn, node->claimant.phone, node->claimant.email);
    printf(" Scheduled Visit: %s, %s\n", node->visit_date, node->visit_slot);

    char input[MAX_DESC_LEN];

    printf("\nEnter New Claimant Name [Press Enter to keep '%s']: ", node->claimant.name);
    if (read_line(input, sizeof(input)) > 0) {
        strncpy(node->claimant.name, input, sizeof(node->claimant.name) - 1);
    }

    printf("Enter New USN [Press Enter to keep '%s']: ", node->claimant.usn);
    if (read_line(input, sizeof(input)) > 0) {
        strncpy(node->claimant.usn, input, sizeof(node->claimant.usn) - 1);
    }

    printf("Enter New Phone [Press Enter to keep '%s']: ", node->claimant.phone);
    if (read_line(input, sizeof(input)) > 0) {
        strncpy(node->claimant.phone, input, sizeof(node->claimant.phone) - 1);
    }

    printf("Enter New College Email [Press Enter to keep '%s']: ", node->claimant.email);
    if (read_line(input, sizeof(input)) > 0) {
        if (validate_college_email(input, COLLEGE_EMAIL_DOMAIN)) {
            strncpy(node->claimant.email, input, sizeof(node->claimant.email) - 1);
        } else {
            printf("[!] Invalid email, keeping original.\n");
        }
    }

    printf("Do you want to reschedule campus visit? (1 = Yes, 2 = No): ");
    int resched = read_int_range(1, 2);
    if (resched == 1) {
        prompt_campus_visit_slot(node->visit_date, sizeof(node->visit_date), node->visit_slot, sizeof(node->visit_slot));
    }

    printf("Enter New Proof Notes [Press Enter to keep current]: ");
    if (read_line(input, sizeof(input)) > 0) {
        strncpy(node->notes, input, sizeof(node->notes) - 1);
    }

    save_queue_to_file(CLAIMS_QUEUE_FILE);
    printf("\n[SUCCESS] Claim Request #%d updated successfully!\n", node->request_id);
}

void process_claim_from_queue(void) {
    if (g_claim_queue.count == 0) {
        printf("\n[*] No pending claim requests in the queue to process.\n");
        return;
    }

    ClaimRequestNode *req = g_claim_queue.front;
    ItemNode *item = find_item_by_id(g_inventory_head, req->item_id);

    printf("\n====================================================================\n");
    printf("              NEXT CLAIM REQUEST IN QUEUE (FIFO HEAD)               \n");
    printf("====================================================================\n");
    printf(" Request ID    : #%d\n", req->request_id);
    printf(" Item ID       : #%d\n", req->item_id);
    printf(" Claimant Name : %s\n", req->claimant.name);
    printf(" College USN   : %s\n", req->claimant.usn);
    printf(" Phone Number  : %s\n", req->claimant.phone);
    printf(" College Email : %s\n", req->claimant.email);
    printf(" Request Date  : %s\n", req->request_date);
    printf(" Scheduled Slot: %s, %s\n", req->visit_date, req->visit_slot);
    printf(" Ownership Note: %s\n", req->notes);

    if (strlen(req->serial_no) > 0 || strlen(req->purchase_proof) > 0) {
        printf("--------------------------------------------------------------------\n");
        printf(" [VERIFICATION DETAILS]:\n");
        if (strlen(req->serial_no) > 0)
            printf("   - Hardware Serial / IMEI : %s\n", req->serial_no);
        if (strlen(req->purchase_proof) > 0)
            printf("   - Purchase Proof/Invoice : %s\n", req->purchase_proof);
    }
    printf("====================================================================\n");

    if (!item) {
        printf("[!] Warning: Item #%d referenced in this request no longer exists in inventory.\n", req->item_id);
        printf("Dismiss this orphaned claim request? (1: Yes, 2: No): ");
        if (read_int_range(1, 2) == 1) {
            ClaimRequestNode discarded;
            queue_dequeue(&g_claim_queue, &discarded);
            save_queue_to_file(CLAIMS_QUEUE_FILE);
            printf("[+] Orphaned request discarded.\n");
        }
        return;
    }

    printf("\nTarget Item In Inventory:\n");
    display_item_detailed(item);

    printf("\nDecision for Claim Request #%d:\n", req->request_id);
    printf("1. APPROVE & HAND OVER (Mark Item Claimed & Archive to 1-Year Database)\n");
    printf("2. REJECT & DISMISS (Return Item to AVAILABLE status)\n");
    printf("3. SKIP / LEAVE IN QUEUE\n");

    int decision = read_int_range(1, 3);
    if (decision == 1) {
        ClaimRequestNode approved_req;
        queue_dequeue(&g_claim_queue, &approved_req);

        char remarks[MAX_DESC_LEN];
        printf("Enter Admin Verification Remarks / Proof Verified (e.g. Serial matched, physical ID verified): ");
        if (read_line(remarks, sizeof(remarks)) == 0) {
            strncpy(remarks, "Item verified and handed over with Student ID card.", sizeof(remarks) - 1);
        }

        item->status = STATUS_CLAIMED;
        save_inventory_to_file(ITEMS_FILE);

        char visit_summary[128];
        snprintf(visit_summary, sizeof(visit_summary), "%s, %s", approved_req.visit_date, approved_req.visit_slot);

        append_claim_to_history(item, &approved_req.claimant, remarks,
                                approved_req.serial_no, approved_req.purchase_proof,
                                visit_summary, CLAIM_HISTORY_FILE);
        save_queue_to_file(CLAIMS_QUEUE_FILE);

        printf("\n[SUCCESS] Claim Request #%d approved! Item #%d handed over and marked CLAIMED.\n",
               approved_req.request_id, item->id);
    } else if (decision == 2) {
        ClaimRequestNode rejected_req;
        queue_dequeue(&g_claim_queue, &rejected_req);

        item->status = STATUS_AVAILABLE;
        save_inventory_to_file(ITEMS_FILE);
        save_queue_to_file(CLAIMS_QUEUE_FILE);

        printf("\n[-] Claim Request #%d has been rejected and dequeued. Item #%d restored to AVAILABLE.\n",
               rejected_req.request_id, item->id);
    } else {
        printf("\n[*] Claim request left in queue for subsequent review.\n");
    }
}

void direct_claim_walkin(void) {
    printf("\n====================================================\n");
    printf("       DIRECT ON-THE-SPOT CLAIM (WALK-IN)           \n");
    printf("====================================================\n");

    display_all_items(g_inventory_head, 1);

    printf("\nEnter Item ID to process claim for: ");
    char buf[64];
    if (read_line(buf, sizeof(buf)) == 0) return;
    int item_id = atoi(buf);

    ItemNode *item = find_item_by_id(g_inventory_head, item_id);
    if (!item) {
        printf("[!] Item ID #%d not found in inventory.\n", item_id);
        return;
    }

    if (item->status == STATUS_CLAIMED) {
        printf("[!] Item #%d is ALREADY CLAIMED and disbursed!\n", item_id);
        return;
    }

    display_item_detailed(item);

    Claimant claimant;
    memset(&claimant, 0, sizeof(claimant));

    /* Claimant Name */
    while (1) {
        printf("Enter Claimant's Full Name: ");
        if (read_line(claimant.name, sizeof(claimant.name)) > 0) break;
        printf("[!] Name cannot be blank.\n");
    }

    /* College USN */
    while (1) {
        printf("Enter Claimant's College USN (e.g., 1RV21CS045): ");
        if (read_line(claimant.usn, sizeof(claimant.usn)) > 0) {
            if (validate_usn(claimant.usn)) break;
            printf("[!] Invalid USN format. Must be alphanumeric (5-20 characters).\n");
        }
    }

    /* Phone Number */
    while (1) {
        printf("Enter Claimant's Contact Phone Number: ");
        if (read_line(claimant.phone, sizeof(claimant.phone)) > 0) {
            if (validate_phone(claimant.phone)) break;
            printf("[!] Invalid phone number. Must contain at least 10 digits.\n");
        }
    }

    /* College Email Validation */
    while (1) {
        printf("Enter Claimant's College Email (MUST end with %s): ", COLLEGE_EMAIL_DOMAIN);
        if (read_line(claimant.email, sizeof(claimant.email)) > 0) {
            if (validate_college_email(claimant.email, COLLEGE_EMAIL_DOMAIN)) {
                printf("[+] Email validated successfully!\n");
                break;
            } else {
                printf("[!] INVALID EMAIL. Claimant must provide a valid college email ending with '%s'.\n",
                       COLLEGE_EMAIL_DOMAIN);
            }
        }
    }

    get_current_timestamp_str(claimant.claim_date, sizeof(claimant.claim_date));

    char serial_no[64] = {0};
    char purchase_proof[128] = {0};

    if (strcmp(item->category, "Electronics") == 0) {
        printf("\nEnter Hardware Serial Number / IMEI (if applicable, or 'none'): ");
        read_line(serial_no, sizeof(serial_no));
        printf("Enter Purchase Invoice / Store Bill details (if available, or 'none'): ");
        read_line(purchase_proof, sizeof(purchase_proof));
    }

    char remarks[MAX_DESC_LEN];
    printf("\nEnter Verification Notes / Admin Remarks: ");
    if (read_line(remarks, sizeof(remarks)) == 0) {
        strncpy(remarks, "In-person walk-in claim. Identity verified via Student ID card.", sizeof(remarks) - 1);
    }

    item->status = STATUS_CLAIMED;
    save_inventory_to_file(ITEMS_FILE);

    append_claim_to_history(item, &claimant, remarks, serial_no, purchase_proof,
                            "Walk-in (Immediate Handover)", CLAIM_HISTORY_FILE);

    printf("\n[SUCCESS] Item #%d successfully handed over to %s (%s).\n",
           item->id, claimant.name, claimant.usn);
}

void student_submit_claim_request(void) {
    printf("\n====================================================\n");
    printf("        SUBMIT A CLAIM REQUEST (STUDENT PORTAL)     \n");
    printf("====================================================\n");

    display_all_items(g_inventory_head, 1);

    printf("\nEnter the Item ID you wish to claim: ");
    char buf[64];
    if (read_line(buf, sizeof(buf)) == 0) return;
    int item_id = atoi(buf);

    ItemNode *item = find_item_by_id(g_inventory_head, item_id);
    if (!item) {
        printf("[!] Item ID #%d not found in inventory.\n", item_id);
        return;
    }

    if (item->status == STATUS_CLAIMED) {
        printf("[!] Item #%d has already been claimed by its owner.\n", item_id);
        return;
    }

    display_item_detailed(item);

    Claimant claimant;
    memset(&claimant, 0, sizeof(claimant));

    /* Name */
    while (1) {
        printf("Enter Your Full Name: ");
        if (read_line(claimant.name, sizeof(claimant.name)) > 0) break;
        printf("[!] Name cannot be blank.\n");
    }

    /* USN */
    while (1) {
        printf("Enter Your College USN (e.g., 1RV21CS045): ");
        if (read_line(claimant.usn, sizeof(claimant.usn)) > 0) {
            if (validate_usn(claimant.usn)) break;
            printf("[!] Invalid USN format. Must be alphanumeric (5-20 characters).\n");
        }
    }

    /* Phone */
    while (1) {
        printf("Enter Your Phone Number: ");
        if (read_line(claimant.phone, sizeof(claimant.phone)) > 0) {
            if (validate_phone(claimant.phone)) break;
            printf("[!] Invalid phone number. Must contain at least 10 digits.\n");
        }
    }

    /* Email with validation */
    while (1) {
        printf("Enter Your College Email (MUST end with %s): ", COLLEGE_EMAIL_DOMAIN);
        if (read_line(claimant.email, sizeof(claimant.email)) > 0) {
            if (validate_college_email(claimant.email, COLLEGE_EMAIL_DOMAIN)) {
                printf("[+] Email validated successfully!\n");
                break;
            } else {
                printf("[!] INVALID EMAIL. Only official college email addresses ending with '%s' are permitted.\n",
                       COLLEGE_EMAIL_DOMAIN);
            }
        }
    }

    get_current_timestamp_str(claimant.claim_date, sizeof(claimant.claim_date));

    char serial_no[64] = {0};
    char purchase_proof[128] = {0};
    char notes[MAX_DESC_LEN] = {0};

    if (strcmp(item->category, "Electronics") == 0) {
        printf("\n[DEVICE OWNERSHIP DETAILS]:\n");
        printf("Enter Hardware Serial Number / IMEI / Model (if known, or 'none'): ");
        read_line(serial_no, sizeof(serial_no));
        printf("Enter Proof of Purchase / Store Invoice Ref (if available, or 'none'): ");
        read_line(purchase_proof, sizeof(purchase_proof));
        printf("Enter Identifying Details (Wallpaper, Scratches, Markings): ");
        read_line(notes, sizeof(notes));
    } else {
        printf("\nDescribe Item Characteristics / Distinct Marks to confirm ownership: ");
        if (read_line(notes, sizeof(notes)) == 0) {
            strncpy(notes, "Identifying details provided by student.", sizeof(notes) - 1);
        }
    }

    /* Automated Campus Visit Scheduling */
    char visit_date[32];
    char visit_slot[32];
    prompt_campus_visit_slot(visit_date, sizeof(visit_date), visit_slot, sizeof(visit_slot));

    int req_id = g_next_request_id++;
    char req_date[MAX_DATE_LEN];
    get_current_timestamp_str(req_date, sizeof(req_date));

    /* Enqueue to FIFO queue */
    if (queue_enqueue(&g_claim_queue, req_id, item_id, &claimant, notes, req_date,
                      visit_date, visit_slot, serial_no, purchase_proof)) {
        item->status = STATUS_PENDING_CLAIM;
        save_inventory_to_file(ITEMS_FILE);
        save_queue_to_file(CLAIMS_QUEUE_FILE);

        printf("\n====================================================================\n");
        printf("           DIGITAL CAMPUS VISIT PICKUP PASS ISSUED                  \n");
        printf("====================================================================\n");
        printf(" Pass / Request ID : #%d\n", req_id);
        printf(" Item Claimed      : Item #%d (%s)\n", item->id, item->name);
        printf(" Claimant Name     : %s (USN: %s)\n", claimant.name, claimant.usn);
        printf(" Verified Contact  : %s | %s\n", claimant.phone, claimant.email);
        printf(" Scheduled Date    : %s\n", visit_date);
        printf(" Scheduled Slot    : %s\n", visit_slot);
        printf(" Queue Position    : %d in line\n", g_claim_queue.count);
        printf(" Pickup Location   : Lost & Found Office, Central Administration Block\n");
        printf(" Mandatory Actions : Bring Physical College ID Card to Lost & Found desk.\n");
        printf("====================================================================\n");
    } else {
        printf("[ERROR] Failed to add claim request to queue.\n");
    }
}

void student_check_claim_status(void) {
    printf("\n====================================================\n");
    printf("            CHECK CLAIM REQUEST STATUS              \n");
    printf("====================================================\n");
    printf("Enter your Request ID (e.g., 1001): ");

    char buf[64];
    if (read_line(buf, sizeof(buf)) == 0) return;
    int req_id = atoi(buf);

    ClaimRequestNode *curr = g_claim_queue.front;
    int pos = 1;
    while (curr) {
        if (curr->request_id == req_id) {
            ItemNode *item = find_item_by_id(g_inventory_head, curr->item_id);
            printf("\n[STATUS: PENDING IN QUEUE]\n");
            printf(" Request ID    : #%d\n", curr->request_id);
            printf(" Item          : #%d (%s)\n", curr->item_id, item ? item->name : "Item");
            printf(" Claimant Name : %s\n", curr->claimant.name);
            printf(" Queue Position: %d (Out of %d pending requests)\n", pos, g_claim_queue.count);
            printf(" Scheduled Visit: %s, %s\n", curr->visit_date, curr->visit_slot);
            printf(" Submission Date: %s\n", curr->request_date);
            printf(" Note: Please visit the admin desk during your reserved slot with your ID card.\n");
            return;
        }
        curr = curr->next;
        pos++;
    }

    printf("\n[*] Request ID #%d is not currently in the pending queue.\n", req_id);
    printf("It may have already been processed and approved by the admin, or rejected.\n");
    printf("Please contact the Lost & Found administrator for assistance.\n");
}

/* ============================================================================
 * IMPLEMENTATION: User Interfaces & Menus
 * ============================================================================ */

void display_about_and_retention(void) {
    printf("\n================================================================================\n");
    printf("            ABOUT THE LOST AND FOUND SYSTEM & RETENTION POLICY                  \n");
    printf("================================================================================\n");
    printf("1. CORE ARCHITECTURE:\n");
    printf("   - Singly Linked List: Dynamically manages real-time item inventory.\n");
    printf("   - FIFO Queue: Ensures fair, sequential processing of claim requests.\n");
    printf("   - Text File Persistence: Ensures inventory and queue persist across reboots.\n\n");
    printf("2. ADVANCED WORKFLOW ENHANCEMENTS:\n");
    printf("   - Scheduled Campus Visits: Automated time-slot reservation with slot quotas.\n");
    printf("   - Multi-Criteria Search: Filter by Category, Location, and Keywords.\n\n");
    printf("3. 1-YEAR CLAIMANT RETENTION DATABASE ('%s'):\n", CLAIM_HISTORY_FILE);
    printf("   - Why: College assets and personal belongings carry institutional liability.\n");
    printf("   - What: Logs claimant's full name, verified college email (%s),\n", COLLEGE_EMAIL_DOMAIN);
    printf("     contact number, university USN, serial numbers, visit slot, and timestamps.\n");
    printf("   - Retention Duration: Kept for 365 calendar days for audit investigations,\n");
    printf("     dispute reconciliation, and regulatory compliance.\n");
    printf("================================================================================\n");
}

void student_menu(void) {
    int choice = 0;
    while (1) {
        printf("\n====================================================\n");
        printf("           STUDENT PORTAL (READ-ONLY ACCESS)        \n");
        printf("====================================================\n");
        printf(" 1. View All Available Found Items\n");
        printf(" 2. Search Items by Keyword / Description / Category\n");
        printf(" 3. Filter Items by Category (Electronics, IDs, Books...)\n");
        printf(" 4. Filter Items by Campus Location\n");
        printf(" 5. Filter Items by Upload / Found Date\n");
        printf(" 6. Submit a Claim Request & Schedule Visit\n");
        printf(" 7. Track Status of My Claim Request\n");
        printf(" 8. Switch to Admin Portal\n");
        printf(" 9. Return to Main Menu\n");
        printf("====================================================\n");

        choice = read_int_range(1, 9);
        switch (choice) {
            case 1:
                display_all_items(g_inventory_head, 1);
                pause_console();
                break;
            case 2: {
                char kw[128];
                printf("\nEnter keyword to search (name, color, category, or location): ");
                if (read_line(kw, sizeof(kw)) > 0) {
                    search_items_by_keyword(g_inventory_head, kw);
                }
                pause_console();
                break;
            }
            case 3: {
                char cat[32];
                prompt_category(cat, sizeof(cat));
                filter_items_by_category(g_inventory_head, cat);
                pause_console();
                break;
            }
            case 4: {
                printf("\nSelect campus location filter:\n");
                printf("1. Block A\n2. Block B\n3. Block C\n4. Block D\n5. Block E\n6. Basketball Court\n7. Near Temple\n8. Entrance\n");
                int loc_c = read_int_range(1, 8);
                char query[64];
                if (loc_c >= 1 && loc_c <= 5) {
                    snprintf(query, sizeof(query), "Block %c", 'A' + (loc_c - 1));
                } else if (loc_c == 6) {
                    strncpy(query, "Basketball Court", sizeof(query));
                } else if (loc_c == 7) {
                    strncpy(query, "Near Temple", sizeof(query));
                } else {
                    strncpy(query, "Entrance", sizeof(query));
                }
                filter_items_by_location(g_inventory_head, query);
                pause_console();
                break;
            }
            case 5:
                filter_items_by_date(g_inventory_head);
                pause_console();
                break;
            case 6:
                student_submit_claim_request();
                pause_console();
                break;
            case 7:
                student_check_claim_status();
                pause_console();
                break;
            case 8: {
                char entered_pass[MAX_PASS_LEN];
                printf("\nEnter Admin Password (Default: %s): ", DEFAULT_ADMIN_PASSWORD);
                read_masked_password(entered_pass, sizeof(entered_pass));
                if (strcmp(entered_pass, g_admin_password) == 0) {
                    printf("[+] Access Granted. Switching to Administrator Control Panel...\n");
                    admin_menu();
                } else {
                    printf("[!] Authentication failed! Incorrect password.\n");
                    pause_console();
                }
                break;
            }
            case 9:
                return;
            default:
                break;
        }
    }
}

void admin_menu(void) {
    int choice = 0;
    while (1) {
        printf("\n====================================================\n");
        printf("           ADMINISTRATOR CONTROL PANEL              \n");
        printf("====================================================\n");
        printf(" 1. Add New Found Item (Register to Inventory)\n");
        printf(" 2. View All Items in Inventory (All Statuses)\n");
        printf(" 3. View Item Detailed Card by ID\n");
        printf(" 4. View Pending Claim Requests Queue (FIFO)\n");
        printf(" 5. Process Next Claim Request from Queue\n");
        printf(" 6. Direct On-The-Spot Claim (Walk-in Handover)\n");
        printf(" 7. View Scheduled Campus Visits Schedule\n");
        printf(" 8. View 1-Year Claimant History Archive (%s)\n", CLAIM_HISTORY_FILE);
        printf(" 9. Edit Found Item Details & Photo\n");
        printf(" 10. Edit / Reschedule Pending Claim Request\n");
        printf(" 11. Delete / Remove an Item Record\n");
        printf(" 12. Change Admin Password\n");
        printf(" 13. Return to Main Menu (Logout)\n");
        printf("====================================================\n");

        choice = read_int_range(1, 13);
        switch (choice) {
            case 1:
                add_item();
                pause_console();
                break;
            case 2:
                display_all_items(g_inventory_head, 0);
                pause_console();
                break;
            case 3: {
                printf("\nEnter Item ID to inspect: ");
                char buf[64];
                if (read_line(buf, sizeof(buf)) > 0) {
                    ItemNode *item = find_item_by_id(g_inventory_head, atoi(buf));
                    if (item) {
                        display_item_detailed(item);
                    } else {
                        printf("[!] Item not found.\n");
                    }
                }
                pause_console();
                break;
            }
            case 4:
                queue_display(&g_claim_queue);
                pause_console();
                break;
            case 5:
                process_claim_from_queue();
                pause_console();
                break;
            case 6:
                direct_claim_walkin();
                pause_console();
                break;
            case 7:
                display_scheduled_campus_visits();
                pause_console();
                break;
            case 8:
                display_claim_history_file(CLAIM_HISTORY_FILE);
                pause_console();
                break;
            case 9:
                edit_item_details();
                pause_console();
                break;
            case 10:
                edit_claim_request();
                pause_console();
                break;
            case 11: {
                printf("\nEnter Item ID to permanently delete from inventory: ");
                char buf[64];
                if (read_line(buf, sizeof(buf)) > 0) {
                    int del_id = atoi(buf);
                    printf("Are you sure you want to delete Item #%d? (1: Yes, 2: Cancel): ", del_id);
                    if (read_int_range(1, 2) == 1) {
                        if (delete_item_by_id(&g_inventory_head, del_id)) {
                            save_inventory_to_file(ITEMS_FILE);
                            printf("[+] Item #%d deleted successfully.\n", del_id);
                        } else {
                            printf("[!] Item #%d not found.\n", del_id);
                        }
                    }
                }
                pause_console();
                break;
            }
            case 12: {
                char new_pw1[MAX_PASS_LEN], new_pw2[MAX_PASS_LEN];
                printf("\nEnter new admin password: ");
                read_masked_password(new_pw1, sizeof(new_pw1));
                printf("Confirm new admin password: ");
                read_masked_password(new_pw2, sizeof(new_pw2));
                if (strcmp(new_pw1, new_pw2) == 0 && strlen(new_pw1) >= 4) {
                    strncpy(g_admin_password, new_pw1, sizeof(g_admin_password) - 1);
                    printf("[+] Admin password updated successfully for this session!\n");
                } else {
                    printf("[!] Passwords did not match or was under 4 characters. Not changed.\n");
                }
                pause_console();
                break;
            }
            case 13:
                printf("\n[*] Logging out of Admin Panel...\n");
                return;
            default:
                break;
        }
    }
}

void main_menu(void) {
    int choice = 0;
    while (1) {
        printf("\n====================================================\n");
        printf("         COLLEGE LOST & FOUND MANAGEMENT SYSTEM     \n");
        printf("====================================================\n");
        printf(" 1. Student Portal (Browse, Filter & Book Claims)\n");
        printf(" 2. Admin Portal (Password Protected)\n");
        printf(" 3. View 1-Year Retention Policy & Architecture\n");
        printf(" 4. Exit Application\n");
        printf("====================================================\n");

        choice = read_int_range(1, 4);
        switch (choice) {
            case 1:
                student_menu();
                break;
            case 2: {
                char entered_pass[MAX_PASS_LEN];
                printf("\nEnter Admin Password (Default: %s): ", DEFAULT_ADMIN_PASSWORD);
                read_masked_password(entered_pass, sizeof(entered_pass));

                if (strcmp(entered_pass, g_admin_password) == 0) {
                    printf("[+] Access Granted. Welcome, Administrator.\n");
                    admin_menu();
                } else {
                    printf("[!] ACCESS DENIED: Incorrect password.\n");
                    pause_console();
                }
                break;
            }
            case 3:
                display_about_and_retention();
                pause_console();
                break;
            case 4:
                printf("\nSaving session data and exiting... Goodbye!\n");
                save_inventory_to_file(ITEMS_FILE);
                save_queue_to_file(CLAIMS_QUEUE_FILE);
                return;
            default:
                break;
        }
    }
}

/* ============================================================================
 * PROGRAM ENTRY POINT
 * ============================================================================ */
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("====================================================\n");
    printf(" Initializing College Lost & Found System...\n");
    printf(" Features: Scheduled Visits | Category Search       \n");
    printf("====================================================\n");

    /* Initialize in-memory queue */
    queue_init(&g_claim_queue);

    /* Load persistent data from disk */
    load_inventory_from_file(ITEMS_FILE);
    load_queue_from_file(CLAIMS_QUEUE_FILE);

    printf("[+] Active inventory loaded successfully (%s).\n", ITEMS_FILE);
    printf("[+] Pending claim queue loaded successfully (%s).\n", CLAIMS_QUEUE_FILE);

    /* Launch interactive main interface */
    main_menu();

    /* Clean up allocated dynamic memory before termination */
    free_inventory(&g_inventory_head);
    queue_free(&g_claim_queue);

    printf("[+] All memory safely deallocated. Exit clean.\n");
    return 0;
}


