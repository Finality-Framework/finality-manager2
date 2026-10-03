#include <stdlib.h>
#include <string.h>
char *concat(char* dest,char* src){
    char *result = malloc(strlen(dest)+strlen(src));
    strcpy(result, dest);
    strcat(result, src);
    return result;
}
char *truncate_lines(const char* text, int max_lines) {
    if (text == NULL || max_lines <= 0) {
        return NULL;
    }
    const char* p = text;
    int total_lines = 0;
    while (*p) {
        if (*p == '\n') total_lines++;
        p++;
    }
    if (p > text && *(p - 1) != '\n') {
        total_lines++;
    }
    if (total_lines <= max_lines) {
        char* result = malloc(strlen(text) + 1);
        if (result) strcpy(result, text);
        return result;
    }
    p = text;
    int lines_found = 0;
    const char* end_pos = text;
    while (*p) {
        if (*p == '\n') {
            lines_found++;
            if (lines_found == max_lines) {
                end_pos = p + 1;
                break;
            }
        }
        p++;
    }
    size_t len = end_pos - text;
    char* result = malloc(len + 1);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, text, len);
    result[len] = '\0';
    return result;
}
