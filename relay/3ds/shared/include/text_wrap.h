#ifndef THREEDSRP_TEXT_WRAP_H
#define THREEDSRP_TEXT_WRAP_H
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Return one bounded console row, preserving explicit newlines. Only tokens
 * wider than an entire row may be split. Caller supplies width+1 bytes. */
static inline bool text_wrap_line(const char **cursor,char *line,size_t width)
{
    const char *start=*cursor;
    if(!*start || !width) return false;
    while(*start==' ') start++;
    size_t n=0,space=0;
    while(start[n] && start[n]!='\n' && n<width) {
        if(start[n]==' ') space=n;
        n++;
    }
    size_t take=n;
    if(n==width && start[n] && start[n]!='\n' && start[n]!=' ' && space)
        take=space;
    memcpy(line,start,take); line[take]=0;
    const char *next=start+take;
    while(*next==' ') next++;
    if(*next=='\n') next++;
    *cursor=next;
    return true;
}
#endif
