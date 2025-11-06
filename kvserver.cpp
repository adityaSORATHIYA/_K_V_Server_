
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

class LRUCache {
    size_t capacity;
    list<pair<int, string>> kvcache;
    unordered_map<int, list<pair<int, string>>::iterator> kvmap;
    mutable mutex mtx;

public:
    LRUCache(size_t cap) 
    {
        capacity = cap;
    }

    void put(int key, const string& value) 
    {
        lock_guard<mutex> lock(mtx);

        // Move to front if exists
        auto it = kvmap.find(key);
        if (it != kvmap.end()) 
        {
            kvcache.erase(it->second);
        }

        kvcache.push_front({key, value});
        kvmap[key] = kvcache.begin();

        // Remove least recently used
        if (kvcache.size() > capacity) 
        {
            auto last = kvcache.back();
            kvmap.erase(last.first);
            kvcache.pop_back();
        }
    }

    bool get(int key, string& value) 
    {
        lock_guard<mutex> lock(mtx);
        auto it = kvmap.find(key);
        if (it == kvmap.end())
        { 
            return false;
        }

        value = it->second->second;

        // MOVE TO FRONT
        kvcache.erase(it->second);
        kvcache.push_front({key, value});
        kvmap[key] = kvcache.begin();
        return true;
    }

    void remove(int key) {
        lock_guard<mutex> lock(mtx);
        auto it = kvmap.find(key);
        if (it == kvmap.end())
        {
            return;
        }
        kvcache.erase(it->second);
        kvmap.erase(it);
    }
};

static const char* DB_HOST = "localhost";
static const unsigned DB_PORT = 33060;
static const char* DB_USER = "root";
static const char* DB_PASS = "Aditya1234";
static const char* DB_SCHEMA = "kv_db";

mysqlx::Session* create_session() {
    try {
        return new mysqlx::Session(DB_HOST, DB_PORT, DB_USER, DB_PASS, DB_SCHEMA);
    }
    catch (...) {
        cerr << "Cannot connect to database\n";
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
    }
    catch (...) {
        cerr << "DB Create/Update failed\n";
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
    } 
    catch (...) {
        cerr << "DB read failed\n";
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
    }
    catch (...) {
        cerr << "DB delete failed\n";
        return false;
    }
}

LRUCache cache(100);

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

    cout << " Server running on port 8080 with " << threads << " threads.\n";
    app.port(8080).concurrency(threads).run();

    return 0;
}
