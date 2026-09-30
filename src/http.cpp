
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

#include "http_server.h"
#include "http.h"
HTTP_STATUS_TEXT_MAP(http_status_text);

// ===================================
// ===== Parse HTTP request body =====
// ===================================

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

/*
------geckoformboundary5b646c4afa3c5bf9d917fad17379fd74
Content-Disposition: form-data; name="totp"

123456
------geckoformboundary5b646c4afa3c5bf9d917fad17379fd74
Content-Disposition: form-data; name="uploadedfile"; filename="test.txt"
Content-Type: text/plain

Hello!

World

Goodbye



------geckoformboundary5b646c4afa3c5bf9d917fad17379fd74--
*/

// ====================================
// ===== Parse multipart/form-data ====
// ====================================

// Extract the value of a named attribute from a Content-Disposition or
// Content-Type header string, e.g. extract_attr("name=\"foo\"", "name") -> "foo".
// Returns empty string if not found.
static std::string extract_attr(const std::string &header, const std::string &attr) {
    std::string needle = attr + "=\"";
    std::size_t pos = header.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    std::size_t end = header.find('"', pos);
    if (end == std::string::npos) return header.substr(pos);
    return header.substr(pos, end - pos);
}

// Extract boundary token from Content-Type value.
// e.g. "multipart/form-data; boundary=----xyz" -> "----xyz"
static std::string extract_boundary(const std::string &content_type) {
    std::string needle = "boundary=";
    std::size_t pos = content_type.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    // boundary may be quoted
    if (pos < content_type.size() && content_type[pos] == '"') {
        ++pos;
        std::size_t end = content_type.find('"', pos);
        return content_type.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    }
    // unquoted: runs until whitespace or end
    std::size_t end = content_type.find_first_of(" \t;", pos);
    return content_type.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

std::map<std::string, std::string> parse_multipart_args(
    http_request_t &request,
    const std::map<std::string, schema_field_t> &schema,
    const std::string &upload_dir)
{
    std::cout << "  parse_multipart_args" << std::endl;

    std::map<std::string, std::string> result;

    // --- Extract boundary --------------------------------------------------
    std::string content_type = "";
    if (request.header_vars.count("content-type"))
        content_type = request.header_vars["content-type"];
    int content_length = 0;
    if (request.header_vars.count("content-length")) {
        content_length = std::stoi(request.header_vars["content-length"]);
    }
    std::cout << "content-length is " << content_length << std::endl;

    std::string boundary = extract_boundary(content_type);
    if (boundary.empty()) {
        std::cout << "  parse_multipart_args: no boundary found in Content-Type" << std::endl;
        return result;
    }

    // The wire delimiter prefixes boundary with "--"
    std::string delim     = "--" + boundary;      // part separator
    std::string delim_end = "--" + boundary + "--"; // final boundary

    // --- Read the full stream into a rolling accumulation buffer -----------
    // We accumulate in `buf`, scanning for boundary markers.
    // To avoid buffering huge files, we flush file-part bodies to disk as soon
    // as we can confirm we are not looking at a boundary byte run.
    // The maximum boundary length we ever need to look-ahead is delim.size()+2
    // (\r\n prefix) so we can safely flush buf up to buf.size()-lookahead_len.

    //const std::size_t lookahead = delim.size() + 4; // "\r\n--boundary\r\n" margin
    const int READ_BUF = 4096;
    char tmp[READ_BUF + 1];

    // Seed buf with already-buffered body bytes (headers were stripped by
    // handle_client; request.body holds anything read after the header block).
    std::string buf = request.body;
    std::cout << "To start, buf (" << buf.length() << ") has " << buf << std::endl;

    // Helper: refill buf until it contains at least `needed` bytes or EOF.
    // Never reads more than content_length total bytes from the stream.
    int total_read = (int)buf.size(); // bytes already seeded from request.body
    auto refill = [&](std::size_t needed) {
        std::cout << "Refill called for " << (int)needed << " bytes" << std::endl;
        while (buf.size() < needed && total_read < content_length) {
            int want = (int)std::min(
                (std::size_t)READ_BUF,
                (std::size_t)(content_length - total_read)
            );
            std::cout << "Grabbing " << want << " bytes" << std::endl;
            int n = read_ssl_request(request, tmp, want);
            std::cout << "Read " << n << " bytes" << std::endl;
            if (n <= 0) break;
            buf.append(tmp, n);
            total_read += n;
        }
    };

    // Advance past the first boundary line ("\r\n" + delim + "\r\n" or just delim + "\r\n")
    std::cout << "Trying to grab " << ((int)delim.size() + 4) << " bytes" << std::endl;
    refill(delim.size() + 4);
    std::size_t pos = buf.find(delim);
    if (pos == std::string::npos) {
        std::cout << "  parse_multipart_args: opening boundary not found" << std::endl;
        return result;
    }
    pos += delim.size();
    // skip \r\n after delimiter
    if (pos + 1 < buf.size() && buf[pos] == '\r' && buf[pos+1] == '\n') pos += 2;
    else if (pos < buf.size() && buf[pos] == '\n') pos += 1;

    // --- Iterate over parts ------------------------------------------------
    while (true) {
        // Ensure we have enough data to parse part headers
        refill(pos + 512);
        if (pos >= buf.size()) break;

        // Check for final boundary
        if (buf.compare(pos, delim_end.size(), delim_end) == 0) break;

        // --- Parse part headers -------------------------------------------
        // Headers end at the first blank line (\r\n\r\n)
        std::string field_name;
        std::string filename;

        while (true) {
            refill(pos + 256);
            std::size_t line_end = buf.find("\r\n", pos);
            if (line_end == std::string::npos) break; // malformed

            std::string line = buf.substr(pos, line_end - pos);
            pos = line_end + 2;

            if (line.empty()) break; // blank line = end of part headers

            // lower-case header name for comparison
            std::size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string hname = line.substr(0, colon);
            for (int i = 0; i < (int)hname.length(); ++i) {
                char c = hname[i];
                if (c >= 'A' && c <= 'Z') {
                    hname[i] = 'a' + (c - 'A');
                }
            }
            //std::transform(hname.begin(), hname.end(), hname.begin(), ::tolower);
            std::string hval  = line.substr(colon + 1);
            // strip leading space
            std::size_t vs = hval.find_first_not_of(" \t");
            if (vs != std::string::npos) hval = hval.substr(vs);

            if (hname == "content-disposition") {
                field_name = extract_attr(hval, "name");
                filename   = extract_attr(hval, "filename");
            }
        }

        if (field_name.empty()) {
            // Skip unrecognised part — scan to next boundary
            std::size_t next = buf.find(delim, pos);
            if (next == std::string::npos) break;
            pos = next + delim.size() + 2;
            continue;
        }

        // Determine if this field is a file type per schema
        bool is_file = false;
        if (schema.count(field_name)) {
            is_file = (schema.at(field_name).type == SCHEMA_TYPE_FILE);
        }

        // --- Read part body until the next boundary -----------------------
        // Body ends just before "\r\n--boundary"
        std::string body_delim = "\r\n" + delim;

        if (is_file) {
            // Stream body to output file
            std::string save_path = upload_dir + "/" + (filename.empty() ? field_name : filename);
            std::ofstream out_file(save_path, std::ios::binary | std::ios::trunc);
            if (!out_file.is_open()) {
                std::cout << "  parse_multipart_args: could not open " << save_path << std::endl;
                break;
            }

            // Write chunks: keep a trailing window of `lookahead` bytes
            // in `buf` so we can detect the boundary without false writes.
            while (true) {
                // Ensure buf holds at least pos + body_delim.size() + 1
                refill(pos + body_delim.size() + 1);

                std::size_t search_end = buf.size();
                std::size_t found = buf.find(body_delim, pos);
                if (found != std::string::npos) {
                    // Write everything up to the boundary, then stop
                    out_file.write(buf.data() + pos, found - pos);
                    pos = found + body_delim.size();
                    break;
                }

                // Boundary not yet fully in buf; flush safe prefix
                if (search_end > pos + body_delim.size()) {
                    std::size_t safe = search_end - body_delim.size();
                    out_file.write(buf.data() + pos, safe - pos);
                    buf = buf.substr(safe);
                    pos = 0;
                } else {
                    // Need more data
                    int n = read_ssl_request(request, tmp, READ_BUF);
                    if (n <= 0) break;
                    buf.append(tmp, n);
                }
            }
            out_file.close();

            // Store metadata
            result[field_name] = "filename=" + filename + ";path=" + save_path;
            std::cout << "  parse_multipart_args: saved file field '" << field_name
                      << "' to " << save_path << std::endl;
        } else {
            // Accumulate text value
            std::string value;
            while (true) {
                refill(pos + body_delim.size() + 1);

                std::size_t search_end = buf.size();
                std::size_t found = buf.find(body_delim, pos);
                if (found != std::string::npos) {
                    value += buf.substr(pos, found - pos);
                    pos = found + body_delim.size();
                    break;
                }

                if (search_end > pos + body_delim.size()) {
                    std::size_t safe = search_end - body_delim.size();
                    value += buf.substr(pos, safe - pos);
                    buf = buf.substr(safe);
                    pos = 0;
                } else {
                    int n = read_ssl_request(request, tmp, READ_BUF);
                    if (n <= 0) break;
                    buf.append(tmp, n);
                }
            }

            result[field_name] = value;
            std::cout << "  parse_multipart_args: text field '" << field_name
                      << "' = '" << value << "'" << std::endl;
        }

        // Advance past the \r\n after the boundary delimiter
        if (pos + 1 < buf.size() && buf[pos] == '\r' && buf[pos+1] == '\n') {
            pos += 2;
        } else if (pos < buf.size() && buf[pos] == '\n') {
            pos += 1;
        }
        // Check if this is the final boundary
        refill(pos + delim_end.size());
        if (buf.compare(pos, 2, "--") == 0) break;
    }

    return result;
}

// ===============================
// ===== Build HTTP response =====
// ===============================

inline void send_response(SSL* ssl, std::string response) {
    SSL_write(ssl, response.data(), static_cast<int>(response.size()));
}

int get_file_size(std::string path) {
    std::ifstream f(path, std::ios::binary);
    int size = get_file_size(f);
    f.close();
    return size;
}

int get_file_size(std::ifstream &f) {
    if (!f) return 0;
    f.seekg(0, std::ios::end);
    std::streamsize file_size = f.tellg();
    f.seekg(0, std::ios::beg);
    return (int)file_size;
}

void send_http_response_status(SSL* ssl, http_status_t status) {
    std::ostringstream buf;
    std::string status_text = http_status_text[status];
    buf << "HTTP/1.1 " << status << " " << status_text << "\r\n"
        << "Connection: close\r\n";
    std::string str = buf.str();
    send_response(ssl, str);
}

void send_http_response_headers(SSL* ssl, std::map<std::string, std::string> headers, bool close_headers) {
    std::ostringstream buf;
    for (std::pair<std::string, std::string> p : headers) {
        buf << p.first << ":" << p.second << "\r\n";
    }
    if (close_headers) {
        buf << "\r\n";
    }
    std::string str = buf.str();
    send_response(ssl, str);
}

void send_http_response_body(SSL* ssl, std::string content_type, std::string body, bool send_headers) {
    if (send_headers) {
        std::ostringstream buf;
        buf << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "\r\n";
        std::string str = buf.str();
        send_response(ssl, str);
    }
    send_response(ssl, body);
}

void send_http_response_file(SSL* ssl, std::string content_type, std::string path, bool send_headers) {
    // read file
    std::ifstream f(path, std::ios::binary);
    int file_size = get_file_size(f);
    
    // write headers
    if (send_headers) {
        std::ostringstream buf;
        buf << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << file_size << "\r\n"
        << "\r\n";
        std::string str = buf.str();
        send_response(ssl, str);
    }
    
    // write body
    char block[1024];
    while (f.read(block, sizeof(block)) || f.gcount() > 0) {
        send_response(ssl, std::string(block, f.gcount()));
    }
    f.close();
}
