
#include "route.h"
#include "http.h"

#include <string>
#include <map>
#include <vector>
#include <algorithm>

// ====================================
// ===== Route variable functions =====
// ====================================

route_variable_t::route_variable_t() : type(STATIC), value() {}

route_variable_t::route_variable_t(const route_variable_t& o) : type(o.type), value() {
    switch (o.type) {
        case INT:    value.i = o.value.i; break;
        case FLOAT:  value.f = o.value.f; break;
        case STRING: new (&value.s) std::string(o.value.s); break;
        case LIST:   value.l = o.value.l; break;
        case MAP:    value.m = o.value.m; break;
        default:     break;
    }
}

route_variable_t::route_variable_t::~route_variable_t() {
    if (type == STRING) value.s.~basic_string();
}

route_variable_t::route_variable_t(route_variable_type_e type) : type(type), value() {
    switch (type) {
        case LIST: new (&value.l) std::vector<route_variable_t>(); break;
        case MAP:  new (&value.m) std::map<std::string, route_variable_t>(); break;
        default:   break;
    }
}

route_variable_t::route_variable_t(int i) : type(INT), value() { value.i = i; }
route_variable_t::route_variable_t(float f) : type(FLOAT), value() { value.f = f; }
route_variable_t::route_variable_t(std::string s) : type(STRING), value() { new (&value.s) std::string(s); }
route_variable_t::route_variable_t(char *s) : type(STRING), value() { new (&value.s) std::string(s); }
route_variable_t::route_variable_t(std::vector<route_variable_t> *l) : type(LIST), value() { value.l = l; }
route_variable_t::route_variable_t(std::map<std::string, route_variable_t> *m) : type(MAP), value() { value.m = m; }

bool route_variable_t::is_null() {
    switch (type) {
        case INT:    return value.i == 0;
        case FLOAT:  return value.f == 0.0f;
        case STRING: return value.s.length() == 0;
        case LIST:   return !value.l || value.l->size() == 0;
        case MAP:    return !value.m || value.m->size() == 0;
        default: return true;
    }
}

route_variable_t& route_variable_t::operator=(const route_variable_t& o) {
    if (this == &o) return *this;
    // Destroy active string if we're replacing it
    if (type == STRING) value.s.~basic_string();
    type = o.type;
    switch (o.type) {
        case INT:    value.i = o.value.i; break;
        case FLOAT:  value.f = o.value.f; break;
        case STRING: new (&value.s) std::string(o.value.s); break;
        case LIST:   value.l = o.value.l; break;
        case MAP:    value.m = o.value.m; break;
        default: break;
    }
    return *this;
}

std::string route_variable_t::to_string(bool wrap_in_quotes) {
    std::ostringstream ss;
    bool first = true;
    switch (type) {
        case INT:    return std::to_string(value.i);
        case FLOAT:  return std::to_string(value.f);
        case STRING: return wrap_in_quotes ? ("\"" + value.s + "\"") : value.s;
        case LIST:
            if (!value.l) return "";

            if (wrap_in_quotes) {
                ss << '"';
            }
            ss << '[';
            for (auto iter = value.l->begin(); iter != value.l->end(); iter++) {
                route_variable_t v = *iter;
                if (!first) {
                    ss << ", ";
                }
                else {
                    first = false;
                }

                if (v.type == STRING) {
                    ss << "'" << v.value.s << "'";
                }
                else {
                    ss << v.to_string();
                }
            }
            ss << ']';
            if (wrap_in_quotes) {
                ss << '"';
            }
            return ss.str();
        case MAP:
            if (!value.m) return "";

            if (wrap_in_quotes) {
                ss << '"';
            }
            ss << '{';
            for (auto iter = value.m->begin(); iter != value.m->end(); iter++) {
                std::pair<std::string, route_variable_t> p = *iter;
                if (!first) {
                    ss << ", ";
                }
                else {
                    first = false;
                }

                ss << "\"" << p.first << "\": ";
                if (p.second.type == STRING) {
                    ss << "'" << p.second.value.s << "'";
                }
                else {
                    ss << p.second.to_string();
                }
            }
            ss << ']';
            if (wrap_in_quotes) {
                ss << '"';
            }
            return ss.str();
        default: return "";
    }
}

// =============================
// ===== Utility functions =====
// =============================

// Parse a variable from text
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

// Create a node in the route tree
route_node_t *create_route_node(std::vector<route_node_t*> *route_nodes, route_node_t *parent, std::string name, bool is_static, route_variable_type_e var_type) {
    // create node structure
    route_node_t *node = new route_node_t{
        .is_static = is_static,
        .variable = route_variable_t(var_type),
        .name = name,
        .children = {},
        .callbacks = callbacks_t(),
        .is_final_node = false
    };
    node->children.reserve(64);
    route_nodes->push_back(node);

    // add node to parent
    if (parent) {
        parent->children.push_back(node);
    }

    return node;
}

// Create a static node that terminates URL parsing and stores the rest of the URL with a variable
route_node_t *create_final_static_route_node(std::vector<route_node_t*> *route_nodes, route_node_t *parent, std::string name) {
    route_node_t *node = create_route_node(route_nodes, parent, name, true, STATIC);
    node->is_final_node = true;
    return node;
}

// Create a static node without a variable substitution
route_node_t *create_static_route_node(std::vector<route_node_t*> *route_nodes, route_node_t *parent, std::string name) {
    return create_route_node(route_nodes, parent, name, true, STATIC);
}

// Create a node with variable substitution
route_node_t *create_variable_route_node(std::vector<route_node_t*> *route_nodes, route_node_t *parent, std::string name, route_variable_type_e var_type) {
    return create_route_node(route_nodes, parent, name, false, var_type);
}
