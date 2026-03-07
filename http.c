/*
 * http.c - HTTP/HTTPS client with runtime-loaded OpenSSL for Solaris 7
 *
 * Uses raw BSD sockets for HTTP and loads OpenSSL via dlopen() at
 * runtime for HTTPS.  This avoids linking against libssl/libcrypto,
 * which causes the Solaris /usr/ccs/bin/ld linker to take hours
 * (or never finish) on slow SPARC hardware due to the massive
 * symbol table in OpenSSL 3.
 *
 * Link: -lsocket -lnsl -ldl
 */

#include "http.h"
#include "config.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <dlfcn.h>

/* ================================================================
 * RUNTIME-LOADED OPENSSL via dlopen()
 *
 * Instead of linking -lssl -lcrypto (which makes the Solaris ld
 * spend hours resolving OpenSSL 3's massive symbol table on a
 * 110MHz microSPARC-II), we dlopen() the libraries at runtime
 * and dlsym() only the ~20 functions we actually use.
 * ================================================================ */

/* Opaque SSL types (forward declarations, never defined here) */
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;
typedef struct ssl_method_st SSL_METHOD;

/* OpenSSL constants */
#define SPM_SSL_OP_NO_SSLv2              0x01000000L
#define SPM_SSL_OP_NO_SSLv3              0x02000000L
#define SPM_SSL_VERIFY_NONE              0
#define SPM_SSL_VERIFY_PEER              1
#define SPM_SSL_CTRL_OPTIONS             32
#define SPM_SSL_CTRL_SET_TLSEXT_HOSTNAME 55
#define SPM_X509_V_OK                    0
#define SPM_INIT_LOAD_SSL_STRINGS        0x00200000L
#define SPM_INIT_LOAD_CRYPTO_STRINGS     0x02000000L

/* Function pointer types */
typedef int          (*pfn_OPENSSL_init_ssl)(unsigned long long, void *);
typedef int          (*pfn_SSL_library_init)(void);
typedef void         (*pfn_SSL_load_error_strings)(void);
typedef void         (*pfn_OpenSSL_add_all_algorithms)(void);
typedef SSL_METHOD * (*pfn_TLS_client_method)(void);
typedef SSL_CTX *    (*pfn_SSL_CTX_new)(const SSL_METHOD *);
typedef long         (*pfn_SSL_CTX_ctrl)(SSL_CTX *, int, long, void *);
typedef int          (*pfn_SSL_CTX_load_verify_locations)(SSL_CTX *, const char *, const char *);
typedef void         (*pfn_SSL_CTX_set_verify)(SSL_CTX *, int, void *);
typedef void         (*pfn_SSL_CTX_free)(SSL_CTX *);
typedef SSL *        (*pfn_SSL_new)(SSL_CTX *);
typedef int          (*pfn_SSL_set_fd)(SSL *, int);
typedef long         (*pfn_SSL_ctrl)(SSL *, int, long, void *);
typedef int          (*pfn_SSL_connect)(SSL *);
typedef long         (*pfn_SSL_get_verify_result)(const SSL *);
typedef int          (*pfn_SSL_write)(SSL *, const void *, int);
typedef int          (*pfn_SSL_read)(SSL *, void *, int);
typedef int          (*pfn_SSL_shutdown)(SSL *);
typedef void         (*pfn_SSL_free)(SSL *);
typedef void         (*pfn_ERR_print_errors_fp)(FILE *);
typedef void         (*pfn_EVP_cleanup)(void);
typedef void         (*pfn_ERR_free_strings)(void);

