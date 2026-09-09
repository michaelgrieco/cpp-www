
#include "home_http_server.h"

#include <fstream>
#include <sstream>
#include <iostream>

#include "http.h"
#include "schema.h"
#include "route.h"

// ==============================
// ===== Callback functions =====
// ==============================

void home_http_server::get_file(http_request_t request) {
    std::cout << "  File callback" << std::endl;

    // get file name and extension
    std::string file_name = request.vars["www"].value.s;
    std::string file_path = "www/" + file_name;
    std::string file_type = "text/";
    int dot_idx = file_name.find_last_of('.');
    if (dot_idx == -1) {
        file_type += "plain";
    }
    else {
        file_type += file_name.substr(dot_idx + 1);
    }
    std::cout << "  File name is " << file_name << " with type " << file_type << std::endl;
    
    // stream file
    std::ifstream file(file_path);
    if (file.good()) {
        send_http_response_status(request.ssl, HTTP_OKAY);
        send_http_response_headers(request.ssl, {});
        send_http_response_file(request.ssl, file_type, file_path);
    } else {
        send_http_response_status(request.ssl, HTTP_NOT_FOUND);
        send_http_response_headers(request.ssl, {});
        send_http_response_body(request.ssl, "text", "404 Not Found");
    }
    file.close();
}

// index callback
void home_http_server::index(http_request_t request) {
    std::cout << "  Index callback" << std::endl;

    std::cout << "  Variables:" << std::endl;
    for (variable_entry_t entry : request.vars) {
        std::cout << "    " << entry.first << ": " << entry.second.to_string() << std::endl;
    }

    std::cout << "  Body:" << std::endl;
    variables_t form_vars;
    parse_query_args(request.body, form_vars);
    for (variable_entry_t entry : form_vars) {
        std::cout << "    " << entry.first << ": " << entry.second.to_string() << std::endl;
    }

    send_http_response_status(request.ssl, HTTP_OKAY);
    send_http_response_headers(request.ssl, {});
    send_http_response_file(request.ssl, "text/html", "www/index.html");
}

void home_http_server::get_form(http_request_t request) {
    std::cout << "  Get form callback" << std::endl;

    send_http_response_status(request.ssl, HTTP_OKAY);
    send_http_response_headers(request.ssl, {});
    send_http_response_file(request.ssl, "text/html", "www/form.html");
}

void home_http_server::get_form_search(http_request_t request) {
    std::cout << "  Get form search callback" << std::endl;

    send_http_response_status(request.ssl, HTTP_OKAY);
    send_http_response_headers(request.ssl, {});
    send_http_response_file(request.ssl, "text/html", "www/search_forms.html");
}

void home_http_server::form_search(http_request_t request) {
    variables_t form_vars;
    parse_query_args(request.body, form_vars);
    std::string form_id = form_vars["name"].value.s;
    std::string form_url = "/form/" + form_id;
    std::cout << "  Redirecting to form " << form_id << std::endl;

    send_http_response_status(request.ssl, HTTP_MOVED_PERMANENTLY);
    send_http_response_headers(request.ssl, {{"Location", form_url}});
    send_http_response_body(request.ssl, "text", "");
}

