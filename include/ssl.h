
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

class SSLSession {
public:
    int SetupSSL(int sockfd);
    int Read(void *buffer, uint32_t size);
    int Write(const void *buffer, uint32_t size);

private:
    SSL_CTX *_ctx;
    SSL *_ssl;
    BIO *_sbio;
    uint32_t _sslState;
    bool blocking;

};

#endif // __LOCAL_SSL_H__
