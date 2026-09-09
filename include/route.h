
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <map>
#include <vector>

#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#ifndef __ROUTE_H__
#define __ROUTE_H__

// Forward declarations
struct route_node_t;
struct route_variable_t;
class http_server;

// Types of variables in a URL route node
enum route_variable_type_e {
    STATIC = 0,
    INT,
    FLOAT,
    STRING,
    LIST,
    MAP,
};

// URL variable value
typedef union route_variable_value_t {
    int i;
    float f;
    double d;
    std::string s;
    std::vector<route_variable_t> *l;
    std::map<std::string, route_variable_t> *m;

    route_variable_value_t() {}
    ~route_variable_value_t() {}
} route_variable_value_t;

// URL variable
class route_variable_t {
public:
    route_variable_type_e type;
    route_variable_value_t value;

    // default constructors and destructor
    route_variable_t();
    route_variable_t(const route_variable_t& o);
    ~route_variable_t();

    // construct empty variable with type
    route_variable_t(route_variable_type_e type);

    // construct variable from value
    route_variable_t(int i);
    route_variable_t(float f);
    route_variable_t(std::string s);
    route_variable_t(char *s);
    route_variable_t(std::vector<route_variable_t> *l);
    route_variable_t(std::map<std::string, route_variable_t> *m);

    // null or empty check
    bool is_null();

    // assignment operator
    route_variable_t& operator=(const route_variable_t& o);

    // stringify the variable
    std::string to_string(bool wrap_in_quotes = false);
};

// Mapping of name to variable in a URL
typedef std::pair<std::string, route_variable_t> variable_entry_t;
typedef std::map<std::string, route_variable_t> variables_t;

// request information
typedef struct {
    SSL         *ssl;
    http_server *server;
    variables_t  vars;
    std::string  body;
    std::time_t  req_time;
} http_request_t;

// URL callback function
typedef void(*callback_t)(http_request_t request);
#define CALLBACK_USE_VARS() \
    (void)server; \
    (void)ssl; \
    (void)vars; \
    (void)body;

// Mapping of request method to callback
typedef std::map<std::string, callback_t> callbacks_t;

// Piece of a URL path
typedef struct route_node_t {
    bool is_static;
    route_variable_t variable;
    std::string name;
    std::vector<struct route_node_t*> children;
    callbacks_t callbacks;
    bool is_final_node = false;
} route_node_t;

// Parse a variable from text
route_variable_t parse_var(std::string arg);

// Create a node in the route tree
route_node_t *create_route_node(std::vector<route_node_t*> *route_nodes, route_node_t *parent, std::string name, bool is_static, route_variable_type_e var_type);

// Create a static node that terminates URL parsing and stores the rest of the URL with a variable
route_node_t *create_final_static_route_node(std::vector<route_node_t*> *route_nodes, route_node_t *parent, std::string name);

// Create a static node without a variable substitution
route_node_t *create_static_route_node(std::vector<route_node_t*> *route_nodes, route_node_t *parent, std::string name);

// Create a node with variable substitution
route_node_t *create_variable_route_node(std::vector<route_node_t*> *route_nodes, route_node_t *parent, std::string name, route_variable_type_e var_type);

#endif // __ROUTE_H__
