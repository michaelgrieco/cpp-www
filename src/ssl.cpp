
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

#include "ssl.h"

// ---------------------------------------------------------------------------
// Initialise an SSL_CTX loaded with the server certificate and private key.
// ---------------------------------------------------------------------------
SSL_CTX* create_ssl_context(const char *cert_file, const char *key_file) {
    //SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX* ctx = SSL_CTX_new(SSLv23_server_method());
    SSL_CTX_set_options(ctx, SSL_OP_SINGLE_DH_USE);
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        std::exit(1);
    }
    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        std::exit(1);
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        std::exit(1);
    }
    return ctx;

}

// ---------------------------------------------------------------------------
// Create and bind a TCP listen socket on the given port.
// ---------------------------------------------------------------------------
int create_listen_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); std::exit(1); }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port));

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind"); std::exit(1);
    }
    if (listen(fd, SOMAXCONN) < 0) {
        perror("listen"); std::exit(1);
    }
    return fd;
}

// Source - https://stackoverflow.com/q/19286131
// Posted by Jeremy Friesner
// Retrieved 2026-10-01, License - CC BY-SA 3.0

#define SSL_STATE_WRITE_WANTS_READABLE_SOCKET 1
#define SSL_STATE_WRITE_WANTS_WRITEABLE_SOCKET 2
#define SSL_STATE_READ_WANTS_READABLE_SOCKET 4
#define SSL_STATE_READ_WANTS_WRITEABLE_SOCKET 8
#define RESULT_FAILURE 1
#define RESULT_SUCCESS 0

// Socket setup routine, called when the server accepts a new TCP socket
int SSLSession :: SetupSSL(int sockfd)
{
  _ctx = SSL_CTX_new(SSLv23_method());
  if (_ctx)
  {
     SSL_CTX_set_mode(_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);

     _ssl = SSL_new(_ctx);
     if (_ssl)
     {
        _sbio = BIO_new_socket(sockfd, BIO_NOCLOSE);
        if (_sbio)
        {
           SSL_set_bio(_ssl, _sbio, _sbio);
           SSL_set_accept_state(_ssl);

           BIO_set_nbio(_sbio, !blocking);
           ERR_print_errors_fp(stderr);

           return RESULT_SUCCESS;
        }
        else fprintf(stderr, "SSLSession:  BIO_new_socket() failed!\n");
     }
     else fprintf(stderr, "SSLSession:  SSL_new() failed!\n");
  }
  else fprintf(stderr, "SSLSession:  SSL_CTX_new() failed!\n");

  return RESULT_FAILURE;
}

// Socket read routine -- returns number of bytes read from SSL-land
int32_t SSLSession :: Read(void *buffer, uint32_t size)
{
  if (_ssl == NULL) return -1;

  int32_t bytes = SSL_read(_ssl, buffer, size);
  if (bytes > 0) 
  {
     _sslState &= ~(SSL_STATE_READ_WANTS_READABLE_SOCKET | SSL_STATE_READ_WANTS_WRITEABLE_SOCKET);
  }
  else if (bytes == 0) return -1;  // connection was terminated
  else
  {
     int err = SSL_get_error(_ssl, bytes);
     if (err == SSL_ERROR_WANT_WRITE)
     {
        // We have to wait until our socket is writeable, and then repeat our SSL_read() call.
        _sslState &= ~SSL_STATE_READ_WANTS_READABLE_SOCKET;
        _sslState |=  SSL_STATE_READ_WANTS_WRITEABLE_SOCKET;
        bytes = 0;
     }
     else if (err == SSL_ERROR_WANT_READ)
     {
        // We have to wait until our socket is readable, and then repeat our SSL_read() call.
        _sslState |=  SSL_STATE_READ_WANTS_READABLE_SOCKET;
        _sslState &= ~SSL_STATE_READ_WANTS_WRITEABLE_SOCKET;
        bytes = 0;
     }
     else
     {
        fprintf(stderr, "SSL_read() ERROR:  ");
        ERR_print_errors_fp(stderr);
     }
  }
  return bytes;
}

// Socket write routine -- returns number of bytes written to SSL-land
int32_t SSLSession :: Write(const void *buffer, uint32_t size)
{
  if (_ssl == NULL) return -1;

  int32_t bytes = SSL_write(_ssl, buffer, size);
  if (bytes > 0) 
  {
     _sslState &= ~(SSL_STATE_WRITE_WANTS_READABLE_SOCKET | SSL_STATE_WRITE_WANTS_WRITEABLE_SOCKET);
  }
  else if (bytes == 0) return -1;  // connection was terminated
  else
  {
     int err = SSL_get_error(_ssl, bytes);
     if (err == SSL_ERROR_WANT_READ)
     {
        // We have to wait until our socket is readable, and then repeat our SSL_write() call.
        _sslState |=  SSL_STATE_WRITE_WANTS_READABLE_SOCKET;
        _sslState &= ~SSL_STATE_WRITE_WANTS_WRITEABLE_SOCKET;
        bytes = 0;
     }
     else if (err == SSL_ERROR_WANT_WRITE)
     {
        // We have to wait until our socket is writeable, and then repeat our SSL_write() call.
        _sslState &= ~SSL_STATE_WRITE_WANTS_READABLE_SOCKET;
        _sslState |=  SSL_STATE_WRITE_WANTS_WRITEABLE_SOCKET;
        bytes = 0;
     }
     else
     {
        fprintf(stderr,"SSL_write() ERROR!");
        ERR_print_errors_fp(stderr);
     }
  }
  return bytes;
}

