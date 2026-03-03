/*
 * html.h - TGCware Apache autoindex HTML parser
 *
 * Parses the directory listing from jupiterrise.com/tgcware/
 * and extracts package metadata (name, version, deps, md5, etc.)
 */

#ifndef SOLPKG_HTML_H
#define SOLPKG_HTML_H

/* Package entry parsed from the TGCware HTML index */
typedef struct {
    char name[128];       /* Package name: "curl", "openssl", etc. */
    char version[64];     /* Version: "8.18.0" */
    int  release;         /* Release number: 1 */
    char arch[32];        /* "sparc", "sparcv8", "sparc64" */
    char filename[320];   /* Full filename: "curl-8.18.0-1.tgc-..." */
    char pkg_code[64];    /* SVR4 package code: "TGCcurl" */
    char description[256]; /* Human description */
    char deps[512];       /* Comma-separated dep codes: "TGClgcc1,TGCssl3" */
    char md5[64];         /* MD5 checksum */
    char size_str[16];    /* Size string: "720K", "1.9M" */
    char date[24];        /* Date: "2026-02-26" */
} tgc_entry_t;

/* Parsed TGCware package index */
typedef struct {
    tgc_entry_t *entries;
    int count;
    int alloc;
} tgc_index_t;

/*
 * Parse a TGCware Apache autoindex HTML page.
 * Returns a tgc_index_t with all parsed entries.
 * Caller must free with tgc_index_free().
 */
tgc_index_t *tgc_parse_index(const char *html);

/*
 * Free a parsed TGCware index.
 */
void tgc_index_free(tgc_index_t *idx);

/*
 * Search the TGCware index for packages matching a term.
 * Searches name, description, and pkg_code.
 * Returns number of matches found, up to max_results.
 * Writes matching indices into results[].
 */
int tgc_search(const tgc_index_t *idx, const char *term,
               int *results, int max_results);

/*
 * Find the best (latest) version of a package by name.
 * Returns index into entries[], or -1 if not found.
 */
int tgc_find_latest(const tgc_index_t *idx, const char *name);

/*
 * Find a package by its TGC package code (e.g., "TGCcurl").
 * Returns the latest version's index, or -1 if not found.
 */
int tgc_find_by_code(const tgc_index_t *idx, const char *code);

#endif /* SOLPKG_HTML_H */
