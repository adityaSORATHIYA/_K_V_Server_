# Key-Value Store Service (Crow + MySQL + LRU Cache)

This project is a Key-Value store web service written in C++ using:
- **Crow** web framework for HTTP REST APIs
- **MySQL X DevAPI** as the database
- **In-memory LRU Cache** for performance

## Features
- Create/Update key-value pairs
- Read key-value pair (cache-first)
- Delete key
- Thread-safe caching
- Per-thread MySQL connections

-------------------------------------------------------------------------------------------------------------

### Install MySQL X Protocol
Inside MySQL shell:
```sql
ALTER USER 'root'@'localhost' IDENTIFIED WITH mysql_native_password BY 'yourpassword';
INSTALL COMPONENT 'file://component_mysqlx';
```
Make sure MySQL X Plugin is ON:
```bash
mysqlx --version
```

Make sure you edit DB username/password in code if needed.

## Database Setup
This service expects MySQL running with X Protocol enabled (port `33060`).

Use this SQL:
```sql
CREATE DATABASE IF NOT EXISTS kv_db;
USE kv_db;
CREATE TABLE IF NOT EXISTS kv_table (
  k INT PRIMARY KEY,
  v TEXT
);
```

-------------------------------------------------------------------------------------------------------------

### Compile Example
```bash
g++ kvserver.cpp \
    -std=c++17 \
    -Icrow/include \
    -I. \
    -I/opt/homebrew/opt/openssl@3/include \
    -I/opt/homebrew/Cellar/mysql-connector-c++/9.4.0/include \
    -L/opt/homebrew/Cellar/mysql-connector-c++/9.4.0/lib \
    -L/opt/homebrew/opt/openssl@3/lib \
    -lmysqlcppconnx \
    -lssl -lcrypto -lpthread \
    -o kvserver

### Run Server
```bash
./server <thread_pool_size>
# example
./server 4
```

Server runs on **port 8080**.

Compile your C++ program and run with thread pool size argument, e.g:

```bash
./server 4
```

The server will run on port `8080`.

-------------------------------------------------------------------------------------------------------------

Example requests using curl:
```bash
# Create
curl -X POST localhost:8080/create -H "Content-Type: application/json" -d '{"key":1,"value":"hi"}'

# Read
curl localhost:8080/read/1

# Delete
curl -X DELETE localhost:8080/delete/1
```
-------------------------------------------------------------------------------------------------------------

## API Endpoints

### 1) Create/Update a Key
**POST /create**
```json
{
  "key": 1,
  "value": "hello world"
}
```
Response:
- `200 Created` if success
- `500 Database Error` if DB failed

-------------------------------------------------------------------------------------------------------------

### 2) Read a Key
**GET /read/<int>**

Example:
```
GET /read/1
```
Response:
- `200 <value>` if found
- `404 Key not found` if key does not exist

-------------------------------------------------------------------------------------------------------------

### 3) Delete a Key
**DELETE /delete/<int>**

Example:
```
DELETE /delete/1
```
Response:
- `200 Deleted` on success
- `500 Database Error` on failure

-------------------------------------------------------------------------------------------------------------

## Architecture Flow

This system uses a **read-through + write-through caching strategy**.

 ┌──────────────┐          ┌───────────────────────┐          ┌─────────────────┐
 │   CLIENT     │  HTTP    │   CROW WEB SERVER     │  C++ API │   LRU CACHE     │
 │(Postman/curl)│ ───────► │(Routes: create/read/..│ ───────► │  (in-memory)    │
 └──────────────┘          └─────────▲─────────────┘          └───────▲─────────┘
                                      │                                 │
                                      │ cache MISS                      │ cache HIT
                                      │                                 │
                                      │                                 │ return value
                                      │                                 │
                            ┌─────────┴───────────┐
                            │   MYSQL DATABASE    │
                            │   (Persistent KV)   │
                            └─────────────────────┘


-------------------------------------------------------------------------------------------------------------

### What happens on read
1. Client sends GET `/read/<key>`
2. App checks LRU Cache
3. If present → return instantly
4. If not present
   - read from DB
   - insert into Cache
   - return response
Client ----HTTP----> Crow Handler ---> Cache Lookup
                                   ├--> hit -> return
                                   └--> miss -> DB -> Cache update -> return

-------------------------------------------------------------------------------------------------------------

### What happens on write
1. Client sends POST `/create`
2. App writes to DB
3. If DB write success → update cache
4. Return status

-------------------------------------------------------------------------------------------------------------



