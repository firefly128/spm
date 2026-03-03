/*
 * html.c - TGCware Apache autoindex HTML parser
 *
 * Parses the HTML directory listing from jupiterrise.com and extracts
 * structured package metadata. The format is:
 *
 *  <a href="FILENAME">FILENAME</a>  DATE  SIZE  DESC - PKG_CODE - DEPS - MD5
 *
 * Where FILENAME is: name-version-release.tgc-sunos5.7-arch-tgcware.gz
 * And DESC fields are separated by " - "
 */

#include "html.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Case-insensitive strstr */
static const char *strcasestr_local(const char *haystack, const char *needle)
{
    int nlen = strlen(needle);
    while (*haystack) {
        if (strncasecmp(haystack, needle, nlen) == 0)
            return haystack;
        haystack++;
    }
    return NULL;
}

/*
 * Parse the filename to extract name, version, release, and arch.
 *
 * Format: name-version-release.tgc-sunos5.7-arch-tgcware.gz
 * Examples:
 *   curl-8.18.0-1.tgc-sunos5.7-sparc-tgcware.gz
 *   openssl-3.0.19-1.tgc-sunos5.7-sparcv8-tgcware.gz
 *   gcc-4.5.4-3.tgc-sunos5.7-sparc64-tgcware64.gz
 */
static int parse_filename(const char *fn, tgc_entry_t *e)
{
    const char *tgc_marker;
    const char *p, *last_dash;
    const char *ver_start, *rel_start;
    int name_len;
    const char *arch_start, *arch_end;

    /* Find the ".tgc-" marker */
    tgc_marker = strstr(fn, ".tgc-");
    if (!tgc_marker) return -1;

    /* Work backwards from .tgc- to find release number (after last '-' before .tgc) */
    rel_start = tgc_marker;
    while (rel_start > fn && *(rel_start - 1) != '-') rel_start--;
    if (rel_start <= fn) return -1;

    e->release = atoi(rel_start);

    /* Work backwards from release to find version start */
    ver_start = rel_start - 2; /* skip the '-' before release */
    while (ver_start > fn && *(ver_start - 1) != '-') ver_start--;
    if (ver_start <= fn) {
        /* Single-component name like "m4-1.4.17-1.tgc..." */
        /* Actually let's re-approach: version starts at the first '-' followed by a digit */
        p = fn;
        ver_start = NULL;
        while (*p) {
            if (*p == '-' && (*(p+1) >= '0' && *(p+1) <= '9')) {
                ver_start = p + 1;
                break;
            }
            p++;
        }
        if (!ver_start) return -1;
    }

    /* Package name is everything before the version's dash */
    name_len = (ver_start - 1) - fn;
    if (name_len <= 0 || name_len >= (int)sizeof(e->name)) return -1;
    memcpy(e->name, fn, name_len);
    e->name[name_len] = '\0';

    /* Version is between name-dash and release-dash */
    {
        int vlen = (rel_start - 1) - ver_start;
        if (vlen <= 0 || vlen >= (int)sizeof(e->version)) return -1;
        memcpy(e->version, ver_start, vlen);
        e->version[vlen] = '\0';
    }

    /* Architecture: after "sunos5.7-" and before "-tgcware" */
    arch_start = strstr(tgc_marker, "sunos5.7-");
    if (!arch_start) return -1;
    arch_start += 9; /* skip "sunos5.7-" */

    arch_end = strstr(arch_start, "-tgcware");
    if (!arch_end) return -1;
    {
        int alen = arch_end - arch_start;
        if (alen <= 0 || alen >= (int)sizeof(e->arch)) return -1;
        memcpy(e->arch, arch_start, alen);
        e->arch[alen] = '\0';
    }

    /* Copy full filename */
    strncpy(e->filename, fn, sizeof(e->filename) - 1);
    e->filename[sizeof(e->filename) - 1] = '\0';

    return 0;
}

