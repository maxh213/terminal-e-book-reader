#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* Safe allocation - exit on failure */
void *xmalloc(size_t size);
void *xcalloc(size_t nmemb, size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* Path utilities */
char *path_join(const char *dir, const char *file);
char *path_dirname(const char *path);
const char *path_basename(const char *path);

/* String utilities */
char *str_trim(char *s);
int str_endswith(const char *s, const char *suffix);
int str_startswith(const char *s, const char *prefix);
char *str_tolower(const char *s);

/* URL/href utilities */
char *href_strip_fragment(const char *href);
const char *href_get_fragment(const char *href);

/* XDG paths - caller frees returned strings */
char *xdg_config_home(void);
char *xdg_data_home(void);

/* FNV-1a hash */
unsigned int fnv1a_hash(const char *str);

#endif
