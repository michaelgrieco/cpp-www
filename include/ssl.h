
#ifndef __LOCAL_SSL_H__
#define __LOCAL_SSL_H__

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

SSL_CTX* create_ssl_context(const char *cert_file, const char *key_file);

// ---------------------------------------------------------------------------
// Create and bind a TCP listen socket on the given port.
// ---------------------------------------------------------------------------
int create_listen_socket(int port);

#endif // __LOCAL_SSL_H__
