# College Lost and Found Management System (Pure C)

A robust, console-based **Lost and Found Management System** engineered in pure C (C99 standard). Designed specifically for collegiate environments to manage found property, streamline student claim requests, enforce identity verification, and maintain a statutory **1-Year Claimant Retention Database**.

---

## 1. System Architecture & Data Structures

| Component | Data Structure | Purpose |
| :--- | :--- | :--- |
| **Inventory Store** | **Singly Linked List (`ItemNode*`)** | Dynamically stores and manages current lost & found items. Allows $O(1)$ insertions, dynamic scaling without fixed-array constraints, and efficient traversal, searching, and filtering. |
| **Claim Requests Queue** | **FIFO Queue (`ClaimQueue`)** | Front-and-rear linked queue that sequences claim requests on a first-come, first-served basis for admin verification. |
| **Item Persistence** | Flat file (`items.txt`) | Delimited storage for item records across restarts. |
| **Pending Queue File** | Flat file (`claims_queue.txt`) | Saves pending claim requests to preserve queue state across reboots. |
| **1-Year Retention Archive** | Append-only Audit Log (`claim_history.txt`) | Statutory audit database recording claimant details (Name, USN, Phone, College Email) and verification timestamps. |

---

## 2. Key Features

- **Role-Based Access Control**:
  - **Admin Interface (Password Protected)**: Default password `admin123`. Allows registering new items, viewing all items, reviewing FIFO claim requests, performing direct walk-in handovers, managing inventory records, and inspecting the 1-year audit archive.
  - **Student Interface (Read-Only)**: Students can view available items, search by keyword/color/brand, filter by campus locations, and submit formal claim requests into the queue.
- **Location Sub-Menu & Validation**:
  - Structured location taxonomy: `Block A`, `Block B`, `Block C`, `Block D`, `Block E` (with mandatory prompt for Room/Lab number), `Basketball Court`, `Near Temple`, and `Entrance`.
- **Image File Path Logging**:
  - Dedicated field tracking the absolute or relative disk path to the image of the object (e.g., `C:/images/watch.jpg`).
- **Claimant Validation**:
  - **Email Validation**: Enforces college email domain check (e.g., must end with `@college.edu`).
  - **USN & Phone Validation**: Validates alphanumeric university seat number (USN) and phone digit requirements.
- **1-Year Retention Policy**:
  - Each disbursement is written to `claim_history.txt` with UTC/local timestamp and explicit retention notice for annual college audits and dispute resolution.

---

## 3. Compilation & Execution

### Prerequisites
- Any C99-compliant compiler (`gcc`, `clang`, or MSVC).

### Compile with GCC (MinGW / Linux / macOS)
```bash
gcc -Wall -Wextra -std=c99 main.c -o lost_found
```

### Run
**On Windows:**
```cmd
lost_found.exe
```

**On Linux / macOS:**
```bash
./lost_found
```

---

## 4. Default Credentials
- **Admin Password**: `admin123` (can be updated in-session via the Admin Control Panel).
- **Authorized Email Suffix**: `@college.edu` (configured via `COLLEGE_EMAIL_DOMAIN` constant).

