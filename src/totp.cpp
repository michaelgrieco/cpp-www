#include "totp.h"

#include <iostream>
#include <fstream>
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

//std::string generate_totp(const std::string &b32_secret, uint64_t time_step, int digits) {
uint32_t generate_totp(const std::string &b32_secret, uint64_t time_step, int digits, int64_t window_offset) {
    std::string key = base32_decode(b32_secret);
    if (key.empty()) {
        return 0;
    }

    uint64_t t = (uint64_t)((int64_t)(static_cast<uint64_t>(std::time(nullptr)) / time_step) + window_offset);
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

    uint32_t totp = binary % (uint32_t)std::pow(10, digits);
    //std::ostringstream ss;
    //ss << std::setw(digits) << std::setfill('0') << totp;
    //return ss.str();
    return totp;
}

//bool validate_totp(const std::string &totp, const std::string &b32_secret, uint64_t time_step, int digits) {
bool validate_totp(uint32_t totp, const std::string &b32_secret, uint64_t time_step, int digits, uint32_t time_window_expansion) {
    uint32_t generated_totp = generate_totp(b32_secret, time_step, digits, 0);
    //std::cout << "Generated " << generated_totp << " versus " << totp << std::endl;
    if (totp == generated_totp) return true;

    // expand window
    for (uint32_t i = 1; i <= time_window_expansion; ++i) {
        generated_totp = generate_totp(b32_secret, time_step, digits, (int32_t)i);
        //std::cout << "Generated " << generated_totp << " versus " << totp << " with offset " << i << std::endl;
        if (totp == generated_totp) return true;

        generated_totp = generate_totp(b32_secret, time_step, digits, -((int32_t)i));
        //std::cout << "Generated " << generated_totp << " versus " << totp << " with offset " << (-i) << std::endl;
        if (totp == generated_totp) return true;
    }

    return false;
}

totp_security_level_e validate_totp_permissions(uint32_t totp, uint32_t acceptable_permission_levels, uint64_t time_step, int digits, uint32_t time_window_expansion) {
    std::string level_secret;
    std::ifstream totp_secret_file(TOTP_SECRET_FILE);
    if (!totp_secret_file.good()) {
        std::cerr << "Unable to find secret file at " << TOTP_SECRET_FILE << std::endl;
        return TOTP_UNAUTHORIZED;
    }

    // scan from least powerful to most powerful
    // *should* not get any conflicts but want to return the less permissive one in that case
    uint32_t test_level = (uint32_t)TOTP_PLACEHOLDER >> 1;
    while (test_level) {
        // read secret from file
        if (!std::getline(totp_secret_file, level_secret)) {
            break;
        }

        // only test if this level is acceptable
        if (test_level & acceptable_permission_levels) {
            if (validate_totp(totp, level_secret, time_step, digits, time_window_expansion)) {
                return (totp_security_level_e)test_level;
            }
        }

        // test next level
        test_level >>= 1;
    }

    return TOTP_UNAUTHORIZED;
}

totp_security_level_e validate_totp_permissions(std::string totp, uint32_t acceptable_permission_levels, uint64_t time_step, int digits, uint32_t time_window_expansion) {
    try {
        uint32_t totp_i = std::stoi(totp);
        return validate_totp_permissions(totp_i, acceptable_permission_levels, time_step, digits, time_window_expansion);
    } catch (const std::exception &e) {
        return TOTP_UNAUTHORIZED;
    }
}

totp_security_level_e validate_totp_permissions(route_variable_t totp, uint32_t acceptable_permission_levels, uint64_t time_step, int digits, uint32_t time_window_expansion) {
    return totp.type == INT
        ? validate_totp_permissions(totp.value.i, acceptable_permission_levels, time_step, digits, time_window_expansion)
        : validate_totp_permissions(totp.to_string(), acceptable_permission_levels, time_step, digits, time_window_expansion);
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
