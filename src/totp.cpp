#include "totp.h"

#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <random>
#include <vector>
#include <openssl/hmac.h>
#include <openssl/evp.h>

std::string base32_decode(const std::string &input) {
    std::string out;
    int buffer = 0;
    int bits_left = 0;

    for (char c : input) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '=') {
            continue;
        }

        int val = 0;
        if (c >= 'A' && c <= 'Z') {
            val = c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            val = c - 'a';
        } else if (c >= '2' && c <= '7') {
            val = c - '2' + 26;
        } else {
            continue;
        }

        buffer = (buffer << 5) | val;
        bits_left += 5;
        if (bits_left >= 8) {
            bits_left -= 8;
            out.push_back(static_cast<char>((buffer >> bits_left) & 0xFF));
        }
    }
    return out;
}

std::string generate_totp(const std::string &b32_secret, uint64_t time_step, int digits) {
    std::string key = base32_decode(b32_secret);
    if (key.empty()) {
        return "";
    }

    uint64_t t = static_cast<uint64_t>(std::time(nullptr)) / time_step;
    uint8_t t_bytes[8];
    for (int i = 7; i >= 0; --i) {
        t_bytes[i] = static_cast<uint8_t>(t & 0xFF);
        t >>= 8;
    }

    unsigned int len = 0;
    unsigned char digest[EVP_MAX_MD_SIZE];
    HMAC(EVP_sha1(), key.data(), key.size(), t_bytes, sizeof(t_bytes), digest, &len);

    int offset = digest[len - 1] & 0x0F;
    uint32_t binary = ((digest[offset] & 0x7F) << 24) |
                      ((digest[offset + 1] & 0xFF) << 16) |
                      ((digest[offset + 2] & 0xFF) << 8) |
                      (digest[offset + 3] & 0xFF);

    uint32_t otp = binary % 1000000;
    std::ostringstream ss;
    ss << std::setw(digits) << std::setfill('0') << otp;
    return ss.str();
}

bool validate_totp(const std::string &otp, const std::string &b32_secret, uint64_t time_step, int digits) {
    std::string generated_totp = generate_totp(b32_secret, time_step, digits);
    std::cout << "Generated " << generated_totp << " versus " << otp << std::endl;
    if (otp.empty()) {
        return false;
    }
    return otp == generated_totp;
}

std::string generate_random_string(size_t length) {
    const std::string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, charset.size() - 1);

    std::string s;
    s.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        s += charset[dist(gen)];
    }
    return s;

}
