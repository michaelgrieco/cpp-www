#ifndef __TOTP_H__
#define __TOTP_H__

#include <string>
#include <cstdint>

enum totp_security_level_e {
    TOTP_ADMIN,
    TOTP_FILE_UPLOADER,
    TOTP_DATA_READER
};

/**
 * Decodes a base-32 RFC 4648 encoded string into raw bytes.
 */
std::string base32_decode(const std::string &input);

/**
 * Generates an RFC 6238 TOTP string (SHA1 / HMAC).
 *
 * @param b32_secret Base-32 encoded secret key.
 * @param time_step Time step window in seconds (default: 30).
 * @param digits Number of OTP digits (default: 6).
 * @return Formatted OTP string with leading zeroes.
 */
std::string generate_totp(const std::string &b32_secret, uint64_t time_step = 30, int digits = 6);

/**
 * Validates a given OTP against the generated TOTP for a given secret.
 */
bool validate_totp(const std::string &otp, const std::string &b32_secret, uint64_t time_step = 30, int digits = 6);



/**
 * Generates a random alphanumeric string of specified length.
 */
std::string generate_random_string(size_t length);

#endif // __TOTP_H__