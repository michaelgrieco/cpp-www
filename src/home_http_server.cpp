
#include "home_http_server.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

#include "http.h"
#include "schema.h"
#include "route.h"
#include "totp.h"

// ==============================
// ===== Callback functions =====
// ==============================

void home_http_server::get_file(http_request_t request) {
    // validate file name
    std::string file_name = request.vars["www"].value.s;
    bool seen_dot = false;
    for (char c : file_name) {
        bool is_dot = c == '.';
        if (seen_dot && is_dot) {
            respond_not_found(request);
            return;
        }
        seen_dot = is_dot;
    }

    // get file path and extension
    std::string file_path = "www/" + file_name;
    std::string file_type = "";
    int dot_idx = file_name.find_last_of('.');
    if (dot_idx == -1) {
        file_type += "plain";
    }
    else {
        file_type += file_name.substr(dot_idx + 1);
    }
    std::cout << "  File name is " << file_name << " with type " << file_type << std::endl;

    // list directory contents or stream file
    struct stat path_stat;
    stat(file_path.c_str(), &path_stat);
    if (S_ISDIR(path_stat.st_mode)) {
        // construct HTML
        std::ostringstream ss;
        ss << "<p>Directory contents for " << file_path << ":</p><ul>" << "\r\n";
        DIR *dir = opendir(file_path.c_str());
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                std::string path = entry->d_name;
                if (path == "." || path == "..") continue;
                std::cout << "Directory entry " << path << std::endl;
                ss << "<li><a href=\"/www/" << file_name << "/" << path << "\">" << path << "</a></li>" << "\r\n";
            }
            closedir(dir);
        }
        ss << "</ul>";
        std::string str = ss.str();
        std::cout << "HTML " << str << std::endl;

        // compute total size
        int length = get_file_size("www/directory.prefix.html")
            + str.length()
            + get_file_size("www/directory.postfix.html");
        std::cout << "Total length: " << get_file_size("www/directory.prefix.html") << ", " << str.length() << ", " << get_file_size("www/directory.postfix.html") << " for a sum of " << length << std::endl;

        // send response
        send_http_response_status(request.ssl, HTTP_OKAY);
        send_http_response_headers(request.ssl, {
            {"Content-Type", "text/html"},
            {"Content-Length", std::to_string(length)}
        }, true);
        send_http_response_file(request.ssl, "text/html", "www/directory.prefix.html", false);
        send_http_response_body(request.ssl, "text/html", str, false);
        send_http_response_file(request.ssl, "text/html", "www/directory.postfix.html", false);
    }
    // stream file
    else {
        std::ifstream file(file_path);
        if (file.good()) {
            send_http_response_status(request.ssl, HTTP_OKAY);
            send_http_response_headers(request.ssl, {});
            send_http_response_file(request.ssl, file_type, file_path);
        } else {
            respond_not_found(request);
        }
        file.close();
    }
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

// =================
// ===== Forms =====
// =================