/* Loaded function pointers */
static pfn_OPENSSL_init_ssl              dl_OPENSSL_init_ssl;
static pfn_SSL_library_init              dl_SSL_library_init;
static pfn_SSL_load_error_strings        dl_SSL_load_error_strings;
static pfn_OpenSSL_add_all_algorithms    dl_OpenSSL_add_all_algorithms;
static pfn_TLS_client_method             dl_TLS_client_method;
static pfn_SSL_CTX_new                   dl_SSL_CTX_new;
static pfn_SSL_CTX_ctrl                  dl_SSL_CTX_ctrl;
static pfn_SSL_CTX_load_verify_locations dl_SSL_CTX_load_verify_locations;
static pfn_SSL_CTX_set_verify            dl_SSL_CTX_set_verify;
static pfn_SSL_CTX_free                  dl_SSL_CTX_free;
static pfn_SSL_new                       dl_SSL_new;
static pfn_SSL_set_fd                    dl_SSL_set_fd;
static pfn_SSL_ctrl                      dl_SSL_ctrl;
static pfn_SSL_connect                   dl_SSL_connect;
static pfn_SSL_get_verify_result         dl_SSL_get_verify_result;
static pfn_SSL_write                     dl_SSL_write;
static pfn_SSL_read                      dl_SSL_read;
static pfn_SSL_shutdown                  dl_SSL_shutdown;
static pfn_SSL_free                      dl_SSL_free;
static pfn_ERR_print_errors_fp           dl_ERR_print_errors_fp;
static pfn_EVP_cleanup                   dl_EVP_cleanup;
static pfn_ERR_free_strings              dl_ERR_free_strings;

static void *ssl_lib_handle = NULL;
static void *crypto_lib_handle = NULL;
static int ssl_loaded = 0;

#define LOAD_SYM(h, name) do { \
    dl_##name = (pfn_##name)dlsym(h, #name); \
    if (!dl_##name) { \
        fprintf(stderr, "spm: dlsym(%s): %s\n", #name, dlerror()); \
        return -1; \
    } \
} while(0)

static int load_openssl(void)
{
    if (ssl_loaded) return 0;

    /* Load libcrypto first (libssl depends on it) */
    crypto_lib_handle = dlopen("/usr/tgcware/lib/libcrypto.so", RTLD_NOW | RTLD_GLOBAL);
    if (!crypto_lib_handle)
        crypto_lib_handle = dlopen("libcrypto.so", RTLD_NOW | RTLD_GLOBAL);
    if (!crypto_lib_handle) {
        fprintf(stderr, "spm: cannot load libcrypto: %s\n", dlerror());
        return -1;
    }

    ssl_lib_handle = dlopen("/usr/tgcware/lib/libssl.so", RTLD_NOW | RTLD_GLOBAL);
    if (!ssl_lib_handle)
        ssl_lib_handle = dlopen("libssl.so", RTLD_NOW | RTLD_GLOBAL);
    if (!ssl_lib_handle) {
        fprintf(stderr, "spm: cannot load libssl: %s\n", dlerror());
        return -1;
    }

    /* Init: try OpenSSL 3 API first, fall back to 1.0 */
    dl_OPENSSL_init_ssl = (pfn_OPENSSL_init_ssl)dlsym(ssl_lib_handle, "OPENSSL_init_ssl");
    if (!dl_OPENSSL_init_ssl) {
        /* OpenSSL 1.0 path */
        dl_SSL_library_init = (pfn_SSL_library_init)dlsym(ssl_lib_handle, "SSL_library_init");
        dl_SSL_load_error_strings = (pfn_SSL_load_error_strings)dlsym(ssl_lib_handle, "SSL_load_error_strings");
        dl_OpenSSL_add_all_algorithms = (pfn_OpenSSL_add_all_algorithms)dlsym(crypto_lib_handle, "OpenSSL_add_all_algorithms");
    }

    /* Method: try TLS_client_method (3.x) then SSLv23_client_method (1.0) */
    dl_TLS_client_method = (pfn_TLS_client_method)dlsym(ssl_lib_handle, "TLS_client_method");
    if (!dl_TLS_client_method)
        dl_TLS_client_method = (pfn_TLS_client_method)dlsym(ssl_lib_handle, "SSLv23_client_method");
    if (!dl_TLS_client_method) {
        fprintf(stderr, "spm: cannot find TLS_client_method or SSLv23_client_method\n");
        return -1;
    }

    /* Core SSL functions (present in all versions) */
    LOAD_SYM(ssl_lib_handle, SSL_CTX_new);
    LOAD_SYM(ssl_lib_handle, SSL_CTX_ctrl);
    LOAD_SYM(ssl_lib_handle, SSL_CTX_load_verify_locations);
    LOAD_SYM(ssl_lib_handle, SSL_CTX_set_verify);
    LOAD_SYM(ssl_lib_handle, SSL_CTX_free);
    LOAD_SYM(ssl_lib_handle, SSL_new);
    LOAD_SYM(ssl_lib_handle, SSL_set_fd);
    LOAD_SYM(ssl_lib_handle, SSL_ctrl);
    LOAD_SYM(ssl_lib_handle, SSL_connect);
    LOAD_SYM(ssl_lib_handle, SSL_get_verify_result);
    LOAD_SYM(ssl_lib_handle, SSL_write);
    LOAD_SYM(ssl_lib_handle, SSL_read);
    LOAD_SYM(ssl_lib_handle, SSL_shutdown);
    LOAD_SYM(ssl_lib_handle, SSL_free);
    LOAD_SYM(crypto_lib_handle, ERR_print_errors_fp);

    /* Cleanup functions - optional (deprecated/removed in 3.x) */
    dl_EVP_cleanup = (pfn_EVP_cleanup)dlsym(crypto_lib_handle, "EVP_cleanup");
    dl_ERR_free_strings = (pfn_ERR_free_strings)dlsym(crypto_lib_handle, "ERR_free_strings");

    ssl_loaded = 1;
    return 0;
}

