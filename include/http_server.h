
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <ctime>
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

#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

class http_server {

public:

    // default constructor
    http_server(int port, bool https = false, const char *cert_file = nullptr, const char *key_file = nullptr);

    // virtual destructor to allow subclass cleanup
    virtual ~http_server();

    // construct context and listener
    void construct();
    
    // listen for requests
    void listen();
    
protected:

    // server parameters
    int port;
    bool https;
    char *cert_file;
    char *key_file;

    // server structure
    route_node_t *root_ptr;
    std::vector<route_node_t*> route_nodes;

    // client variables
    int      g_listen_fd = -1;
    SSL_CTX* g_ctx       = nullptr;
    //std::ostringstream error_ss;

    virtual void construct_route_tree() {}

private:

    // Respond to a client request
    void handle_client(SSL* ssl, route_node_t *root_ptr);

};

#endif // __HTTP_SERVER_H__
