
#ifndef __HTTP_H__
#define __HTTP_H__

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

#include "route.h"

// Request methods
#define HTTP_GET "GET"
#define HTTP_POST "POST"

// Response statuses
typedef enum {
    HTTP_OKAY = 200,
    HTTP_MOVED_PERMANENTLY = 301,
    HTTP_BAD_REQUEST = 400,
    HTTP_NOT_FOUND = 404,
    HTTP_INTERNAL_ERROR = 500,
} http_status_t;

// Map of HTTP statuses to the text value
#define HTTP_STATUS_TEXT_MAP(name) static std::map<int, std::string> name = { \
    { HTTP_OKAY, "OKAY" }, \
    { HTTP_MOVED_PERMANENTLY, "Moved Permanently" }, \
    { HTTP_BAD_REQUEST, "Bad Request" }, \
    { HTTP_NOT_FOUND, "Not Found" } \
    { HTTP_INTERNAL_ERROR, "Internal Server Error" } \
};

// Content types

void parse_query_args(char *buf, int i, int path_size, variables_t &vars);
void parse_query_args(std::string buf, variables_t &vars);

std::string http_response(http_status_t status, const std::string& status_text,
                                 const std::string& content_type,
                                 const std::string& body,
                                 std::vector<std::string> headers);

inline void send_response(SSL* ssl, std::string response);

void send_http_response_status(SSL* ssl, http_status_t status);
void send_http_response_headers(SSL* ssl, std::map<std::string, std::string> headers);
void send_http_response_body(SSL* ssl, std::string content_type, std::string body);
void send_http_response_file(SSL* ssl, std::string content_type, std::string path);

#endif // __HTTP_H__