/* ================================================================ */

#define HTTP_RECV_BUF   4096
#define HTTP_MAX_RESP   (4 * 1024 * 1024)   /* 4 MB max in-memory response */
#define HTTP_MAX_REDIR  5
#define HTTP_TIMEOUT    30

/* Global SSL context */
static SSL_CTX *g_ssl_ctx = NULL;
static char g_ca_bundle[512] = "/usr/tgcware/etc/curl-ca-bundle.pem";

/* ================================================================
 * SSL INIT / SHUTDOWN
 * ================================================================ */

int http_init(void)
{
    if (load_openssl() < 0) return -1;

    /* Initialize OpenSSL */
    if (dl_OPENSSL_init_ssl) {
        /* OpenSSL 3.x */
        dl_OPENSSL_init_ssl(
            SPM_INIT_LOAD_SSL_STRINGS |
            SPM_INIT_LOAD_CRYPTO_STRINGS, NULL);
    } else {
        /* OpenSSL 1.0.x */
        if (dl_SSL_library_init) dl_SSL_library_init();
        if (dl_SSL_load_error_strings) dl_SSL_load_error_strings();
        if (dl_OpenSSL_add_all_algorithms) dl_OpenSSL_add_all_algorithms();
    }

    g_ssl_ctx = dl_SSL_CTX_new(dl_TLS_client_method());
    if (!g_ssl_ctx) {
        fprintf(stderr, "spm: SSL_CTX_new failed\n");
        dl_ERR_print_errors_fp(stderr);
        return -1;
    }

    /* Disable old protocols, allow TLS 1.0+ */
    dl_SSL_CTX_ctrl(g_ssl_ctx, SPM_SSL_CTRL_OPTIONS,
                    SPM_SSL_OP_NO_SSLv2 | SPM_SSL_OP_NO_SSLv3, NULL);

    /* Load CA certificates for verification */
    if (dl_SSL_CTX_load_verify_locations(g_ssl_ctx, g_ca_bundle, NULL) != 1) {
        /* Try alternate paths */
        if (dl_SSL_CTX_load_verify_locations(g_ssl_ctx,
                "/usr/tgcware/etc/curl-ca-bundle.pem", NULL) != 1) {
          if (dl_SSL_CTX_load_verify_locations(g_ssl_ctx,
                "/usr/tgcware/share/curl/curl-ca-bundle.crt", NULL) != 1) {
            if (dl_SSL_CTX_load_verify_locations(g_ssl_ctx,
                    "/etc/ssl/certs/ca-certificates.crt", NULL) != 1) {
                fprintf(stderr, "spm: warning: cannot load CA certs\n");
                fprintf(stderr, "  tried: %s\n", g_ca_bundle);
                fprintf(stderr, "  HTTPS verification disabled\n");
                dl_SSL_CTX_set_verify(g_ssl_ctx, SPM_SSL_VERIFY_NONE, NULL);
                return 0;
              }
            }
        }
    }

    dl_SSL_CTX_set_verify(g_ssl_ctx, SPM_SSL_VERIFY_PEER, NULL);
    return 0;
}

