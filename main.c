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




