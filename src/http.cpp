
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

#include "http.h"

void parse_query_args(char *buf, int i, int path_size, variables_t &vars) {
    std::string var_name;
    int token_start_i = i;
    std::cout << "parse_query_args " << i << " " << path_size << std::endl;
    for (; i <= path_size; ++i) {
        bool is_name = buf[i] == '=';
        if (is_name || buf[i] == '&' || buf[i] == '\0') {
            buf[i] = '\0';

            if (is_name) {
                // token is a key, store for later
                var_name = std::string(buf + token_start_i);
            }
            else {
                route_variable_t var = parse_var(std::string(buf + token_start_i));
                vars[var_name] = var;
                var_name = "";
            }
            
            // move to next token
            token_start_i = i + 1;
        }
    }
}

void parse_query_args(std::string buf, variables_t &vars) {
    std::string var_name;
    int token_start_i = 0;
    int buf_size = (int)buf.size();

    std::cout << "Parsing form body " << buf << std::endl;

    for (int i = 0; i <= buf_size; ++i) {
        bool is_name = buf[i] == '=';
        if (is_name || buf[i] == '&' || buf[i] == '\0') {
            int token_length = i - token_start_i;
            buf[i] = '\0';

            if (is_name) {
                // token is a key, store for later
                var_name = buf.substr(token_start_i, token_length);
            }
            else {
                route_variable_t var = parse_var(buf.substr(token_start_i, token_length));
                vars[var_name] = var;
                var_name = "";
            }
            
            // move to next token
            token_start_i = i + 1;
        }
    }
}

// ---------------------------------------------------------------------------
// Build a minimal HTTP/1.1 response.
// ---------------------------------------------------------------------------
std::string http_response(http_status_t status, const std::string& status_text,
                                 const std::string& content_type,
                                 const std::string& body,
                                 std::vector<std::string> headers) {
    std::ostringstream r;
    r << "HTTP/1.1 " << status << " " << status_text << "\r\n"
      << "Content-Type: " << content_type << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n";
    for (std::string header : headers) {
        r << header << "\r\n";
    }
    r  << "\r\n"
      << body;
    return r.str();
}

void send_response(SSL* ssl, std::string response) {
    SSL_write(ssl, response.data(), static_cast<int>(response.size()));
}

void send_http_response(SSL* ssl, http_status_t status,
                               const std::string& status_text,
                               const std::string& content_type,
                               const std::string& body,
                               std::vector<std::string> headers) {
    send_response(ssl, http_response(status, status_text, content_type, body, headers));
}
