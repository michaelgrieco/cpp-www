
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
HTTP_STATUS_TEXT_MAP(http_status_text);

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
/*std::string http_response(http_status_t status, const std::string& status_text,
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
}*/

inline void send_response(SSL* ssl, std::string response) {
    SSL_write(ssl, response.data(), static_cast<int>(response.size()));
}

void send_http_response_status(SSL* ssl, http_status_t status) {
    std::ostringstream buf;
    std::string status_text = http_status_text[status];
    buf << "HTTP/1.1 " << status << " " << status_text << "\r\n"
        << "Connection: close\r\n";
    std::string str = buf.str();
    send_response(ssl, str);
}

void send_http_response_headers(SSL* ssl, std::map<std::string, std::string> headers) {
    std::ostringstream buf;
    for (auto& [key, value] : headers) {
        buf << key << ":" << value << "\r\n";
    }
    std::string str = buf.str();
    send_response(ssl, str);
}

void send_http_response_body(SSL* ssl, std::string content_type, std::string body) {
    std::ostringstream buf;
    buf << "Content-Type: " << content_type << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "\r\n";
    std::string str = buf.str();
    send_response(ssl, str);
    send_response(ssl, body);
}

void send_http_response_file(SSL* ssl, std::string content_type, std::string path) {
    // read file
    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    f.seekg(0, std::ios::end);
    std::streamsize file_size = f.tellg();
    f.seekg(0, std::ios::beg);
    
    // write headers
    std::ostringstream buf;
    buf << "Content-Type: " << content_type << "\r\n"
      << "Content-Length: " << file_size << "\r\n"
      << "\r\n";
    std::string str = buf.str();
    send_response(ssl, str);
    
    // write body
    char block[1024];
    while (f.read(block, sizeof(block)) || f.gcount() > 0) {
        send_response(ssl, std::string(block, f.gcount()));
    }
}
