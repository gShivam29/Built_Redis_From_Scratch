# RedisLite: A Redis Clone Built From Scratch

Experience the power of in-memory databases, rebuilt from the ground up. **RedisLite** is a lightweight, blazing-fast database inspired by Redis, featuring custom-built data structures, persistent storage, and sorted sets. All handcrafted, no external DB engines used.
Built using C and C++.


---

## ✨ Features at a Glance
- 🗂 **Custom Data Structures** — Hash tables, sorted sets (ZSETs) and AVL trees implemented from scratch.
- ⚡ **In-Memory & Fast** — Just like Redis, responses are serialized and lightning-quick.
- 🔄 **Polling & Pub/Sub** — Real-time updates powered by custom polling logic.
- 💾 **Persistent Storage** — Keeps your data safe beyond runtime.
- 🔒 **GET, SET, DEL Supported** — Basic commands fully functional.
- 🆚 **Redis-Like Commands** — Familiar feel with a unique, ground-up implementation. Supports core Redis commands (GET, SET, DEL) and sorted set queries (ZADD, ZSCORE, ZRANGE).

---

## 📸 Demo
**Start**

> <img width="959" alt="start" src="https://github.com/user-attachments/assets/e1632511-07fa-4bb1-9a89-90ea54548de3" />

**GET, SET, DEL**

> <img width="959" alt="dml" src="https://github.com/user-attachments/assets/cecec6ac-b4ff-41db-9d2b-9a0a581009a4" />

**ZADD, ZSCORE, ZRANGE**

> <img width="959" alt="sorted_set" src="https://github.com/user-attachments/assets/290eab1d-9da7-4cd1-93b2-b812c0b49672" />

**Concurrent Clients**

> <img width="959" alt="multiple_users" src="https://github.com/user-attachments/assets/f9ae01ef-0208-481c-b6b6-7d7e3236b763" />

---

## 🛠 Technical Breakdown
- **Custom Parsing Protocol** — No libraries, just raw parsing.
- **Custom Hashtable Engine** — Tailored for speed and control.
- **AVL Tree for Sorted Sets (ZSETs)** — Ensures fast insertions and lookups.
- **Serialized Responses** — Matching real-world database behavior.
- **Polling System** — Built from scratch for real-time data sync.
- **Handles multiple concurrent users** — Using non-blocking I/O and an event loop. Uses polling to serve many clients simultaneously — just like real-world databases.
- **Command Line Interface** — Custom CLI for a better user experience.

---

## 🎯 Why I Built This
This project was built to deepen my understanding of low-level programming and to explore the real-world application of data structures. It helped me gain a deeper understanding of how databases like Redis work and proved that powerful systems can be implemented using core computer science concepts like hashing, trees, and serialization.
A huge shoutout to **J. Smith's book**, which served as a cornerstone and guide throughout this project.

---

## ✅ Current Commands Supported
- `GET`
- `SET`
- `DEL`
- `ZADD` (Sorted Sets)
- `ZSCORE` (Get the score of a member of a key)
- `ZRANGE` (Retrieve sorted data)
*(More commands coming soon!)*

---

## 📚 Who Can Explore This?
- **Tech Enthusiasts** — Learn how real databases work under the hood.
- **Non-Technical** — Experience RedisLite through the simple demos given above.
- **Students & Engineers** — See data structures like hashtables and AVL trees in real-world use.

---

## 🚩 Try the Demo  
```bash
# Clone the project
git clone https://github.com/gShivam29/Built_Redis_From_Scratch.git
cd Built_Redis_From_Scratch

# Run the server
./11_server
./11_client

```
⚠️ **Note:** This project is compatible only with Linux-based operating systems, as it relies on the sys/socket.h library, which is specific to Linux/Unix environments. You can also use WSL, this project was developed using WSL.
