#include <iostream>
#include <evhttp.h>
#include <string.h>
#include <limits.h>
#include <arpa/inet.h>
#include <event2/buffer.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

// Нужен sysloq

#define GET_FILE(path) ((path)[0] == '\0'? ".": (path))

struct pair {
    const void *first;
    const void *second;
};

static pair list_of_extensions[] =
{
    { ".txt", "text/plain" },
    { ".html", "text/html" },
    { ".jpeg", "image/jpeg" },
    { ".jpg", "image/jpeg" },
    { ".png", "image/png" },
    { nullptr, nullptr}
};

static const char *
get_extension(const char *path)
{
    const char *res = strrchr(path, '.');
    if (res == nullptr)
        return "application/octet-stream";
    
    for (int i = 0; list_of_extensions[i].first != nullptr; i++)
        if (strcmp(res, (const char *) list_of_extensions[i].first) == 0)
            return (const char *) list_of_extensions[i].second;

    return "application/octet-stream";
}

// extract path from URI
// returning value should be freed by caller
const char
*get_path(struct evhttp_request *req, void *ptr)
{
    const struct evhttp_uri *uri = evhttp_request_get_evhttp_uri(req);
    if (uri == nullptr)
        return nullptr;

    const char *path_undec = evhttp_uri_get_path(uri);
    if (path_undec == nullptr)
        return nullptr;

    const char *path_dec = evhttp_uridecode(path_undec, 0, nullptr);
    if (path_dec == nullptr)
        return nullptr;

    char *path = (char *) calloc(strlen(path_dec) + 1, sizeof(*path));
    char *cur_path = path;
    const char *cur_dec = path_dec;
    
    while (true) {
        const char *skip = strstr(cur_dec, "/./");
        const char *pred = strstr(cur_dec, "/../");
        if (skip == nullptr && pred == nullptr) {
            strcpy(cur_path, cur_dec);
            break;
        }
        if (pred == nullptr || (skip != nullptr && skip < pred)) {
            if (skip != cur_dec)
                strncpy(cur_path, cur_dec, skip - cur_dec);
            cur_path += (skip - cur_dec);
            cur_dec = skip + 2;
        } else {
            strncpy(cur_path, cur_dec, pred - cur_dec);
            cur_path += (pred - cur_dec);
            cur_dec = pred + 3;
            cur_path --;
            *cur_path = '\0';
            char *tmp = strrchr(path, '/');
            if (tmp == nullptr) {
                free((void *) path);
                free((void *) path_dec);
                return nullptr;
            }
            cur_path = tmp + 1;
            *cur_path = '\0';
        }
    }

    char *res = (char *) calloc(strlen(path), sizeof(*res));
    strcpy(res, path + 1);
    free((void *) path_dec);
    free((void *) path);
    return res;
}

//evhttp_request_get_connection(req); should try
void
post_request(evhttp_request *req, void *ptr)
{
    const char *path = get_path(req, ptr);
    if (path == nullptr) {
        printf("POST request. Bad URI.\n");
        evhttp_send_error(req, HTTP_BADREQUEST, nullptr);
        return;
    }

    struct evbuffer *input = evhttp_request_get_input_buffer(req);
    struct stat stsa;
    if (lstat(GET_FILE(path), &stsa) == 0) {
        printf("POST request. File (%s) already exists.\n", GET_FILE(path));
        evhttp_send_reply(req, HTTP_OK, "Already exists", nullptr);
        struct evbuffer *tmp = evbuffer_new();
        evbuffer_remove_buffer(input, tmp, evbuffer_get_length(input));
        evbuffer_free(tmp);
        free((void *) path);
        return;
    }

    FILE *file = fopen(path, "w");
    if (file == nullptr) {
        printf("POST request. Can't create file.\n");
        evhttp_send_error(req, HTTP_INTERNAL, nullptr);
        free((void *) path);
        return;
    }
    
    enum { BUF_MAX = 1024 };
    char buf[BUF_MAX];
    int size;
    while ((size = evbuffer_remove(input, buf, BUF_MAX)) > 0) {
        if (fwrite(buf, sizeof(*buf), size, file) != size * sizeof(*buf)) {
            evhttp_send_error(req, HTTP_INTERNAL, nullptr);
            free((void *) path);
            return;
        }
    }
    fclose(file);
    printf("POST request. Created \"%s\"\n", path);
    evhttp_send_reply(req, HTTP_OK, "File created", nullptr);
    free((void *) path);
}

void
get_request_dir(evhttp_request *req, void *ptr)
{
    struct evbuffer *output = evhttp_request_get_output_buffer(req); 
    const char *path = (char *) ptr;
    DIR *dir = opendir(GET_FILE(path));
    if (dir == nullptr) {
        printf("Error opening directrory %s\n", path);
        evhttp_send_error(req, HTTP_INTERNAL, nullptr);
        return;
    }
    evbuffer_add_printf(output, "<html>" "<head><title>entry for %s</title></head>" "<body>"
                        "<h1>entry for %s</h1>", path, path);
    struct dirent *dd;
    while ((dd = readdir(dir)) != nullptr) {
        if (dd->d_name[0] != '.') {
            evbuffer_add_printf(output, "<p><a href=\"");
            evbuffer_add_printf(output, (path[0] == '\0')? "": "/%s", path);
            evbuffer_add_printf(output, "/%s\">%s</a></p>", dd->d_name, dd->d_name);
        }
    }
    closedir(dir);
    evbuffer_add_printf(output, "</body></html>");
    
    struct evkeyvalq *headers = evhttp_request_get_output_headers(req);
    evhttp_add_header(headers, "Content-Type",  "text/html; charset=utf-8");
    
    evhttp_send_reply(req, HTTP_OK, "OK", output);
    printf("GET request for directory \"%s\"\n", GET_FILE(path));
}

