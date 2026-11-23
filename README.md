# College Lost and Found Management System (Pure C & Modern Web)

A comprehensive, enterprise-grade **Lost and Found Management System** engineered in pure C (C99 standard) with both a terminal console interface and a responsive, high-performance web dashboard. Designed specifically for collegiate environments to manage found property, streamline student claim workflows, schedule campus pickup appointments, and maintain an immutable statutory **1-Year Claimant Retention Database**.

---

## 1. System Architecture & Data Structures

| Component | Data Structure | Purpose |
| :--- | :--- | :--- |
| **Inventory Store** | **Singly Linked List (`ItemNode*`)** | Dynamically stores and manages active and found items. Enables $O(1)$ insertions, dynamic memory scaling, and rapid traversal, keyword searching, category filtering, and location matching. |
| **Claim Requests Queue** | **FIFO Queue (`ClaimQueue`)** | Singly-linked queue (`front` & `rear` pointers) enforcing first-come, first-served claim processing, preventing queue-jumping, and scheduling appointments. |
| **Item Persistence** | Flat file (`items.txt`) | Delimited storage for item records across restarts. |
| **Pending Queue File** | Flat file (`claims_queue.txt`) | Delimited storage preserving claim requests, appointment time slots, and verification records. |
| **1-Year Retention Archive** | Append-only Audit Log (`claim_history.txt`) | Institutional audit database logging claimant details, verified college email, serial numbers, proof of purchase, and visit records for statutory 365-day compliance. |

---

## 2. Key Modules & Features

### 1. 📅 Scheduled Campus Visits (Automated Appointment Booking)
- **Time Slot Management**: Automated scheduling module offering 6 daily 30-minute pickup windows:
  - `09:30 AM - 10:00 AM`
  - `10:30 AM - 11:00 AM`
  - `11:30 AM - 12:00 PM`
  - `02:00 PM - 02:30 PM`
  - `03:00 PM - 03:30 PM`
  - `04:00 PM - 04:30 PM`
- **Real-Time Capacity Quotas**: Enforces a capacity limit of 3 students per slot to eliminate campus crowding.
- **Digital Campus Visit Pass**: Generates an appointment confirmation pass with reference ID, pickup location, and preparation instructions.
- **Admin Desk Schedule**: Administrators can inspect all scheduled visits organized by date.

### 2. 🔍 Advanced Filters (Categories & Upload Dates)
- **Hierarchical Taxonomy**:
  - `Electronics`
  - `IDs & Cards`
  - `Books & Stationery`
  - `Bags & Bottles`
  - `Personal Accessories`
  - `Other`
- **Upload Date Filters**:
  - `All Upload Dates`
  - `Uploaded Today`
  - `Uploaded Past 3 Days`
  - `Uploaded Past 7 Days`
  - `Uploaded Past 30 Days`
  - `Specific Custom Date Picker` (exact calendar match)
- **Multi-Parameter Search**: Instant filtering combining category quick-chips, campus location blocks (`Block A`–`Block E` with room numbers, `Basketball Court`, `Near Temple`, `Entrance`), upload dates, and item availability status.

### 3. 🖼️ Actual Photo Uploads & Student Photo Inspection
- **Drag-and-Drop Image Uploader**: Administrators can upload actual picture files (JPG, PNG, WEBP) directly from their computer when adding or editing found items. Images are converted to high-quality data URIs stored with item records.
- **Student Photo Review & Lightbox**: Every item card displays a thumbnail photo. Clicking any photo opens the **High-Resolution Lightbox Viewer** allowing students to closely inspect condition, scratches, distinct stickers, and colors.

### 4. 🏛️ Welcome Front Page (Role Selector)
- **Landing Gateway**: Visitors land on a modern portal selection page allowing immediate choice between:
  - **Student Portal**: Direct 1-click open access to browse belongings, review photos, and submit claims.
  - **Admin & Staff Portal**: Password-protected entrance (`admin123`) to manage campus inventory, review queues, and audit archives.
- **Universal Role Switcher**: A "Home / Roles" and "← Switch Role" button in the top navigation allows switching views anytime.

### 5. ✏️ Post-Creation Item & Claim Editing (Admin)
- **Edit Found Items**: Administrators can edit item names, categories, descriptions, campus locations/rooms, photos, and upload dates at any time from the inventory directory.
- **Edit & Reschedule Claims**: Administrators can update claimant details, edit proof notes, or reschedule appointment visit dates and 30-min time slots directly from the Claims Queue or Scheduled Visits agenda.

### 6. 🔒 Verified Student Claim Workflow
- **Domain Verification**: Enforces college-issued email validation (`@college.edu`).
- **Electronic Device Verification**: Captures hardware serial numbers, IMEI, and purchase proofs for devices to ensure accurate matching.
- **Physical Handover Checklist**: Admins verify student ID and device credentials before confirming item disbursement.

---

## 3. Compilation & Execution

### Option A: Pure C Console Application (`main.c`)
Compile the interactive console program:
```bash
gcc -Wall -Wextra -std=c99 main.c -o lost_found.exe
```
Run the console program:
```powershell
.\lost_found.exe
```

### Option B: Pure C Web Server (`server.c`)
Compile the native WinSock2 HTTP server:
```bash
gcc -Wall -Wextra -std=c99 server.c -lws2_32 -o server.exe
```
Run the web server:
```powershell
.\server.exe
```
Then open `http://localhost:8080/` in your browser.

### Option C: Direct Standalone Web Dashboard
The web frontend at `web/index.html` (or the artifact `lost_and_found_ui.html`) can also be opened directly in any modern browser. It automatically detects server availability and runs a complete linked-list and queue simulation client-side.

---

## 4. Institutional Security & Retention Policy
- **Authorized Email Suffix**: `@college.edu` (enforced on all student claim requests).
- **Default Admin Password**: `admin123` (customizable in-session).
- **Statutory 1-Year Retention**: Recorded in `claim_history.txt` with timestamped transaction records retained for 365 calendar days for audit reconciliation and legal compliance.
