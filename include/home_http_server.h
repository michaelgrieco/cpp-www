
#ifndef __HOME_HTTP_SERVER_H__
#define __HOME_HTTP_SERVER_H__

#include "http_server.h"

class home_http_server : public http_server {

public:

    // inherit base constructors
    using http_server::http_server;

protected:

    // override to register home-server routes
    void construct_route_tree() override;

private:

    // Callback functions
    static void get_file(http_request_t request);
    static void index(http_request_t request);
    static void get_form(http_request_t request);
    static void get_form_search(http_request_t request);
    static void form_search(http_request_t request);
    static void post_form(http_request_t request);
    static void get_form_completion(http_request_t request);

};

#endif // __HOME_HTTP_SERVER_H__