void http_shutdown(void)
{
    if (g_ssl_ctx) {
        dl_SSL_CTX_free(g_ssl_ctx);
        g_ssl_ctx = NULL;
    }
    if (dl_EVP_cleanup) dl_EVP_cleanup();
    if (dl_ERR_free_strings) dl_ERR_free_strings();
    if (ssl_lib_handle) { dlclose(ssl_lib_handle); ssl_lib_handle = NULL; }
    if (crypto_lib_handle) { dlclose(crypto_lib_handle); crypto_lib_handle = NULL; }
    ssl_loaded = 0;
}

void http_set_ca_bundle(const char *path)
{
    if (path) {
        strncpy(g_ca_bundle, path, sizeof(g_ca_bundle) - 1);
        g_ca_bundle[sizeof(g_ca_bundle) - 1] = '\0';
    }
}

/* ================================================================
 * URL PARSING
 * ================================================================ */

typedef struct {
    char scheme[16];
    char host[256];
    int  port;
    char path[2048];
} parsed_url_t;

static int parse_url(const char *url, parsed_url_t *u)
{
    const char *p, *host_start, *path_start, *port_start;
    int host_len;

    memset(u, 0, sizeof(*u));

    p = strstr(url, "://");
    if (!p) return -1;
    if (p - url >= (int)sizeof(u->scheme)) return -1;
    memcpy(u->scheme, url, p - url);
    u->scheme[p - url] = '\0';

    host_start = p + 3;

    if (strcmp(u->scheme, "https") == 0)
        u->port = 443;
    else
        u->port = 80;

    path_start = strchr(host_start, '/');
    if (!path_start) {
        strcpy(u->path, "/");
        host_len = strlen(host_start);
    } else {
        strncpy(u->path, path_start, sizeof(u->path) - 1);
        host_len = path_start - host_start;
    }

    port_start = NULL;
    {
        const char *bracket = strchr(host_start, ']');
        const char *colon;
        if (bracket && bracket < host_start + host_len)
            colon = strchr(bracket, ':');
        else
            colon = strchr(host_start, ':');
        if (colon && colon < host_start + host_len)
            port_start = colon;
    }

    if (port_start) {
        int hlen = port_start - host_start;
        if (hlen >= (int)sizeof(u->host)) hlen = sizeof(u->host) - 1;
        memcpy(u->host, host_start, hlen);
        u->host[hlen] = '\0';
        u->port = atoi(port_start + 1);
    } else {
        if (host_len >= (int)sizeof(u->host)) host_len = sizeof(u->host) - 1;
        memcpy(u->host, host_start, host_len);
        u->host[host_len] = '\0';
    }

    return 0;
}

static void build_url(const parsed_url_t *u, char *buf, int buflen)
{
    int dp = (strcmp(u->scheme, "https") == 0) ? 443 : 80;
    if (u->port == dp)
        snprintf(buf, buflen, "%s://%s%s", u->scheme, u->host, u->path);
    else
        snprintf(buf, buflen, "%s://%s:%d%s", u->scheme, u->host, u->port, u->path);
}

/* ================================================================
 * TCP CONNECTION
 * ================================================================ */

static int tcp_connect(const char *host, int port)
{
    struct sockaddr_in addr;
    struct hostent *he;
    int fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    {
        struct timeval tv;
        tv.tv_sec = HTTP_TIMEOUT;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv));
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);

    addr.sin_addr.s_addr = inet_addr(host);
    if (addr.sin_addr.s_addr == (in_addr_t)-1) {
        he = gethostbyname(host);
        if (!he) { close(fd); return -1; }
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd); return -1;
    }

    return fd;
}

/* ================================================================
 * CONNECTION ABSTRACTION (plain or SSL)
 * ================================================================ */

typedef struct {
    int   fd;
    SSL  *ssl;       /* NULL for plain HTTP */
    int   is_ssl;
} http_conn_t;

