
#ifndef __ROUTE_H__
#define __ROUTE_H__

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <map>
#include <vector>

#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

// Forward declarations
struct route_node_t;

// Types of variables in a URL route node
typedef enum {
    STATIC = 0,
    INT,
    FLOAT,
    STRING,
} route_variable_type_t;

// URL variable value
typedef union route_variable_value_t {
    int i;
    float f;
    double d;
    std::string s;

    route_variable_value_t() {}
    ~route_variable_value_t() {}
} route_variable_value_t;

// URL variable
typedef struct route_variable_t {
    route_variable_type_t type;
    route_variable_value_t value;

    route_variable_t() : type(STATIC), value() {}

    route_variable_t(const route_variable_t& o) : type(o.type), value() {
        switch (o.type) {
            case INT:    value.i = o.value.i; break;
            case FLOAT:  value.f = o.value.f; break;
            case STRING: new (&value.s) std::string(o.value.s); break;
            default: break;
        }
    }

    route_variable_t(route_variable_type_t type) : type(type), value() {}

    route_variable_t(int i) : type(INT), value() { value.i = i; }
    route_variable_t(float f) : type(FLOAT), value() { value.f = f; }
    route_variable_t(std::string s) : type(STRING), value() { new (&value.s) std::string(s); }
    route_variable_t(char *s) : type(STRING), value() { new (&value.s) std::string(s); }

    route_variable_t& operator=(const route_variable_t& o) {
        if (this == &o) return *this;
        // Destroy active string if we're replacing it
        if (type == STRING) value.s.~basic_string();
        type = o.type;
        switch (o.type) {
            case INT:    value.i = o.value.i; break;
            case FLOAT:  value.f = o.value.f; break;
            case STRING: new (&value.s) std::string(o.value.s); break;
            default: break;
        }
        return *this;
    }

    ~route_variable_t() {
        if (type == STRING) value.s.~basic_string();
    }

    std::string to_string() {
        switch (type) {
            case INT:    return std::to_string(value.i);
            case FLOAT:  return std::to_string(value.f);
            case STRING: return value.s;
            default: return "";
        }
    }
} route_variable_t;

// Mapping of name to variable in a URL
typedef std::pair<std::string, route_variable_t> variable_entry_t;
typedef std::map<std::string, route_variable_t> variables_t;

// URL callback function
typedef void(*callback_t)(SSL *ssl, variables_t vars, std::string body);

// Mapping of request method to callback
typedef std::map<std::string, callback_t> callbacks_t;

// Piece of a URL path
typedef struct route_node_t {
    bool is_static;
    route_variable_t variable;
    std::string name;
    std::vector<struct route_node_t*> children;
    callbacks_t callbacks;
} route_node_t;

// Parse a variable
route_variable_t parse_var(std::string arg);

#endif // __ROUTE_H__
