
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

// Decode a percent-encoded URL string (e.g. "hello%20world" -> "hello world").
// '+' is treated as a space (application/x-www-form-urlencoded convention).
static std::string url_decode(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() &&
            std::isxdigit((unsigned char)s[i+1]) &&
            std::isxdigit((unsigned char)s[i+2]))
        {
            char hex[3] = { s[i+1], s[i+2], '\0' };
            out += static_cast<char>(std::strtol(hex, nullptr, 16));
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

void parse_query_args(char *buf, int i, int path_size, variables_t &vars) {
    std::string var_name;
    int token_start_i = i;
    for (; i <= path_size; ++i) {
        bool is_name = buf[i] == '=';
        if (is_name || buf[i] == '&' || buf[i] == '\0') {
            buf[i] = '\0';

            if (is_name) {
                // token is a key, store for later
                var_name = url_decode(std::string(buf + token_start_i));
            }
            else {
                std::string str = url_decode(std::string(buf + token_start_i));
                route_variable_t var = parse_var(str);
                if (vars.count(var_name) && vars[var_name].type == LIST) {
                    vars[var_name].value.l->push_back(var);
                }
                else {
                    vars[var_name] = var;
                }
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

    for (int i = 0; i <= buf_size; ++i) {
        bool is_name = buf[i] == '=';
        if (is_name || buf[i] == '&' || buf[i] == '\0') {
            int token_length = i - token_start_i;
            
            if (token_length) {
                buf[i] = '\0';
                std::string decoded = url_decode(buf.substr(token_start_i, token_length));

                if (is_name) {
                    // token is a key, store for later
                    var_name = decoded;
                }
                else {
                    route_variable_t var = parse_var(decoded);
                    if (vars.count(var_name) && vars[var_name].type == LIST) {
                        vars[var_name].value.l->push_back(var);
                    }
                    else {
                        vars[var_name] = var;
                    }
                    var_name = "";
                }
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
    f.close();
}
