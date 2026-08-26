
#include "route.h"

#include <string>
#include <map>
#include <vector>

// parse variable
route_variable_t parse_var(std::string arg) {
    try {
        std::size_t pos = 0;
        int i = std::stoi(arg, &pos);
        if (pos == arg.size()) {
            return route_variable_t(i);
        }
    } catch (...) {}

    try {
        std::size_t pos = 0;
        float f = std::stof(arg, &pos);
        if (pos == arg.size()) {
            return route_variable_t(f);
        }
    } catch (...) {}

    return route_variable_t(arg);
}
