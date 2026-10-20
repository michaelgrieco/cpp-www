
#ifndef __TOTP_H__
#define __TOTP_H__

#include <string>
#include <cstdint>
#include <map>

#include "route.h"

enum totp_security_level_e {
    TOTP_UNAUTHORIZED  = 0,
    TOTP_ADMIN         = 1,
    TOTP_FILE_UPLOADER = 2,
    TOTP_DATA_READER   = 4,
    TOTP_PLACEHOLDER   = 8
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
 * @param window_offset Offset for the time step for the generated code.
 * @return OTP.
 */
 //std::string generate_totp(const std::string &b32_secret, uint64_t time_step = 30, int digits = 6);
 uint32_t generate_totp(const std::string &b32_secret, uint64_t t, int digits = 6);
 
 /**
 * Validates a given OTP against the generated TOTP for a given secret.
 *
 * @param time_window_expansion Grace period to account for server clock offset.
 *        A value of one will accept the OTP for time window, time window - 1, and time window + 1
 */
//bool validate_totp(const std::string &otp, const std::string &b32_secret, uint64_t time_step = 30, int digits = 6);
uint64_t validate_totp(uint32_t otp, const std::string &b32_secret, uint64_t time_step = 30, int digits = 6, uint32_t time_window_expansion = 1);

class totp {

public:

    /**
     * @param time_step Time step window in seconds (default: 30).
     * @param digits Number of OTP digits (default: 6).
     * @param time_window_expansion Grace period to account for server clock offset.
     */
    totp(uint64_t time_step = 30, int digits = 6, uint32_t time_window_expansion = 1)
        : time_step(time_step), digits(digits), time_window_expansion(time_window_expansion) {}

    /**
     * Validates the requested security level of a given OTP.
     * On success, the matched time step is remembered for the matched level so the same OTP
     * (or an older one) cannot be reused for that level.
     *
     * @param otp Provided OTP.
     * @param acceptable_permission_levels The bitwise-OR combination of the acceptable permission level IDs.
     * @return The matched permission level of the OTP, TOTP_UNAUTHORIZED if no match.
     */
    totp_security_level_e validate_totp_permissions(uint32_t otp, uint32_t acceptable_permission_levels);
    totp_security_level_e validate_totp_permissions(std::string otp, uint32_t acceptable_permission_levels);
    totp_security_level_e validate_totp_permissions(route_variable_t otp, uint32_t acceptable_permission_levels);

    /**
     * @return The last authenticated time step for the given level, 0 if never authenticated.
     */
    uint64_t get_last_authenticated_time_step(totp_security_level_e level) const {
        auto it = last_authenticated_time_step.find(level);
        return it == last_authenticated_time_step.end() ? 0 : it->second;
    }

private:

    uint64_t time_step;
    int digits;
    uint32_t time_window_expansion;
    std::map<totp_security_level_e, uint64_t> last_authenticated_time_step;

};

/**
 * Generates a random alphanumeric string of specified length.
 */
std::string generate_random_string(size_t length);

#endif // __TOTP_H__
