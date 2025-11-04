
#include "crow/app.h"
#include <mysqlx/xdevapi.h>
#include "json.hpp"
#include <iostream>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <list>
#include <string>

using namespace std;
using json = nlohmann::json;

// =============================
// Simple Thread-safe LRU Cache
// =============================
class LRUCache {
    size_t capacity_;
    list<pair<int, string>> cache_;
    unordered_map<int, list<pair<int, string>>::iterator> index_;
    mutable mutex mtx_;

public:
    explicit LRUCache(size_t cap) : capacity_(cap) {}

    void put(int key, const string& value) {
        lock_guard<mutex> lock(mtx_);

        // Move to front if exists
        auto it = index_.find(key);
        if (it != index_.end()) {
            cache_.erase(it->second);
        }

        cache_.push_front({key, value});
        index_[key] = cache_.begin();

        // Remove least recently used
        if (cache_.size() > capacity_) {
            auto last = cache_.back();
            index_.erase(last.first);
            cache_.pop_back();
        }
    }

    bool get(int key, string& value) {
        lock_guard<mutex> lock(mtx_);
        auto it = index_.find(key);
        if (it == index_.end()) return false;

        // Move accessed item to front
        cache_.splice(cache_.begin(), cache_, it->second);
        value = it->second->second;
        return true;
    }

    void remove(int key) {
        lock_guard<mutex> lock(mtx_);
        auto it = index_.find(key);
        if (it == index_.end()) return;
        cache_.erase(it->second);
        index_.erase(it);
    }
};

// =============================
// Database Configuration
// =============================
static const char* DB_HOST = "localhost";
static const unsigned DB_PORT = 33060;
static const char* DB_USER = "root";
static const char* DB_PASS = "Aditya1234";
static const char* DB_SCHEMA = "kv_db";

// =============================
// Per-thread MySQL Session
// =============================
mysqlx::Session* create_session() {
    try {
        return new mysqlx::Session(DB_HOST, DB_PORT, DB_USER, DB_PASS, DB_SCHEMA);
    } catch (const mysqlx::Error& err) {
        cerr << "[DB] Connection error: " << err.what() << endl;
        return nullptr;
    } catch (...) {
        cerr << "[DB] Unknown connection error" << endl;
        return nullptr;
    }
}

mysqlx::Session* thread_session() {
    thread_local mysqlx::Session* session = nullptr;
    if (!session) {
        session = create_session();
    }
    return session;
}

// =============================
// Database Operations
// =============================
bool db_create_or_update(int key, const string& value) {
    mysqlx::Session* s = thread_session();
    if (!s) return false;

    try {
        auto schema = s->getSchema(DB_SCHEMA);
        auto table = schema.getTable("kv_table");

        // Check if key exists
        auto res = table.select("k").where("k = :key").bind("key", key).execute();
        if (res.fetchOne())
            table.update().set("v", value).where("k = :key").bind("key", key).execute();
        else
            table.insert("k", "v").values(key, value).execute();

        return true;
    } catch (const mysqlx::Error& err) {
        cerr << "[DB] create/update error: " << err.what() << endl;
        return false;
    }
}

bool db_read(int key, string& value) {
    mysqlx::Session* s = thread_session();
    if (!s) return false;

    try {
        auto schema = s->getSchema(DB_SCHEMA);
        auto table = schema.getTable("kv_table");
        auto res = table.select("v").where("k = :key").bind("key", key).execute();
        auto row = res.fetchOne();
        if (!row) return false;
        value = row[0].get<string>();
        return true;
    } catch (const mysqlx::Error& err) {
        cerr << "[DB] read error: " << err.what() << endl;
        return false;
    }
}

bool db_delete(int key) {
    mysqlx::Session* s = thread_session();
    if (!s) return false;

    try {
        auto schema = s->getSchema(DB_SCHEMA);
        auto table = schema.getTable("kv_table");
        table.remove().where("k = :key").bind("key", key).execute();
        return true;
    } catch (const mysqlx::Error& err) {
        cerr << "[DB] delete error: " << err.what() << endl;
        return false;
    }
}

// =============================
// Global Cache
// =============================
LRUCache cache(100);

// =============================
// Crow Web Server
// =============================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <thread_pool_size>\n";
        return 1;
    }

    int threads = stoi(argv[1]);
    crow::SimpleApp app;

    // POST /create
    CROW_ROUTE(app, "/create").methods(crow::HTTPMethod::POST)(
        [](const crow::request& req) {
            try {
                auto j = json::parse(req.body);
                if (!j.contains("key") || !j.contains("value"))
                    return crow::response(400, "Invalid JSON");

                int key = j["key"].get<int>();
                string value = j["value"].get<string>();

                bool ok = db_create_or_update(key, value);
                if (ok) cache.put(key, value);

                return crow::response(ok ? 200 : 500, ok ? "Created" : "Database Error");
            } catch (...) {
                return crow::response(400, "Invalid JSON format");
            }
        });

    // GET /read/<int>
    CROW_ROUTE(app, "/read/<int>")
    ([](int key) {
        string value;
        bool ok = false;

        if (cache.get(key, value))
            ok = true;
        else if (db_read(key, value)) {
            ok = true;
            cache.put(key, value);
        }

        return crow::response(ok ? 200 : 404, ok ? value : "Key not found");
    });

    // DELETE /delete/<int>
    CROW_ROUTE(app, "/delete/<int>").methods(crow::HTTPMethod::DELETE)(
        [](int key) {
            bool ok = db_delete(key);
            if (ok) cache.remove(key);
            return crow::response(ok ? 200 : 500, ok ? "Deleted" : "Database Error");
        });

    cout << "🚀 Server running on port 8080 with " << threads << " threads.\n";
    app.port(8080).concurrency(threads).run();

    return 0;
}
