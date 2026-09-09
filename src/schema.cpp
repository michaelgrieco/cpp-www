
#include "schema.h"

#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>

bool read_schema_file(std::string file_path, std::vector<schema_field_t> *out) {
    std::ifstream schema_file(file_path);
    if (!schema_file.good()) {
        return false;
    }

    std::string line;
    while (std::getline(schema_file, line)) {
        schema_field_t entry;

        entry.name = line;
        if (entry.name.length() == 0 || entry.name[0] == '#') {
            continue;
        }
        std::string desc;
        std::getline(schema_file, desc);
        if (desc.length() != 2) continue;
        entry.desc.optionality = (schema_field_desc_optionality_e)desc[0];
        entry.desc.type = (schema_field_desc_type_e)desc[1];

        out->push_back(entry);
    }
    schema_file.close();
    
    return true;
}

bool schema_field_is_list(schema_field_t field) {
    return field.desc.type >= 'A' && field.desc.type <= 'Z';
}