void get_request_file(evhttp_request *req, void *ptr)
{
    struct evbuffer *output = evhttp_request_get_output_buffer(req); 
    const char *path = (char *) ((struct pair *) ptr)->first;
    int file = open(path, O_RDONLY);
    if (file < 0) {
        printf("Error opening file %s\n", path);
        evhttp_send_error(req, HTTP_INTERNAL, nullptr);
        return;
    }
    
    off_t *p_size = (off_t *) ((struct pair *) ptr)->second;
    evbuffer_add_file(output, file, 0, *p_size);

    struct evkeyvalq *headers = evhttp_request_get_output_headers(req);
    evhttp_add_header(headers, "Content-Type",  get_extension(path));
    
    evhttp_send_reply(req, HTTP_OK, "OK", output);
    printf("GET request for file \"%s\"\n", GET_FILE(path));
}

void
get_request(evhttp_request *req, void *ptr)
{
    const char *path = get_path(req, ptr);
    if (path == nullptr) {
        printf("GET request. Bad URI.\n");
        evhttp_send_error(req, HTTP_BADREQUEST, nullptr);
        return;
    }
    
    struct stat stsa;
    if (lstat(GET_FILE(path), &stsa) != 0) {
        printf("GET request. File (%s) no longer exists.\n", GET_FILE(path));
        evhttp_send_error(req, HTTP_NOTFOUND, nullptr);
        free((void *) path);
        return;
    }

    if (S_ISDIR(stsa.st_mode)) {
        get_request_dir(req, (void *) path);
    } else {
        struct pair tmp = (struct pair) { (void *) path, (void *) &stsa.st_size };
        get_request_file(req, (void *) &tmp);
    }   
    
    free((void *) path);
}

void
head_request(evhttp_request *req, void *ptr)
{
    const char *path = get_path(req, ptr);
    if (path == nullptr) {
        printf("HEAD request. Bad URI.\n");
        evhttp_send_error(req, HTTP_BADREQUEST, nullptr);
        return;
    }
    
    struct stat stsa;
    if (lstat(GET_FILE(path), &stsa) != 0) {
        printf("HEAD request. File (%s) no longer exists.\n", GET_FILE(path));
        evhttp_send_error(req, HTTP_NOTFOUND, nullptr);
        free((void *) path);
        return;
    }

    struct evkeyvalq *headers = evhttp_request_get_output_headers(req);
    if (S_ISDIR(stsa.st_mode)) {
        evhttp_add_header(headers, "Content-Type",  "text/html; charset=utf-8");
    } else {
        evhttp_add_header(headers, "Content-Type",  get_extension(path));
    }

    evhttp_send_reply(req, HTTP_OK, "OK", nullptr);
    printf("HEAD request for \"%s\"\n", GET_FILE(path));
    free((void *) path);
}

void
gen_request(evhttp_request *req, void *ptr)
{
    switch (evhttp_request_get_command(req)) {
    case EVHTTP_REQ_POST:
        post_request(req, ptr);
        return;
    case EVHTTP_REQ_GET:
        get_request(req, ptr);
        return;
    case EVHTTP_REQ_HEAD:
        head_request(req, ptr);
        return;
    default:
        printf("Unknown request. Ignored.\n");
        evhttp_send_error(req, HTTP_NOTIMPLEMENTED, nullptr); // impossible, i suppose
    }
}

int
parse_input(int argc, char **argv, char **h, char **p, char **d )
{
    if (argc != 4)
        return -1;
    
    for (int i = 1; i < 4; i++) {
        if (argv[i][0] != '-')
            return -1;
        
        switch (argv[i][1]) {
        case 'd': *d = &argv[i][2];
            break;
        case 'h': *h = &argv[i][2];
            break;
        case 'p': *p = &argv[i][2];
            break;
        default:
            return -1;
        }
    }

    int idx;
    sscanf(*p, "%*d%n", &idx);
    (*p)[idx] = '\0';
    return 0;
}

int
main(int argc, char **argv)
{
    char *ipstr, *portstr, *dirstr;
    if (parse_input(argc, argv, &ipstr, &portstr, &dirstr) != 0) {
         printf("Usage: %s -d <dir> -h <ip> -p <port>\n", argv[0]);
         exit(1);
    }

    if (chdir(dirstr) != 0) {
        printf("Can't access dir. Exit.\n");
        exit(1);
    }
    
    char *dir = (char *) calloc(PATH_MAX, sizeof(*dir));
    if (realpath(".", dir) == nullptr) {
        printf("Wrong path for directory. %s. Exit.\n", strerror(errno));
        free(dir);
        exit(1);
    }

    struct event_base *base = event_base_new();  
    struct evhttp *http = evhttp_new(base);
    
    evhttp_set_allowed_methods(http, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_HEAD);
    evhttp_set_gencb(http, gen_request, dir);

    if (evhttp_bind_socket(http, ipstr, atoi(portstr)) != 0) {
        printf("Bind error: %s.\nExit.\n", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        exit(1);
    }
    printf("Listening on %s:%s\n", ipstr, portstr);
    
    if (event_base_dispatch(base) != 0) {
        printf("dispatch error\n");
    }
    free(dir);
}
