#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p) { fprintf(stderr, "tread: out of memory\n"); exit(1); }
    return p;
}

void *xcalloc(size_t nmemb, size_t size) {
    void *p = calloc(nmemb, size);
    if (!p) { fprintf(stderr, "tread: out of memory\n"); exit(1); }
    return p;
}

void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p && size) { fprintf(stderr, "tread: out of memory\n"); exit(1); }
    return p;
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    char *d = strdup(s);
    if (!d) { fprintf(stderr, "tread: out of memory\n"); exit(1); }
    return d;
}

char *xstrndup(const char *s, size_t n) {
    if (!s) return NULL;
    char *d = strndup(s, n);
    if (!d) { fprintf(stderr, "tread: out of memory\n"); exit(1); }
    return d;
}

char *path_join(const char *dir, const char *file) {
    if (!dir || !dir[0]) return xstrdup(file);
    if (!file || !file[0]) return xstrdup(dir);
    size_t dlen = strlen(dir);
    int need_sep = (dir[dlen - 1] != '/');
    size_t total = dlen + need_sep + strlen(file) + 1;
    char *out = xmalloc(total);
    snprintf(out, total, "%s%s%s", dir, need_sep ? "/" : "", file);
    return out;
}

char *path_dirname(const char *path) {
    if (!path) return xstrdup(".");
    const char *last = strrchr(path, '/');
    if (!last) return xstrdup(".");
    if (last == path) return xstrdup("/");
    return xstrndup(path, (size_t)(last - path));
}

const char *path_basename(const char *path) {
    if (!path) return "";
    const char *last = strrchr(path, '/');
    return last ? last + 1 : path;
}

char *str_trim(char *s) {
    if (!s) return s;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

int str_endswith(const char *s, const char *suffix) {
    size_t slen = strlen(s), sufflen = strlen(suffix);
    if (sufflen > slen) return 0;
    return strcmp(s + slen - sufflen, suffix) == 0;
}

int str_startswith(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

char *str_tolower(const char *s) {
    char *out = xstrdup(s);
    for (char *p = out; *p; p++) *p = (char)tolower((unsigned char)*p);
    return out;
}

char *href_strip_fragment(const char *href) {
    if (!href) return NULL;
    const char *hash = strchr(href, '#');
    if (!hash) return xstrdup(href);
    return xstrndup(href, (size_t)(hash - href));
}

const char *href_get_fragment(const char *href) {
    if (!href) return NULL;
    const char *hash = strchr(href, '#');
    return hash ? hash + 1 : NULL;
}

char *xdg_config_home(void) {
    const char *env = getenv("XDG_CONFIG_HOME");
    if (env && env[0]) return xstrdup(env);
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    return path_join(home, ".config");
}

char *xdg_data_home(void) {
    const char *env = getenv("XDG_DATA_HOME");
    if (env && env[0]) return xstrdup(env);
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    return path_join(home, ".local/share");
}

unsigned int fnv1a_hash(const char *str) {
    unsigned int hash = 2166136261u;
    for (; *str; str++) {
        hash ^= (unsigned char)*str;
        hash *= 16777619u;
    }
    return hash;
}