void home_http_server::form_search(http_request_t request) {
    variables_t form_vars;
    std::cout << "Body is " << request.body << std::endl;
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
    std::string form_dir = "data/forms/" + form_id;
    std::string file_path = form_dir + "/fields.txt";
    std::vector<schema_field_t> schema;
    if (!read_schema_file(file_path, &schema)) {
        respond_not_found(request);
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
        switch (field.optionality) {
        // handle empty value
        case SCHEMA_OPTIONALITY_REQUIRED:
            if (!form_vars.count(field.name) || form_vars[field.name].is_null()) {
                error_ss << "Field " << field.name << " is required. ";
            }
            break;
        default:
            break;
        }

        std::string validation_file_path = form_dir + "/validation/" + field.name +  ".txt";
        std::ifstream validation_file(validation_file_path);
        if (validation_file.good()) {
            std::vector<std::string> valid_values;
            std::getline(validation_file, line); // skip comment line
            while (std::getline(validation_file, line)) {
                valid_values.push_back(line);
            }

            // check input value for matches
            bool match = false;
            if (form_vars[field.name].type == LIST) {
                for (auto iter  = form_vars[field.name].value.l->begin();
                          iter != form_vars[field.name].value.l->end();
                          iter++) {
                    std::string value = (*iter).to_string(false);
                    match = false;
                    for (std::string valid_value : valid_values) {
                        if (value == valid_value) {
                            match = true;
                            break;
                        }
                    }
                    if (!match) {
                        break;
                    }
                }
            }
            else {
                std::string value = form_vars[field.name].to_string(false);
                match = false;
                for (std::string valid_value : valid_values) {
                    if (value == valid_value) {
                        match = true;
                        break;
                    }
                }
            }
            
            if (!match) {
                error_ss << "Value for field " << field.name << " is not recognized.";
            }
        }
    }
    std::string error_string = error_ss.str();

    // process schema
    if (error_string.length() == 0) {
        for (schema_field_t field : schema) {
            std::string value = "";
            switch (field.optionality) {
            case SCHEMA_OPTIONALITY_AUTOFILL:
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
                break;
            default:
                value = form_vars[field.name].to_string(true);
                break;
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
        file_path = form_dir + "/results.csv";
        std::cout << "  Writing data to " << file_path << std::endl << ss.str() << std::endl;
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

void home_http_server::get_form_data(http_request_t request) {
    std::string form_id = request.vars["id"].value.s;
    std::cout << "Get form data for " << form_id << std::endl;

    // read secret
    std::string totp_secret;
    std::ifstream totp_secret_file(TOTP_SECRET_FILE);
    if (totp_secret_file.good()) {
        std::getline(totp_secret_file, totp_secret);
    } else {
        send_http_response_status(request.ssl, HTTP_UNAUTHORIZED);
        send_http_response_headers(request.ssl, {});
        send_http_response_body(request.ssl, "text/html", "<p>Bad OTP</p>");
        return;
    }

    // authenticate
    variables_t form_vars;
    parse_query_args(request.body, form_vars);
    std::string totp = form_vars["totp"].to_string();
    std::cout << "TOTP is " << totp << std::endl;
    if (!validate_totp(totp, totp_secret)) {
        send_http_response_status(request.ssl, HTTP_UNAUTHORIZED);
        send_http_response_headers(request.ssl, {});
        send_http_response_body(request.ssl, "text/html", "<p>Bad OTP</p>");
        return;
    }

    // read data into response
    std::string file_path = "data/forms/" + form_id + "/results.csv";
    std::ifstream file(file_path);
    if (file.good()) {
        send_http_response_status(request.ssl, HTTP_OKAY);
        send_http_response_headers(request.ssl, {});
        send_http_response_file(request.ssl, "text/csv", file_path);
        file.close();
    } else {
        respond_not_found(request);
    }

}

// ===================
// ===== Secrets =====
// ===================

void home_http_server::post_secret_file(http_request_t request) {
    // parse form
    std::map<std::string, std::string> form = parse_multipart_args(request, {
        { "otp",  { "otp",  SCHEMA_OPTIONALITY_REQUIRED, SCHEMA_TYPE_STRING } },
        { "file", { "file", SCHEMA_OPTIONALITY_REQUIRED, SCHEMA_TYPE_FILE   } }
    });

    // validate OTP secret
    std::string totp_secret;
    std::ifstream totp_secret_file(TOTP_SECRET_FILE);
    if (totp_secret_file.good()) {
        std::getline(totp_secret_file, totp_secret);
        std::cout << "Compare input " << form["otp"] << " to " << totp_secret << std::endl;
    } else {
        send_http_response_status(request.ssl, HTTP_UNAUTHORIZED);
        send_http_response_headers(request.ssl, {});
        send_http_response_body(request.ssl, "text/html", "<p>Bad OTP</p>");
        return;
    }

    // generate random key
    std::string key = generate_random_string(8);
    
    // generate new secret name
    #define MAX_COLLISIONS 2
    std::string name = "";
    std::string file_path;
    for (int i = 0; i < MAX_COLLISIONS; ++i) {
        name = generate_random_string(8);
        file_path = "data/secret/" + name;
        std::ifstream f(file_path);
        if (!f.good()) {
            break;
        }
        f.close();
    }

    // construct redirect URL
    std::string redirect_url = "/secret/created?secret_id=" + name;

    send_http_response_status(request.ssl, HTTP_MOVED_PERMANENTLY);
    send_http_response_headers(request.ssl, {{"Location", redirect_url}});
    send_http_response_body(request.ssl, "text", "");
    return;
}

void home_http_server::post_secret(http_request_t request) {
    
    // get secret from form
    variables_t form_vars;
    parse_query_args(request.body, form_vars);
    if (!form_vars.count("secret")) {
        send_http_response_status(request.ssl, HTTP_BAD_REQUEST);
        send_http_response_headers(request.ssl, {});
        send_http_response_body(request.ssl, "text", "No secret provided");
        return;
    }
    std::string secret_value = form_vars["secret"].to_string();

    // generate random key
    std::string key = generate_random_string(8);
    
    // generate new secret name
    #define MAX_COLLISIONS 2
    std::string name = "";
    std::string file_path;
    for (int i = 0; i < MAX_COLLISIONS; ++i) {
        name = generate_random_string(8);
        file_path = "data/secret/" + name;
        std::ifstream f(file_path);
        if (!f.good()) {
            break;
        }
        f.close();
    }
    
    // write secret
    std::ofstream file(file_path);
    if (file.is_open()) {
        file << key << "\n";
        file << secret_value;
        file.close();
    }

    // construct redirect URL
    std::string redirect_url = "/secret/created?secret_id=" + name + key;

    send_http_response_status(request.ssl, HTTP_MOVED_PERMANENTLY);
    send_http_response_headers(request.ssl, {{"Location", redirect_url}});
    send_http_response_body(request.ssl, "text", "");
}

void home_http_server::fetch_secret(http_request_t request) {
    // get secret from URL
    std::string secret_value = request.vars["secret"].to_string();
    std::cout << "Fetch secret " << secret_value << std::endl;

    if (secret_value.length() < 16) {
        respond_not_found(request);
        return;
    }

    std::string name = secret_value.substr(0, 8);
    std::string key = secret_value.substr(8, 8);

    // Get file
    std::string file_path = "data/secret/" + name;
    std::ifstream file(file_path);
    if (!file.good()) {
        respond_not_found(request);
        return;
    }
    int file_length = get_file_size(file);

    // Validate key
    std::string first_line;
    if (!std::getline(file, first_line) || first_line != key) {
        file.close();
        respond_not_found(request);
        return;
    }

    // Read secret
    std::stringstream rest_of_file;
    rest_of_file << file.rdbuf();
    file.close();

    // Overwrite with zeros and delete file
    std::ofstream overwrite_file(file_path, std::ios::binary | std::ios::trunc);
    if (overwrite_file.is_open()) {
        std::string zeros(file_length, '\0');
        overwrite_file.write(zeros.data(), file_length);
        overwrite_file.close();
    }
    std::remove(file_path.c_str());

    send_http_response_status(request.ssl, HTTP_OKAY);
    send_http_response_headers(request.ssl, {});
    send_http_response_body(request.ssl, "text", rest_of_file.str());
}

void home_http_server::not_found(http_request_t request) {
    send_http_response_status(request.ssl, HTTP_NOT_FOUND);
    send_http_response_headers(request.ssl, {});
    send_http_response_body(request.ssl, "text", "404 Not Found");
}

// =====================================
// ===== Route tree (home-server) ======
// =====================================

void home_http_server::construct_route_tree() {

    // Top-level node
    route_node_t *root_url = create_static_route_node(&route_nodes, nullptr, "index");
    std::cout << "root_url: " << (void*)root_url << std::endl;
    root_ptr = root_url;
    root_url->callbacks.insert({HTTP_GET, index});
    
    // File endpoint
    route_node_t *www_url = create_final_static_route_node(&route_nodes, root_url, "www");
    std::cout << "www_url: " << (void*)www_url << std::endl;
    www_url->callbacks.insert({HTTP_GET, get_file});

    // Example ID endpoint
    route_node_t *id_url = create_variable_route_node(&route_nodes, root_url, "id", INT);
    std::cout << "id_url: " << (void*)id_url << std::endl;
    root_url->callbacks.insert({HTTP_GET, index});

    // =================
    // ===== Forms =====
    // =================

    // Form base route
    route_node_t *form_url = create_static_route_node(&route_nodes, root_url, "form");
    std::cout << "form_url: " << (void*)form_url << std::endl;
    form_url->get_request_file = "www/search_forms.html";
    form_url->callbacks.insert({HTTP_POST, form_search});

    // Form completion route
    route_node_t *form_completion_url = create_static_route_node(&route_nodes, form_url, "submitted");
    std::cout << "form_completion_url: " << (void*)form_completion_url << std::endl;
    form_completion_url->get_request_file = "www/form_completion.html";

    // Form route
    route_node_t *form_id_url = create_variable_route_node(&route_nodes, form_url, "id", STRING);
    std::cout << "form_id_url: " << (void*)form_id_url << std::endl;
    form_id_url->get_request_file = "www/form.html";
    form_id_url->callbacks.insert({HTTP_POST, post_form});

    // Form data route
    route_node_t *form_data_url = create_static_route_node(&route_nodes, form_id_url, "data");
    std::cout << "form_data_url: " << (void*)form_data_url << std::endl;
    form_data_url->get_request_file = "www/post_otp_form.html";
    form_data_url->callbacks.insert({HTTP_POST, get_form_data});

    // ===================
    // ===== Secrets =====
    // ===================

    route_node_t *create_secret_form = create_static_route_node(&route_nodes, root_url, "secret");
    std::cout << "create_secret_form: " << (void*)create_secret_form << std::endl;
    create_secret_form->get_request_file = "www/create_secret.html";
    create_secret_form->callbacks.insert({HTTP_POST, post_secret});

    route_node_t *secret_created = create_static_route_node(&route_nodes, create_secret_form, "created");
    std::cout << "secret_created: " << (void*)secret_created << std::endl;
    secret_created->get_request_file = "www/secret_created.html";
    
    route_node_t *create_secret_file_form = create_static_route_node(&route_nodes, create_secret_form, "file");
    std::cout << "create_secret_file_form: " << (void*)create_secret_file_form << std::endl;
    create_secret_file_form->get_request_file = "www/create_secret_file.html";
    create_secret_file_form->callbacks.insert({HTTP_POST, post_secret_file});

    route_node_t *get_secret = create_variable_route_node(&route_nodes, create_secret_form, "secret", STRING);
    std::cout << "get_secret: " << (void*)get_secret << std::endl;
    get_secret->callbacks.insert({HTTP_GET, fetch_secret});
    
}