static http_conn_t *conn_open(const char *host, int port, int use_ssl)
{
    http_conn_t *c;
    int fd;

    fd = tcp_connect(host, port);
    if (fd < 0) return NULL;

    c = (http_conn_t *)calloc(1, sizeof(http_conn_t));
    c->fd = fd;
    c->is_ssl = use_ssl;
    c->ssl = NULL;

    if (use_ssl) {
        if (!g_ssl_ctx) {
            fprintf(stderr, "spm: SSL not initialized\n");
            close(fd); free(c);
            return NULL;
        }

        c->ssl = dl_SSL_new(g_ssl_ctx);
        if (!c->ssl) { close(fd); free(c); return NULL; }

        dl_SSL_set_fd(c->ssl, fd);

        /* SNI: set hostname for virtual hosts */
        dl_SSL_ctrl(c->ssl, SPM_SSL_CTRL_SET_TLSEXT_HOSTNAME, 0, (void *)host);

        if (dl_SSL_connect(c->ssl) != 1) {
            fprintf(stderr, "spm: SSL handshake failed to %s:%d\n",
                    host, port);
            dl_ERR_print_errors_fp(stderr);
            dl_SSL_free(c->ssl);
            close(fd); free(c);
            return NULL;
        }

        if (dl_SSL_get_verify_result(c->ssl) != SPM_X509_V_OK) {
            fprintf(stderr, "spm: warning: cert verification failed for %s\n",
                    host);
        }
    }

    return c;
}

