
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

        int ssl_err = SSL_accept(ssl);
        //std::cout << "SSL_accept returned " << ssl_err << std::endl;
        if (ssl_err <= 0) {
            //ERR_print_errors_fp(stderr);
            SSL_shutdown(ssl);
            SSL_free(ssl);
        } else {
            try {
                handle_client(ssl, root_ptr);
            } catch (const std::exception &e) {
                std::cout << "  Error handling client: " << e.what() << std::endl;
            }
            SSL_shutdown(ssl);
            SSL_free(ssl);
            close(client_fd);
        }
    }
}

// Respond to a client request
#define BUF_SIZE 2048
void http_server::handle_client(SSL* ssl, route_node_t *root_ptr) {
    const auto now = std::chrono::system_clock::now();
    std::time_t req_time = std::chrono::system_clock::to_time_t(now);

    http_request_t request = {
        /*.ssl =*/ ssl,
        /*.server =*/ this,
        /*.header_vars =*/ std::map<std::string, std::string>(),
        /*.vars =*/ variables_t(),
        /*.bytes_read =*/ 0,
        /*.body =*/ "",
        /*.req_time =*/ req_time
    };

    // Read until we have at least the request line.
    std::string buf;
    buf.reserve(BUF_SIZE);
    char tmp[BUF_SIZE+1];
    // Read until we have the full HTTP headers (ends with \r\n\r\n)
    while (buf.find("\r\n\r\n") == std::string::npos) {
        int n = read_ssl_request(request, tmp, BUF_SIZE);
        if (n == -2) {
            std::cout << "Request grew too large: " << request.bytes_read << std::endl;
            return;
        }
        else if (n <= 0) {
            break;
        }
        buf += tmp;
    }

    // Parse all HTTP headers into header_vars (lower-cased name -> value)
    std::size_t header_end = buf.find("\r\n\r\n") + 4;

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
        for (int i = 0; i < (int)name.length(); ++i) {
            char c = name[i];
            if (c >= 'A' && c <= 'Z') {
                name[i] = 'a' + (c - 'A');
            }
        }

        // get header value without leading whitespace
        std::size_t val_start = colon + 1;
        while (val_start < line_end && buf[val_start] == ' ') ++val_start;
        std::string value = buf.substr(val_start, line_end - val_start);

        request.header_vars[name] = value;
        pos = line_end;
    }

    #define HEADER_CONTENT_LENGTH "content-length"
    #define HEADER_CONTENT_TYPE "content-type"
    #define HEADER_USER_AGENT "user-agent"
    #define HEADER_ACCEPT_LANGUAGE "accept-language"

    // Parse content length
    int content_length = 0;
    if (request.header_vars.count(HEADER_CONTENT_LENGTH)) {
        content_length = std::stoi(request.header_vars[HEADER_CONTENT_LENGTH]);
    }

    // Test content type
    bool is_multipart_form =
        request.header_vars.count(HEADER_CONTENT_TYPE) &&
        request.header_vars[HEADER_CONTENT_TYPE].find("multipart/form-data") == 0;

    // Read the body if Content-Length indicates there is one
    if (content_length) {
        if (is_multipart_form) {
            request.body += buf.substr(header_end);
        }
        else {
            request.body.reserve(BUF_SIZE);
            int body_bytes_read = static_cast<int>(buf.size()) - static_cast<int>(header_end);
            request.body += buf.substr(header_end);
            while (body_bytes_read < content_length) {
                int n = read_ssl_request(request, tmp, BUF_SIZE);
                if (n == -2) {
                    std::cout << "Request grew too large: " << request.bytes_read << std::endl;
                    return;
                }
                else if (n <= 0) {
                    break;
                }
                tmp[n] = '\0';
                request.body += tmp;
                body_bytes_read += n;
            }
        }
    }

    // Parse the request line: METHOD <SP> path <SP> HTTP/x.x
    char *time_str = std::ctime(&req_time);
    time_str[24] = '\0';
    std::istringstream ss(buf);
    std::string method, path, version;
    ss >> method >> path >> version;
    std::cout << time_str << ": HTTP " << method << " to " << path << std::endl;

    // Validate headers
    for (std::pair<std::string, std::string> p : request.header_vars) {
        std::cout << "  " << p.first << " = " << p.second << std::endl;
    }
    if (!((
        request.header_vars.count(HEADER_USER_AGENT) &&
        request.header_vars[HEADER_USER_AGENT].find("Mozilla") == 0
    ) && (
        request.header_vars.count(HEADER_ACCEPT_LANGUAGE) && (
            request.header_vars[HEADER_ACCEPT_LANGUAGE][0] == '*' || (
                request.header_vars[HEADER_ACCEPT_LANGUAGE][0] >= 'a' &&
                request.header_vars[HEADER_ACCEPT_LANGUAGE][0] <= 'z'
            )
        )
    ))) {
        //not_found(request);
        return;
    }

    // copy path into buffer
    int path_size = path.size();
    memcpy(tmp, path.c_str(), path_size);
    tmp[path_size] = '\0';

    // lookup function callback for each branch of the path
    route_node_t *dst = root_ptr;
    int i = 1;
    int token_start_i = 1;
    int token_length;
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
            route_node_t *next_dst = nullptr;
            route_variable_t var = parse_var(token);
            for (auto node : dst->children) {
                // compare strings
                if (node->is_static &&
                    token_length == (int)node->name.size() &&
                    token == node->name
                ) {
                    next_dst = node;
                    break;
                }

                // check type equivalence for route variable
                else if (!node->is_static && node->variable.type == var.type) {
                    request.vars[node->name] = var;
                    next_dst = node;
                    break;
                }
            }
            
            // move to next token
            token_start_i = i + 1;
            dst = next_dst;
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
                if (token_start_i >= path_size) {
                    request.vars[dst->name] = std::string();
                }
                else {
                    tmp[i] = '\0';
                    request.vars[dst->name] = std::string(tmp + token_start_i);
                }

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
    parse_query_args(tmp, i, path_size, request.vars);

    // callback
    if (dst) {
        if (dst->callbacks.count(method)) {
            dst->callbacks.operator[](method)(request);
            return;
        }
        else if (method == HTTP_GET && dst->get_request_file != "") {
            send_http_response_status(ssl, HTTP_OKAY);
            send_http_response_headers(ssl, {});
            send_http_response_file(ssl, "text/html", dst->get_request_file);
            return;
        }
    }

    not_found(request);
}

void http_server::not_found(http_request_t request) {
    send_http_response_status(request.ssl, HTTP_NOT_FOUND);
    send_http_response_body(request.ssl, "text/plain", "404 Not Found, try something else");
}

void http_server::error(http_request_t request, http_status_t status, std::string msg) {
    send_http_response_status(request.ssl, status);
    send_http_response_body(request.ssl, "text/plain", msg);
}


int read_ssl_request(http_request_t &request, char *buf, int max_segment) {
    int n = SSL_read(request.ssl, buf, max_segment);
    if (!n) {
        // End of request
        return 0;
    }
    else if (n < 0) {
        // Error in request
        return -1;
    }
    else {
        // Valid buffer read
        buf[n] = '\0';
        request.bytes_read += n;
        if (request.bytes_read > MAX_REQ_SIZE) {
            std::cout << "read_ssl_request::Large request" << std::endl;
            respond_error(request, HTTP_BAD_REQUEST, "Request too large");
            return -2;
        }
        return n;
    }
}

void respond_error(http_request_t request, http_status_t status, std::string msg) {
    request.server->error(request, status, msg);
}

void respond_not_found(http_request_t request) {
    request.server->not_found(request);
}