void home_http_server::post_form(http_request_t request) {
    std::cout << "  Post form callback" << std::endl;
    std::string form_id = request.vars["id"].value.s;
    variables_t form_vars;

    // read schema
    std::string file_path = "data/" + form_id + ".fields";
    std::vector<schema_field_t> schema;
    if (!read_schema_file(file_path, &schema)) {
        send_http_response_status(request.ssl, HTTP_NOT_FOUND);
        send_http_response_headers(request.ssl, {});
        send_http_response_body(request.ssl, "text", "404 Not Found");
        return;
    }

    // instantiate lists in form
    for (schema_field_t field : schema) {
        if (schema_field_is_list(field)) {
            form_vars[field.name] = route_variable_t(LIST);
            form_vars[field.name].value.l = new std::vector<route_variable_t>();
        }
    }

    // parse form body
    parse_query_args(request.body, form_vars);

    // validate schema
    bool first = true;
    std::string line;
    std::ostringstream error_ss;
    std::ostringstream ss;
    for (schema_field_t field : schema) {
        // handle empty value
        if (!form_vars.count(field.name) || form_vars[field.name].is_null()) {
            if (field.desc.optionality == SCHEMA_OPTIONALITY_REQUIRED) {
                error_ss << "Field " << field.name << " is required. ";
                continue;
            }
        }
    }
    std::string error_string = error_ss.str();

    // process schema
    if (error_string.length() == 0) {
        for (schema_field_t field : schema) {
            std::string value = "";
            if (field.desc.optionality == SCHEMA_OPTIONALITY_AUTOFILL) {
                if (field.name == "__time__") {
                    // Example: 09/08/26 22:01:24
                    #define TIME_FORMAT "%m/%d/%y %H:%M:%S"
                    #define TIME_STR_LENGTH 17
                    char time_buf[TIME_STR_LENGTH + 1];
                    std::tm *tm_info = std::localtime(&request.req_time);
                    std::strftime(time_buf, sizeof(time_buf), TIME_FORMAT, tm_info);
                    time_buf[TIME_STR_LENGTH] = '\0';
                    value = "\"" + std::string(time_buf) + "\"";
                }
            }
            else {
                value = form_vars[field.name].to_string(true);
            }
            
            // Save value
            if (first) {
                first = false;
            }
            else {
                ss << ",";
            }
            ss << value;
        }
    }

    // free memory
    for (schema_field_t field : schema) {
        if (schema_field_is_list(field)) {
            delete form_vars[field.name].value.l;
            form_vars[field.name].value.l = nullptr;
        }
    }

    // send response
    std::cout << "  Completed form parsing with error? ";
    std::cout << error_string.length() << std::endl;
    if (error_string.length() > 0) {
        std::cout << "  Returning error" << std::endl << error_string << std::endl;
        send_http_response_status(request.ssl, HTTP_BAD_REQUEST);
        send_http_response_headers(request.ssl, {});
        send_http_response_body(request.ssl, "text", error_string);
    }
    else {
        std::cout << "  Writing data" << std::endl << ss.str() << std::endl;
        file_path = "data/" + form_id + ".csv";
        try {
            std::ofstream file(file_path, std::ios_base::app);
            file << ss.str() << std::endl;
            file.close();
        } catch (const std::exception &e) {
            std::cout << "  Error writing data: " << e.what() << std::endl;
            send_http_response_status(request.ssl, HTTP_INTERNAL_ERROR);
            send_http_response_headers(request.ssl, {});
            send_http_response_body(request.ssl, "text", "Could not write form data");
        }

        send_http_response_status(request.ssl, HTTP_MOVED_PERMANENTLY);
        send_http_response_headers(request.ssl, {{"Location", "/form/submitted"}});
        send_http_response_body(request.ssl, "text", "");
    }
}

void home_http_server::get_form_completion(http_request_t request) {
    std::cout << "  Form completion callback" << std::endl;

    send_http_response_status(request.ssl, HTTP_OKAY);
    send_http_response_headers(request.ssl, {});
    send_http_response_file(request.ssl, "text/html", "www/form_completion.html");
}

// =====================================
// ===== Route tree (home-server) ======
// =====================================

void home_http_server::construct_route_tree() {

    // Top-level node
    route_node_t *root_url = create_static_route_node(&route_nodes, nullptr, "index");
    root_ptr = root_url;
    root_url->callbacks.insert({HTTP_GET, index});
    
    // File endpoint
    route_node_t *www_url = create_final_static_route_node(&route_nodes, root_url, "www");
    www_url->callbacks.insert({HTTP_GET, get_file});

    // Example ID endpoint
    create_variable_route_node(&route_nodes, root_url, "id", INT);
    root_url->callbacks.insert({HTTP_GET, index});

    // Form base route
    route_node_t *form_url = create_static_route_node(&route_nodes, root_url, "form");
    form_url->callbacks.insert({HTTP_GET, get_form_search});
    form_url->callbacks.insert({HTTP_POST, form_search});

    // Form completion route
    route_node_t *form_completion_url = create_static_route_node(&route_nodes, form_url, "submitted");
    form_completion_url->callbacks.insert({HTTP_GET, get_form_completion});

    // Form route
    route_node_t *form_id_url = create_variable_route_node(&route_nodes, form_url, "id", STRING);
    form_id_url->callbacks.insert({HTTP_GET, get_form});
    form_id_url->callbacks.insert({HTTP_POST, post_form});
}
