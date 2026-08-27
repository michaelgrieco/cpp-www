
#include "route.h"
#include "http.h"

#include <string>
#include <map>
#include <vector>

// parse variable
route_variable_t parse_var(std::string arg) {
    try {
        std::size_t pos = 0;
        int i = std::stoi(arg, &pos);
        if (pos == arg.size()) {
            return route_variable_t(i);
        }
    } catch (...) {}

    try {
        std::size_t pos = 0;
        float f = std::stof(arg, &pos);
        if (pos == arg.size()) {
            return route_variable_t(f);
        }
    } catch (...) {}

    return route_variable_t(arg);
}

route_node_t *create_route_node(std::vector<route_node_t*> *route_nodes, route_node_t *parent, std::string name, bool is_static, route_variable_type_t var_type) {
    // create node structure
    route_node_t *node = new route_node_t{
        .is_static = is_static,
        .variable = route_variable_t(var_type),
        .name = name,
        .children = {},
        .callbacks = callbacks_t()
    };
    std::cout << "Route name " << name << " has address " << (uint64_t)node << std::endl;
    node->children.reserve(64);
    route_nodes->push_back(node);
    std::cout << "  Node's number of children is " << (int)node->children.size() << std::endl;

    // add node to parent
    if (parent) {
        std::cout << "  Parent's number of children is " << (int)parent->children.size() << std::endl;
        parent->children.push_back(node);
    }

    return node;
}

route_node_t *create_static_route_node(std::vector<route_node_t*> *route_nodes, route_node_t *parent, std::string name) {
    return create_route_node(route_nodes, parent, name, true, STATIC);
}

route_node_t *create_variable_route_node(std::vector<route_node_t*> *route_nodes, route_node_t *parent, std::string name, route_variable_type_t var_type) {
    return create_route_node(route_nodes, parent, name, false, var_type);
}

// ---------------------------------------------------------------------------
// Handle one TLS connection: read the request line, route, respond.
// ---------------------------------------------------------------------------
void handle_client(SSL* ssl, route_node_t *root_ptr) {
    // Read until we have at least the request line.
    std::string buf;
    buf.reserve(2048);
    char tmp[512];
    // Read until we have the full HTTP headers (ends with \r\n\r\n)
    while (buf.find("\r\n\r\n") == std::string::npos) {
        int n = SSL_read(ssl, tmp, static_cast<int>(sizeof(tmp) - 1));
        if (n <= 0) return;
        tmp[n] = '\0';
        buf += tmp;
    }

    // Parse Content-Length header to read body if present
    std::size_t header_end = buf.find("\r\n\r\n") + 4;
    int content_length = 0;
    std::size_t header_test_pos = buf.find("Content-Length:");
    if (header_test_pos == std::string::npos) header_test_pos = buf.find("content-length:");
    if (header_test_pos != std::string::npos) {
        content_length = std::stoi(buf.substr(header_test_pos + 15));
    }

    // Parse Content-Type header
    header_test_pos = buf.find("Content-Type:");
    if (header_test_pos == std::string::npos) header_test_pos = buf.find("content-type:");
    if (header_test_pos != std::string::npos) {
        std::string content_type_str = buf.substr(header_test_pos + 13);
    }

    // Read the body if Content-Length indicates there is one
    std::string body;
    body.reserve(2048);
    int body_bytes_read = static_cast<int>(buf.size()) - static_cast<int>(header_end);
    body += buf.substr(header_end);
    while (body_bytes_read < content_length) {
        int n = SSL_read(ssl, tmp, static_cast<int>(sizeof(tmp) - 1));
        if (n <= 0) break;
        tmp[n] = '\0';
        body += tmp;
        body_bytes_read += n;
    }

    // Parse the request line: METHOD <SP> path <SP> HTTP/x.x
    std::istringstream ss(buf);
    std::string method, path, version;
    ss >> method >> path >> version;
    std::cout << "HTTP " << method << " to " << path << std::endl;

    // copy path into buffer
    int path_size = path.size();
    memcpy(tmp, path.c_str(), path_size);
    tmp[path_size] = '\0';
    std::cout << "Path (" << path_size << ") is " << tmp << std::endl;

    // lookup function callback for each branch of the path
    route_node_t *dst = root_ptr;
    int i = 1;
    int token_start_i = 1;
    int token_length;
    variables_t vars;
    for (; i <= path_size; ++i) {
        // test for end of a complete token
        bool is_query_start = tmp[i] == '?';
        if (is_query_start || tmp[i] == '/' || tmp[i] == '\0') {
            if (i == token_start_i) {
                break;
            }

            // bound current token for comparison
            token_length = i - token_start_i;
            tmp[i] = '\0';

            // search in available endpoints (first matching endpoint has priority)
            std::string token = std::string(tmp + token_start_i);
            route_variable_t var = parse_var(token);
            std::cout << "Token " << token << " has possible type " << var.type << std::endl;
            for (auto node : dst->children) {
                //route_node_t *node = *iter;
                std::cout << "  Branch " << node->name << " has type " << node->variable.type << std::endl;
                // compare strings
                if (node->is_static &&
                    token_length == (int)node->name.size() &&
                    token == node->name
                ) {
                    std::cout << "    Found static node with name " << node->name << std::endl;
                    dst = node;
                    break;
                }

                // check type equivalence for route variable
                else if (node->variable.type == var.type) {
                    std::cout << "    Setting vars (" << (int)vars.size() << ") " << node->name << ": " << var.to_string() << std::endl;
                    vars[node->name] = var;
                    dst = node;
                    break;
                }
            }
            
            // move to next token
            token_start_i = i + 1;
            if (!dst) break;
        }

        if (is_query_start) {
            ++i;
            break;
        }
    }

    // get query string
    parse_query_args(tmp, i, path_size, vars);

    // callback
    std::cout << "In dst, have " << (int)dst->callbacks.size() << " endpoints" << std::endl;
    std::cout << "For method " << method << std::endl;
    std::cout << "  dst is " << (uint64_t)dst << std::endl;
    if (dst && dst->callbacks.count(method)) {
        
        std::cout << "  at endpoint " << dst->name << "," << std::endl;
        dst->callbacks.operator[](method)(ssl, vars, body);
    }
    else {
        send_http_response_status(ssl, HTTP_NOT_FOUND);
        send_http_response_body(ssl, "text/plain", "404 Not Found, try something else");
    }
}