/*
 * Parse the description field after the size column.
 * Format: "shortname - description - PKG_CODE - DEPS_OR_NODEPS - MD5"
 * (5 segments separated by " - ")
 * Where DEPS is comma-separated TGC codes or "no dependencies"
 */
static void parse_description(const char *desc, tgc_entry_t *e)
{
    const char *seps[5];
    const char *segments[5];
    int seg_lens[5];
    int nseg = 0;
    const char *p;
    int len;

    e->description[0] = '\0';
    e->pkg_code[0] = '\0';
    e->deps[0] = '\0';
    e->md5[0] = '\0';

    if (!desc || !*desc) return;

    /* Skip leading whitespace */
    while (*desc == ' ' || *desc == '\t') desc++;

    /* Split on " - " separators, up to 5 segments */
    segments[0] = desc;
    nseg = 1;
    p = desc;
    while (nseg < 5) {
        const char *sep = strstr(p, " - ");
        if (!sep) break;
        seg_lens[nseg - 1] = sep - segments[nseg - 1];
        segments[nseg] = sep + 3;
        nseg++;
        p = sep + 3;
    }
    /* Last segment length: to end of string, trimmed */
    len = strlen(segments[nseg - 1]);
    while (len > 0 && (segments[nseg-1][len-1] == '\n' ||
                       segments[nseg-1][len-1] == '\r' ||
                       segments[nseg-1][len-1] == ' ' ||
                       segments[nseg-1][len-1] == '\t'))
        len--;
    seg_lens[nseg - 1] = len;

    if (nseg == 5) {
        /* 5 segments: shortname - description - pkg_code - deps - md5 */
        /* Segment 0 = shortname (skip, we already have name from filename) */
        /* Segment 1 = description */
        len = seg_lens[1];
        if (len >= (int)sizeof(e->description)) len = sizeof(e->description) - 1;
        memcpy(e->description, segments[1], len);
        e->description[len] = '\0';

        /* Segment 2 = pkg_code */
        len = seg_lens[2];
        if (len >= (int)sizeof(e->pkg_code)) len = sizeof(e->pkg_code) - 1;
        memcpy(e->pkg_code, segments[2], len);
        e->pkg_code[len] = '\0';

        /* Segment 3 = deps */
        len = seg_lens[3];
        if (len >= (int)sizeof(e->deps)) len = sizeof(e->deps) - 1;
        memcpy(e->deps, segments[3], len);
        e->deps[len] = '\0';
        if (strcasestr_local(e->deps, "no dependencies"))
            e->deps[0] = '\0';

        /* Segment 4 = md5 */
        len = seg_lens[4];
        if (len >= (int)sizeof(e->md5)) len = sizeof(e->md5) - 1;
        memcpy(e->md5, segments[4], len);
        e->md5[len] = '\0';
    } else if (nseg == 4) {
        /* 4 segments: try old format desc - pkg_code - deps - md5 */
        len = seg_lens[0];
        if (len >= (int)sizeof(e->description)) len = sizeof(e->description) - 1;
        memcpy(e->description, segments[0], len);
        e->description[len] = '\0';

        len = seg_lens[1];
        if (len >= (int)sizeof(e->pkg_code)) len = sizeof(e->pkg_code) - 1;
        memcpy(e->pkg_code, segments[1], len);
        e->pkg_code[len] = '\0';

        len = seg_lens[2];
        if (len >= (int)sizeof(e->deps)) len = sizeof(e->deps) - 1;
        memcpy(e->deps, segments[2], len);
        e->deps[len] = '\0';
        if (strcasestr_local(e->deps, "no dependencies"))
            e->deps[0] = '\0';

        len = seg_lens[3];
        if (len >= (int)sizeof(e->md5)) len = sizeof(e->md5) - 1;
        memcpy(e->md5, segments[3], len);
        e->md5[len] = '\0';
    } else if (nseg >= 2) {
        /* 2-3 segments: just description and whatever else */
        len = seg_lens[0];
        if (len >= (int)sizeof(e->description)) len = sizeof(e->description) - 1;
        memcpy(e->description, segments[0], len);
        e->description[len] = '\0';

        len = seg_lens[1];
        if (len >= (int)sizeof(e->pkg_code)) len = sizeof(e->pkg_code) - 1;
        memcpy(e->pkg_code, segments[1], len);
        e->pkg_code[len] = '\0';

        if (nseg >= 3) {
            len = seg_lens[2];
            if (len >= (int)sizeof(e->deps)) len = sizeof(e->deps) - 1;
            memcpy(e->deps, segments[2], len);
            e->deps[len] = '\0';
        }
    } else {
        /* Single segment: just description */
        len = seg_lens[0];
        if (len >= (int)sizeof(e->description)) len = sizeof(e->description) - 1;
        memcpy(e->description, segments[0], len);
        e->description[len] = '\0';
    }
}

