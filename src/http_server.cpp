
#include "http_server.h"

#include <chrono>
#include <ctime>

#include "http.h"
#include "ssl.h"
#include "schema.h"
#include "route.h"

// =======================================
// ===== Base server-level functions =====
// =======================================

http_server::http_server(int port, bool https, const char *cert_file, const char *key_file)
    : port(port), https(https), cert_file((char*)cert_file), key_file((char*)key_file) {
    
}

http_server::~http_server() {
    for (auto ptr : route_nodes) delete ptr;
    if (g_listen_fd >= 0) close(g_listen_fd);
    if (https && g_ctx) SSL_CTX_free(g_ctx);
}

void http_server::construct() {
    if (https) {
        g_ctx       = create_ssl_context(cert_file, key_file);
        g_listen_fd = create_listen_socket(port);
    }

    std::cout << "Listening on https://localhost:" << port << std::endl;

    // reserve memory
    route_nodes.reserve(64);

    // register URLs
    construct_route_tree();
    std::cout << "Registered " << (int)route_nodes.size() << " routes" << std::endl;
}

void http_server::listen() {
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
}

// Respond to a client request
void http_server::handle_client(SSL* ssl, route_node_t *root_ptr) {
    std::ostringstream error_ss;
    const auto now = std::chrono::system_clock::now();
    std::time_t req_time = std::chrono::system_clock::to_time_t(now);

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

    // Parse all HTTP headers into header_vars (lower-cased name -> value)
    std::size_t header_end = buf.find("\r\n\r\n") + 4;
    std::map<std::string, std::string> header_vars;

    // Skip the request line, then iterate over each header line
    std::size_t pos = buf.find("\r\n");
    while (pos != std::string::npos && pos + 2 < header_end) {
        pos += 2; // move past \r\n
        std::size_t line_end = buf.find("\r\n", pos);
        if (line_end == std::string::npos || line_end >= header_end) break;

        std::size_t colon = buf.find(':', pos);
        if (colon == std::string::npos || colon >= line_end) { pos = line_end; continue; }

        // get header name as lower case
        std::string name = buf.substr(pos, colon - pos);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        // get header value without leading whitespace
        std::size_t val_start = colon + 1;
        while (val_start < line_end && buf[val_start] == ' ') ++val_start;
        std::string value = buf.substr(val_start, line_end - val_start);

        header_vars[name] = value;
        pos = line_end;
    }

    // Parse content length
    int content_length = 0;
    if (header_vars.count("content-length")) {
        content_length = std::stoi(header_vars["content-length"]);
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
    char *time_str = std::ctime(&req_time);
    time_str[24] = '\0';
    std::istringstream ss(buf);
    std::string method, path, version;
    ss >> method >> path >> version;
    std::cout << time_str << ": HTTP " << method << " to " << path << std::endl;

    // copy path into buffer
    int path_size = path.size();
    memcpy(tmp, path.c_str(), path_size);
    tmp[path_size] = '\0';

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
            for (auto node : dst->children) {
                //route_node_t *node = *iter;
                // compare strings
                if (node->is_static &&
                    token_length == (int)node->name.size() &&
                    token == node->name
                ) {
                    dst = node;
                    break;
                }

                // check type equivalence for route variable
                else if (node->variable.type == var.type) {
                    vars[node->name] = var;
                    dst = node;
                    break;
                }
            }
            
            // move to next token
            token_start_i = i + 1;
            if (!dst) break;

            // test if should skip scanning the rest of the route
            if (dst->is_final_node) {
                // scan to find end of URL route
                char c = 0;
                for (; i <= path_size; ++i) {
                    c = tmp[i];
                    if (c == '?' || c == '\0') {
                        break;
                    }
                }

                // save rest of URL as a variable
                tmp[i] = '\0';
                vars[dst->name] = std::string(tmp + token_start_i);

                // restore character for query string parsing
                tmp[i] = c;
                break;
            }
        }

        if (is_query_start) {
            ++i;
            break;
        }
    }

    // get query string
    parse_query_args(tmp, i, path_size, vars);

    // callback
    if (dst && dst->callbacks.count(method)) {
        http_request_t request = { ssl, this, vars, body, req_time };
        dst->callbacks.operator[](method)(request);

        std::string error_string = error_ss.str();
        if (error_string.length() > 0) {
            std::cout << "Error detected" << std::endl;
        }
    }
    else {
        send_http_response_status(ssl, HTTP_NOT_FOUND);
        send_http_response_body(ssl, "text/plain", "404 Not Found, try something else");
    }
}
