
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

#define MAX_REQ_SIZE (1<<20)

// Virtual base for HTTP server
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

    // Overridable function to customize the not found response
    virtual void not_found(http_request_t request);
    virtual void error(http_request_t request, http_status_t status, std::string msg);
    
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

// request information
typedef struct http_request_t {
    SSL                                *ssl;
    http_server                        *server;
    std::map<std::string, std::string> header_vars;
    variables_t                        vars;
    int                                bytes_read;
    std::string                        body;
    std::time_t                        req_time;
} http_request_t;

// read bytes
int read_ssl_request(http_request_t &request, char *buf, int max_segment);

// respond with a generic error
void respond_error(http_request_t request, http_status_t status, std::string msg);

// respond not found (HTTP 404) to a request
void respond_not_found(http_request_t request);

#endif // __HTTP_SERVER_H__