/*
 * Add an entry to the index, growing the array as needed.
 */
static void index_add(tgc_index_t *idx, const tgc_entry_t *e)
{
    if (idx->count >= idx->alloc) {
        idx->alloc = idx->alloc ? idx->alloc * 2 : 64;
        idx->entries = (tgc_entry_t *)realloc(idx->entries,
                                               idx->alloc * sizeof(tgc_entry_t));
    }
    idx->entries[idx->count++] = *e;
}

tgc_index_t *tgc_parse_index(const char *html)
{
    tgc_index_t *idx;
    const char *p, *href_start, *href_end;
    const char *line_end;

    idx = (tgc_index_t *)calloc(1, sizeof(tgc_index_t));
    if (!idx) return NULL;

    p = html;
    while (*p) {
        tgc_entry_t entry;
        const char *fn;
        int fn_len;
        char fn_buf[320];
        const char *after_link;
        const char *date_p, *size_p, *desc_p;

        /* Find next <a href="...tgcware..."> */
        href_start = strstr(p, "<a href=\"");
        if (!href_start) href_start = strstr(p, "<A HREF=\"");
        if (!href_start) break;
        href_start += 9;

        href_end = strchr(href_start, '"');
        if (!href_end) break;

        fn = href_start;
        fn_len = href_end - href_start;

        /* Skip non-package links (parent dir, column headers, etc.) */
        if (fn_len <= 0 || fn_len >= (int)sizeof(fn_buf) ||
            !strstr(href_start, "tgcware")) {
            p = href_end + 1;
            continue;
        }

        memcpy(fn_buf, fn, fn_len);
        fn_buf[fn_len] = '\0';

        /* Skip if it ends with '/' (directory) */
        if (fn_buf[fn_len - 1] == '/') {
            p = href_end + 1;
            continue;
        }

        memset(&entry, 0, sizeof(entry));

        /* Parse the filename */
        if (parse_filename(fn_buf, &entry) < 0) {
            p = href_end + 1;
            continue;
        }

        /* Find the </a> closing tag */
        after_link = strstr(href_end, "</a>");
        if (!after_link) after_link = strstr(href_end, "</A>");
        if (!after_link) {
            p = href_end + 1;
            continue;
        }
        after_link += 4; /* skip "</a>" */

        /* Find end of this line */
        line_end = strchr(after_link, '\n');
        if (!line_end) line_end = after_link + strlen(after_link);

        /* After </a> there's whitespace, then DATE, SIZE, DESCRIPTION */
        /* Skip whitespace to date */
        date_p = after_link;
        while (*date_p == ' ' || *date_p == '\t') date_p++;

        /* Date is YYYY-MM-DD HH:MM */
        if (date_p + 10 < line_end && date_p[4] == '-' && date_p[7] == '-') {
            memcpy(entry.date, date_p, 10);
            entry.date[10] = '\0';

            /* Skip past date+time to size */
            size_p = date_p + 10;
            while (*size_p == ' ' || *size_p == ':' ||
                   (*size_p >= '0' && *size_p <= '9')) size_p++;

            /* Skip whitespace to size field */
            while (*size_p == ' ' || *size_p == '\t') size_p++;

            /* Size is like "720K", "1.9M", "14M" */
            {
                const char *se = size_p;
                while (*se && *se != ' ' && *se != '\t' && se < line_end) se++;
                {
                    int slen = se - size_p;
                    if (slen > 0 && slen < (int)sizeof(entry.size_str)) {
                        memcpy(entry.size_str, size_p, slen);
                        entry.size_str[slen] = '\0';
                    }
                }

                /* Rest is description */
                desc_p = se;
                while (*desc_p == ' ' || *desc_p == '\t') desc_p++;

                /* Extract description up to line end */
                {
                    char desc_buf[1024];
                    int dlen = line_end - desc_p;
                    if (dlen >= (int)sizeof(desc_buf)) dlen = sizeof(desc_buf) - 1;
                    if (dlen > 0) {
                        memcpy(desc_buf, desc_p, dlen);
                        desc_buf[dlen] = '\0';
                        /* Strip HTML tags from description */
                        {
                            char *r = desc_buf, *w = desc_buf;
                            int in_tag = 0;
                            while (*r) {
                                if (*r == '<') { in_tag = 1; r++; continue; }
                                if (*r == '>') { in_tag = 0; r++; continue; }
                                if (!in_tag) *w++ = *r;
                                r++;
                            }
                            *w = '\0';
                        }
                        parse_description(desc_buf, &entry);
                    }
                }
            }
        }

        index_add(idx, &entry);
        p = line_end;
    }

    return idx;
}

