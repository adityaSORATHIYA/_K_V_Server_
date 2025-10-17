#include<iostream>
#include "crow.h"
using namespace std;
int main() {
    crow::SimpleApp app;
    CROW_ROUTE(app,"/")([](){
        return "Hello Crow !!!";
    });
    app.port(8080).multithreaded().run();
}
