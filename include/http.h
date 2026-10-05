
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
#include "schema.h"

// Request methods
#define HTTP_GET "GET"
#define HTTP_POST "POST"

// Response statuses
typedef enum {
    HTTP_OKAY = 200,
    HTTP_MOVED_PERMANENTLY = 301,
    HTTP_BAD_REQUEST = 400,
    HTTP_UNAUTHORIZED = 401,
    HTTP_NOT_FOUND = 404,
    HTTP_INTERNAL_ERROR = 500
} http_status_t;

// Map of HTTP statuses to the text value
#define HTTP_STATUS_TEXT_MAP(name) static std::map<int, std::string> name = { \
    { HTTP_OKAY, "OKAY" }, \
    { HTTP_MOVED_PERMANENTLY, "Moved Permanently" }, \
    { HTTP_BAD_REQUEST, "Bad Request" }, \
    { HTTP_UNAUTHORIZED, "Unauthorized" }, \
    { HTTP_NOT_FOUND, "Not Found" }, \
    { HTTP_INTERNAL_ERROR, "Internal Server Error" } \
};

// Content types

// Forward declaration - http_request_t is defined in http_server.h
struct http_request_t;

void parse_query_args(char *buf, int i, int path_size, variables_t &vars);
void parse_query_args(std::string buf, variables_t &vars);

// Parse a multipart/form-data request body.
// `schema` maps field names to their schema_field_t descriptors.
// Text fields (SCHEMA_TYPE_STRING etc.) are stored as their decoded value.
// File fields (SCHEMA_TYPE_FILE) are streamed to a temp file under `upload_dir`
// and stored as "filename=<original name>;path=<saved path>".
// Returns the map of field name -> value/metadata strings.
void parse_multipart_args(
    http_request_t &request,
    std::map<std::string, std::string> &result,
    const std::map<std::string, schema_field_t> &schema,
    const std::string &upload_dir = "data/uploads");

std::string http_response(http_status_t status, const std::string& status_text,
                                 const std::string& content_type,
                                 const std::string& body,
                                 std::vector<std::string> headers);

int get_file_size(std::ifstream &f);
int get_file_size(std::string path);

void send_http_response_status(SSL* ssl, http_status_t status);
void send_http_response_headers(SSL* ssl, std::map<std::string, std::string> headers, bool close_headers = false);
void send_http_response_body(SSL* ssl, std::string content_type, std::string body, bool send_headers = true);
void send_http_response_file(SSL* ssl, std::string content_type, std::string path, bool send_headers = true);

#endif // __HTTP_H__
