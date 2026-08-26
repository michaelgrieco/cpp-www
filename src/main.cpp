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

static constexpr int    DEFAULT_PORT  = 8443;
static constexpr char   CERT_FILE[]   = "certs/server.crt";
static constexpr char   KEY_FILE[]    = "certs/server.key";
static constexpr char   INDEX_FILE[]  = "www/index.html";

// ---------------------------------------------------------------------------
// Read a file into a string. Returns empty string on failure.
// ---------------------------------------------------------------------------
// TODO stream buffer into ssl so do not pull entire file into memory
static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// index callback
void index(SSL *ssl, variables_t vars, std::string body) {
    (void)vars;
    (void)body;
    std::cout << "Index callback" << std::endl;

    std::cout << "Variables:" << std::endl;
    for (variable_entry_t entry : vars) {
        std::cout << entry.first << ": " << entry.second.to_string() << std::endl;
    }

    std::cout << "Body:" << std::endl;
    variables_t form_vars;
    parse_query_args(body, form_vars);
    for (variable_entry_t entry : form_vars) {
        std::cout << entry.first << ": " << entry.second.to_string() << std::endl;
    }

    std::string file_body = read_file(INDEX_FILE);
    std::string response = http_response(HTTP_OKAY, "OK", "text/html", file_body, {});
    send_response(ssl, response);
}

void get_form(SSL *ssl, variables_t vars, std::string body) {
    (void)vars;
    (void)body;
    std::cout << "Get form callback" << std::endl;
    std::string file_body = read_file("www/form.html");
    std::string response = http_response(HTTP_OKAY, "OK", "text/html", file_body, {});
    send_response(ssl, response);
}

void post_form(SSL *ssl, variables_t vars, std::string body) {
    (void)vars;
    (void)body;
    std::cout << "Post form callback" << std::endl;
    std::cout << "Completed form!" << std::endl;
    std::cout << "Variables:" << std::endl;
    for (variable_entry_t entry : vars) {
        std::cout << entry.first << ": " << entry.second.to_string() << std::endl;
    }

    std::cout << "Body:" << std::endl;
    variables_t form_vars;
    parse_query_args(body, form_vars);
    for (variable_entry_t entry : form_vars) {
        std::cout << entry.first << ": " << entry.second.to_string() << std::endl;
    }

    //std::string file_body = read_file(INDEX_FILE);
    std::string response = http_response(HTTP_MOVED_PERMANENTLY, "Moved Permanently", "text", "", {"Location: /form/submitted"});
    send_response(ssl, response);
}

void get_form_completion(SSL *ssl, variables_t vars, std::string body) {
    (void)vars;
    (void)body;
    std::cout << "Form completion callback" << std::endl;
    std::string file_body = read_file("www/form_completion.html");
    std::string response = http_response(HTTP_OKAY, "OK", "text/html", file_body, {});
    send_response(ssl, response);
}

route_node_t *root_ptr;
std::vector<route_node_t*> route_nodes;

static int      g_listen_fd = -1;
static SSL_CTX* g_ctx       = nullptr;

static void shutdown_handler(int /*sig*/) {
    std::cout << "Shutdown handler" << std::endl;
    for (auto ptr : route_nodes) delete ptr;
    if (g_listen_fd >= 0) close(g_listen_fd);
    if (g_ctx) SSL_CTX_free(g_ctx);
    std::exit(0);
}

static inline route_node_t *create_route_node(route_node_t *parent, std::string name, bool is_static, route_variable_type_t var_type) {
    int idx = (int)route_nodes.size();

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
    route_nodes.push_back(node);
    std::cout << "  Node's number of children is " << (int)node->children.size() << std::endl;

    // add node to parent
    if (parent) {
        std::cout << "  Parent's number of children is " << (int)parent->children.size() << std::endl;
        parent->children.push_back(node);
    }

    return node;
}

static route_node_t *create_static_route_node(route_node_t *parent, std::string name) {
    return create_route_node(parent, name, true, STATIC);
}

static route_node_t *create_variable_route_node(route_node_t *parent, std::string name, route_variable_type_t var_type) {
    return create_route_node(parent, name, false, var_type);
}

static inline void construct_route_tree() {

    // Top-level node
    route_node_t *root_url = create_static_route_node(nullptr, "index");
    root_ptr = root_url;
    root_url->callbacks.insert({HTTP_GET, index});

    // Example ID endpoint
    route_node_t *id_url = create_variable_route_node(root_url, "id", INT);
    root_url->callbacks.insert({HTTP_GET, index});

    // Form base route
    route_node_t *form_url = create_static_route_node(root_url, "form");
    form_url->callbacks.insert({HTTP_GET, index});

    // Form completion route
    route_node_t *form_completion_url = create_static_route_node(form_url, "submitted");
    //form_url->callbacks[HTTP_GET] = get_form_completion;
    form_completion_url->callbacks.insert({HTTP_GET, get_form_completion});

    // Form route
    route_node_t *form_id_url = create_variable_route_node(form_url, "id", STRING);
    form_id_url->callbacks.insert({HTTP_GET, get_form});
    form_id_url->callbacks.insert({HTTP_POST, post_form});
}

// ---------------------------------------------------------------------------
// Handle one TLS connection: read the request line, route, respond.
// ---------------------------------------------------------------------------
void handle_client(SSL* ssl) {
    std::cout << "Handle_client callbacks for GET: " << root_ptr->callbacks.count(HTTP_GET) << " " << (uint64_t)root_ptr << std::endl;
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
            //std::cout << "New route token from " << token_start_i << " to " << i << ": " << std::string(tmp + token_start_i) << std::endl;

            // search in available endpoints (first matching endpoint has priority)
            std::string token = std::string(tmp + token_start_i);
            route_variable_t var = parse_var(token);
            std::cout << "Token " << token << " has possible type " << var.type << std::endl;
            //for (auto iter = dst->children->begin(); iter != dst->children->end(); ++iter) {
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
        send_http_response(ssl, HTTP_NOT_FOUND, "Not Found", "text/plain", "404 Not Found", {});
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    int port = DEFAULT_PORT;
    if (argc == 2) port = std::atoi(argv[1]);

    g_ctx       = create_ssl_context(CERT_FILE, KEY_FILE);
    g_listen_fd = create_listen_socket(port);

    std::signal(SIGINT, shutdown_handler);

    std::cout << "Listening on https://localhost:" << port << std::endl;

    // reserve memory
    route_nodes.reserve(64);

    // register URLs
    construct_route_tree();
    std::cout << "Registered " << (int)route_nodes.size() << " routes" << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);
        int client_fd = accept(g_listen_fd,
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &client_len);
        if (client_fd < 0) { perror("accept"); continue; }

        SSL* ssl = SSL_new(g_ctx);
        SSL_set_fd(ssl, client_fd);

        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
        } else {
            handle_client(ssl);
        }

        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client_fd);
    }

    shutdown_handler(0);
    return 0;
}
