
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
    HTTP_NOT_FOUND = 404,
} http_status_t;

// Content types

void parse_query_args(char *buf, int i, int path_size, variables_t &vars);
void parse_query_args(std::string buf, variables_t &vars);

std::string http_response(http_status_t status, const std::string& status_text,
                                 const std::string& content_type,
                                 const std::string& body,
                                 std::vector<std::string> headers);

void send_response(SSL* ssl, std::string response);

void send_http_response(SSL* ssl, http_status_t status,
                               const std::string& status_text,
                               const std::string& content_type,
                               const std::string& body,
                               std::vector<std::string> headers);

#endif // __HTTP_H__