static int conn_send(http_conn_t *c, const char *buf, int len)
{
    int sent = 0, n;
    while (sent < len) {
        if (c->is_ssl)
            n = dl_SSL_write(c->ssl, buf + sent, len - sent);
        else
            n = send(c->fd, buf + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

static int conn_recv(http_conn_t *c, char *buf, int buflen)
{
    if (c->is_ssl)
        return dl_SSL_read(c->ssl, buf, buflen);
    else
        return recv(c->fd, buf, buflen, 0);
}

static void conn_close(http_conn_t *c)
{
    if (!c) return;
    if (c->ssl) {
        dl_SSL_shutdown(c->ssl);
        dl_SSL_free(c->ssl);
    }
    if (c->fd >= 0) close(c->fd);
    free(c);
}

/* ================================================================
 * RESPONSE PARSING
 * ================================================================ */

static char *recv_all(http_conn_t *c, long *out_len)
{
    char *buf = NULL;
    long alloc = 0, total = 0;
    int n;
    char tmp[HTTP_RECV_BUF];

    while ((n = conn_recv(c, tmp, sizeof(tmp))) > 0) {
        if (total + n > HTTP_MAX_RESP) break;
        if (total + n > alloc) {
            alloc = (total + n) * 2;
            if (alloc > HTTP_MAX_RESP) alloc = HTTP_MAX_RESP;
            buf = (char *)realloc(buf, alloc + 1);
            if (!buf) return NULL;
        }
        memcpy(buf + total, tmp, n);
        total += n;
    }

    if (buf) buf[total] = '\0';
    *out_len = total;
    return buf;
}

static char *extract_header(const char *headers, long hdr_len, const char *name)
{
    const char *p = headers;
    int nlen = strlen(name);

    while (p < headers + hdr_len) {
        const char *line_end = strstr(p, "\r\n");
        if (!line_end) line_end = strstr(p, "\n");
        if (!line_end) break;

        if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            const char *val = p + nlen + 1;
            int vlen;
            while (*val == ' ') val++;
            vlen = line_end - val;
            {
                char *result = (char *)malloc(vlen + 1);
                memcpy(result, val, vlen);
                result[vlen] = '\0';
                return result;
            }
        }

        p = line_end;
        if (*p == '\r') p++;
        if (*p == '\n') p++;
    }
    return NULL;
}

static int parse_response(const char *raw, long raw_len, http_response_t *resp)
{
    const char *p, *body_start;
    long header_len;
    char *cl_str;

    memset(resp, 0, sizeof(*resp));
    resp->content_length = -1;

    p = strstr(raw, " ");
    if (!p) return -1;
    resp->status_code = atoi(p + 1);

    body_start = strstr(raw, "\r\n\r\n");
    if (body_start) {
        header_len = body_start - raw;
        body_start += 4;
    } else {
        body_start = strstr(raw, "\n\n");
        if (body_start) {
            header_len = body_start - raw;
            body_start += 2;
        } else {
            header_len = raw_len;
            body_start = raw + raw_len;
        }
    }

    resp->content_type = extract_header(raw, header_len, "Content-Type");
    resp->location = extract_header(raw, header_len, "Location");

    cl_str = extract_header(raw, header_len, "Content-Length");
    if (cl_str) {
        resp->content_length = atol(cl_str);
        free(cl_str);
    }

    resp->body_len = raw_len - (body_start - raw);
    if (resp->body_len > 0) {
        resp->body = (char *)malloc(resp->body_len + 1);
        memcpy(resp->body, body_start, resp->body_len);
        resp->body[resp->body_len] = '\0';
    } else {
        resp->body = NULL;
        resp->body_len = 0;
    }

    return 0;
}

void http_response_free(http_response_t *resp)
{
    if (resp->body) { free(resp->body); resp->body = NULL; }
    if (resp->content_type) { free(resp->content_type); resp->content_type = NULL; }
    if (resp->location) { free(resp->location); resp->location = NULL; }
    resp->body_len = 0;
    resp->status_code = 0;
    resp->content_length = -1;
}

/* ================================================================
 * HTTP GET (in-memory response)
 * ================================================================ */

static int do_http_get(const char *url, http_response_t *resp, int depth)
{
    parsed_url_t u;
    http_conn_t *c;
    int use_ssl;
    char header[4096];
    int hlen;
    char *raw;
    long raw_len;
    int rc;

    if (depth > HTTP_MAX_REDIR) return -1;
    memset(resp, 0, sizeof(*resp));
    if (parse_url(url, &u) < 0) return -1;

    use_ssl = (strcmp(u.scheme, "https") == 0);

    c = conn_open(u.host, u.port, use_ssl);
    if (!c) {
        fprintf(stderr, "spm: cannot connect to %s:%d%s\n",
                u.host, u.port, use_ssl ? " (SSL)" : "");
        return -1;
    }

    hlen = snprintf(header, sizeof(header),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: spm/%s (SunOS 5.7 SPARC)\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        u.path, u.host, SPM_VERSION);

    if (conn_send(c, header, hlen) < 0) { conn_close(c); return -1; }

    raw = recv_all(c, &raw_len);
    conn_close(c);

    if (!raw || raw_len == 0) { if (raw) free(raw); return -1; }

    rc = parse_response(raw, raw_len, resp);
    free(raw);
    if (rc < 0) return -1;

    /* Handle redirects */
    if ((resp->status_code == 301 || resp->status_code == 302 ||
         resp->status_code == 303 || resp->status_code == 307) &&
        resp->location) {
        char *new_url;
        if (resp->location[0] == '/') {
            int dp = (strcmp(u.scheme, "https") == 0) ? 443 : 80;
            new_url = (char *)malloc(strlen(u.scheme) + 3 +
                                     strlen(u.host) + 8 + strlen(resp->location) + 1);
            if (u.port == dp)
                sprintf(new_url, "%s://%s%s", u.scheme, u.host, resp->location);
            else
                sprintf(new_url, "%s://%s:%d%s", u.scheme, u.host, u.port, resp->location);
        } else {
            new_url = strdup(resp->location);
        }
        http_response_free(resp);
        rc = do_http_get(new_url, resp, depth + 1);
        free(new_url);
        return rc;
    }

    return 0;
}

int http_get(const char *url, http_response_t *resp)
{
    return do_http_get(url, resp, 0);
}

/* ================================================================
 * HTTP DOWNLOAD (streaming to file)
 * ================================================================ */

static int do_http_download(const char *url, const char *dest_path,
                            http_progress_fn progress_cb, int depth)
{
    parsed_url_t u;
    http_conn_t *c;
    int use_ssl;
    int out_fd;
    char header[4096];
    int hlen;
    char buf[HTTP_RECV_BUF];
    int n;
    long total_recv = 0;
    long content_length = -1;
    int in_headers = 1;
    char *hdr_buf = NULL;
    long hdr_alloc = 0, hdr_len = 0;
    const char *body_start;
    long body_offset;

    if (depth > HTTP_MAX_REDIR) return -1;
    if (parse_url(url, &u) < 0) return -1;

    use_ssl = (strcmp(u.scheme, "https") == 0);

    c = conn_open(u.host, u.port, use_ssl);
    if (!c) {
        fprintf(stderr, "spm: cannot connect to %s:%d\n", u.host, u.port);
        return -1;
    }

    hlen = snprintf(header, sizeof(header),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: spm/%s (SunOS 5.7 SPARC)\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        u.path, u.host, SPM_VERSION);

    if (conn_send(c, header, hlen) < 0) { conn_close(c); return -1; }

    hdr_alloc = 8192;
    hdr_buf = (char *)malloc(hdr_alloc);
    if (!hdr_buf) { conn_close(c); return -1; }

    while (in_headers) {
        n = conn_recv(c, buf, sizeof(buf));
        if (n <= 0) break;
        if (hdr_len + n >= hdr_alloc) {
            hdr_alloc = (hdr_len + n) * 2;
            hdr_buf = (char *)realloc(hdr_buf, hdr_alloc);
            if (!hdr_buf) { conn_close(c); return -1; }
        }
        memcpy(hdr_buf + hdr_len, buf, n);
        hdr_len += n;
        hdr_buf[hdr_len] = '\0';

        body_start = strstr(hdr_buf, "\r\n\r\n");
        if (body_start) { body_start += 4; in_headers = 0; }
        else {
            body_start = strstr(hdr_buf, "\n\n");
            if (body_start) { body_start += 2; in_headers = 0; }
        }
    }

    if (in_headers) { free(hdr_buf); conn_close(c); return -1; }

    /* Parse status */
    {
        const char *sp = strstr(hdr_buf, " ");
        int status;
        if (!sp) { free(hdr_buf); conn_close(c); return -1; }
        status = atoi(sp + 1);

        if (status == 301 || status == 302 || status == 303 || status == 307) {
            char *loc = extract_header(hdr_buf, body_start - hdr_buf, "Location");
            if (loc) {
                char *new_url;
                if (loc[0] == '/') {
                    int dp = (strcmp(u.scheme, "https") == 0) ? 443 : 80;
                    new_url = (char *)malloc(strlen(u.scheme) + 3 +
                                             strlen(u.host) + 8 + strlen(loc) + 1);
                    if (u.port == dp)
                        sprintf(new_url, "%s://%s%s", u.scheme, u.host, loc);
                    else
                        sprintf(new_url, "%s://%s:%d%s", u.scheme, u.host, u.port, loc);
                } else {
                    new_url = strdup(loc);
                }
                free(loc); free(hdr_buf); conn_close(c);
                n = do_http_download(new_url, dest_path, progress_cb, depth + 1);
                free(new_url);
                return n;
            }
            free(hdr_buf); conn_close(c);
            return -1;
        }

        if (status < 200 || status >= 300) {
            fprintf(stderr, "spm: HTTP %d for %s\n", status, url);
            free(hdr_buf); conn_close(c);
            return -1;
        }

        {
            char *cl = extract_header(hdr_buf, body_start - hdr_buf, "Content-Length");
            if (cl) { content_length = atol(cl); free(cl); }
        }
    }

    out_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        fprintf(stderr, "spm: cannot create %s: %s\n", dest_path, strerror(errno));
        free(hdr_buf); conn_close(c);
        return -1;
    }

    body_offset = body_start - hdr_buf;
    if (hdr_len > body_offset) {
        long initial_body = hdr_len - body_offset;
        if (write(out_fd, body_start, initial_body) != initial_body) {
            free(hdr_buf); conn_close(c); close(out_fd); unlink(dest_path);
            return -1;
        }
        total_recv = initial_body;
    }
    free(hdr_buf);

    while ((n = conn_recv(c, buf, sizeof(buf))) > 0) {
        if (write(out_fd, buf, n) != n) {
            conn_close(c); close(out_fd); unlink(dest_path);
            return -1;
        }
        total_recv += n;
        if (progress_cb) progress_cb(total_recv, content_length);
    }

    conn_close(c);
    close(out_fd);

    if (content_length > 0 && total_recv < content_length) {
        fprintf(stderr, "spm: incomplete download (%ld/%ld bytes)\n",
                total_recv, content_length);
        unlink(dest_path);
        return -1;
    }

    return 0;
}

int http_download(const char *url, const char *dest_path,
                  http_progress_fn progress_cb)
{
    return do_http_download(url, dest_path, progress_cb, 0);
}
