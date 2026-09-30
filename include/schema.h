
#ifndef __SCHEMA_H__
#define __SCHEMA_H__

#include <string>
#include <map>
#include <vector>
#include <iostream>

// Possible values for optionality
enum schema_field_desc_optionality_e {
    SCHEMA_OPTIONALITY_AUTOFILL = 'a',
    SCHEMA_OPTIONALITY_OPTIONAL = 'o',
    SCHEMA_OPTIONALITY_REQUIRED = 'r'
};

// Possible values for value type
enum schema_field_desc_type_e {
    SCHEMA_TYPE_NUMBER = 'n',
    SCHEMA_TYPE_STRING = 's',
    SCHEMA_TYPE_DATE = 'd',
    SCHEMA_TYPE_FILE = 'f',
    SCHEMA_TYPE_NUMBER_LIST = 'N',
    SCHEMA_TYPE_STRING_LIST = 'S',
    SCHEMA_TYPE_DATE_LIST = 'D'
};

// Fields to parse and manipulate
typedef struct {
    std::string name;
    schema_field_desc_optionality_e optionality;
    schema_field_desc_type_e type;
} schema_field_t;

bool read_schema_file(std::string file_path, std::vector<schema_field_t> *out);
bool schema_field_is_list(schema_field_t field);

#endif // __SCHEMA_H__
