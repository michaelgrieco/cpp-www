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

// ==============================
// ===== Callback functions =====
// ==============================

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

    send_http_response_status(ssl, HTTP_OKAY);
    send_http_response_headers(ssl, {});
    send_http_response_file(ssl, "text/html", "www/index.html");
}

void get_form(SSL *ssl, variables_t vars, std::string body) {
    (void)vars;
    (void)body;
    std::cout << "Get form callback" << std::endl;

    send_http_response_status(ssl, HTTP_OKAY);
    send_http_response_headers(ssl, {});
    send_http_response_file(ssl, "text/html", "www/form.html");
}

void get_form_search(SSL *ssl, variables_t vars, std::string body) {
    (void)vars;
    (void)body;
    std::cout << "Get form search callback" << std::endl;

    send_http_response_status(ssl, HTTP_OKAY);
    send_http_response_headers(ssl, {});
    send_http_response_file(ssl, "text/html", "www/search_forms.html");
}

void form_search(SSL *ssl, variables_t vars, std::string body) {
    (void)vars;
    (void)body;
    std::cout << "Search form callback" << std::endl;
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

    std::string form_id = form_vars["name"].value.s;
    std::string form_url = "/form/" + form_id;
    std::cout << "Redirecting to form " << form_id << std::endl;

    send_http_response_status(ssl, HTTP_MOVED_PERMANENTLY);
    send_http_response_headers(ssl, {{"Location", form_url}});
    send_http_response_body(ssl, "text", "");
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

    send_http_response_status(ssl, HTTP_MOVED_PERMANENTLY);
    send_http_response_headers(ssl, {{"Location", "/form/submitted"}});
    send_http_response_body(ssl, "text", "");
}

void get_form_completion(SSL *ssl, variables_t vars, std::string body) {
    (void)vars;
    (void)body;
    std::cout << "Form completion callback" << std::endl;

    send_http_response_status(ssl, HTTP_OKAY);
    send_http_response_headers(ssl, {});
    send_http_response_file(ssl, "text/html", "www/form_completion.html");
}

route_node_t *root_ptr;
std::vector<route_node_t*> route_nodes;

static int      g_listen_fd = -1;
static SSL_CTX* g_ctx       = nullptr;

static inline void construct_route_tree() {

    // Top-level node
    route_node_t *root_url = create_static_route_node(&route_nodes, nullptr, "index");
    root_ptr = root_url;
    root_url->callbacks.insert({HTTP_GET, index});

    // Example ID endpoint
    route_node_t *id_url = create_variable_route_node(&route_nodes, root_url, "id", INT);
    root_url->callbacks.insert({HTTP_GET, index});

    // Form base route
    route_node_t *form_url = create_static_route_node(&route_nodes, root_url, "form");
    form_url->callbacks.insert({HTTP_GET, get_form_search});
    form_url->callbacks.insert({HTTP_POST, form_search});

    // Form completion route
    route_node_t *form_completion_url = create_static_route_node(&route_nodes, form_url, "submitted");
    //form_url->callbacks[HTTP_GET] = get_form_completion;
    form_completion_url->callbacks.insert({HTTP_GET, get_form_completion});

    // Form route
    route_node_t *form_id_url = create_variable_route_node(&route_nodes, form_url, "id", STRING);
    form_id_url->callbacks.insert({HTTP_GET, get_form});
    form_id_url->callbacks.insert({HTTP_POST, post_form});
}

// Exit point
static void shutdown_handler(int /*sig*/) {
    std::cout << "Shutdown handler" << std::endl;
    for (auto ptr : route_nodes) delete ptr;
    if (g_listen_fd >= 0) close(g_listen_fd);
    if (g_ctx) SSL_CTX_free(g_ctx);
    std::exit(0);
}

// Entry point
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
            handle_client(ssl, root_ptr);
        }

        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client_fd);
    }

    shutdown_handler(0);
    return 0;
}