void tgc_index_free(tgc_index_t *idx)
{
    if (!idx) return;
    if (idx->entries) free(idx->entries);
    free(idx);
}

/* Case-insensitive substring match */
static int match_term(const char *haystack, const char *needle)
{
    return strcasestr_local(haystack, needle) != NULL;
}

int tgc_search(const tgc_index_t *idx, const char *term,
               int *results, int max_results)
{
    int i, count = 0;
    if (!idx || !term) return 0;

    for (i = 0; i < idx->count && count < max_results; i++) {
        if (match_term(idx->entries[i].name, term) ||
            match_term(idx->entries[i].description, term) ||
            match_term(idx->entries[i].pkg_code, term)) {
            results[count++] = i;
        }
    }
    return count;
}

/* Simple version comparison: returns >0 if a > b, <0 if a < b, 0 if equal.
 * Compares numerically where possible. */
static int version_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            long na = strtol(a, (char **)&a, 10);
            long nb = strtol(b, (char **)&b, 10);
            if (na != nb) return (na > nb) ? 1 : -1;
        } else {
            if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
            a++;
            b++;
        }
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

int tgc_find_latest(const tgc_index_t *idx, const char *name)
{
    int i, best = -1;
    for (i = 0; i < idx->count; i++) {
        if (strcasecmp(idx->entries[i].name, name) == 0) {
            if (best < 0) {
                best = i;
            } else {
                int vc = version_cmp(idx->entries[i].version,
                                      idx->entries[best].version);
                if (vc > 0 || (vc == 0 && idx->entries[i].release >
                               idx->entries[best].release)) {
                    best = i;
                }
            }
        }
    }
    return best;
}

int tgc_find_by_code(const tgc_index_t *idx, const char *code)
{
    int i, best = -1;
    for (i = 0; i < idx->count; i++) {
        if (strcmp(idx->entries[i].pkg_code, code) == 0) {
            if (best < 0) {
                best = i;
            } else {
                int vc = version_cmp(idx->entries[i].version,
                                      idx->entries[best].version);
                if (vc > 0 || (vc == 0 && idx->entries[i].release >
                               idx->entries[best].release)) {
                    best = i;
                }
            }
        }
    }
    return best;
}
