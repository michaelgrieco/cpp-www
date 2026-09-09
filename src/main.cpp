#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <map>
#include <vector>

#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "http.h"
#include "route.h"
#include "ssl.h"
#include "schema.h"
#include "home_http_server.h"

static constexpr int    DEFAULT_PORT  = 8443;
static constexpr char   CERT_FILE[]   = "certs/server.crt";
static constexpr char   KEY_FILE[]    = "certs/server.key";

home_http_server *www;

// Exit point
static void shutdown_handler(int /*sig*/) {
    std::cout << "Shutdown handler" << std::endl;
    if (www) {
        delete www;
    }
    std::exit(0);
}

// Entry point
int main(int argc, char* argv[]) {
    int port = DEFAULT_PORT;
    if (argc == 2) port = std::atoi(argv[1]);

    www = new home_http_server(port, true, CERT_FILE, KEY_FILE);

    std::signal(SIGINT, shutdown_handler);

    www->construct();
    www->listen();

    shutdown_handler(0);
    return 0;
}
