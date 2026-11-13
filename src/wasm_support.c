/* Minimal freestanding memory helpers, only linked by the WebAssembly build. */
#include <stddef.h>
void *memcpy(void *destination,const void *source,size_t count)
{
    unsigned char *d=destination;
    const unsigned char *s=source;
    for(size_t i=0;i<count;++i) d[i]=s[i];
    return destination;
}
void *memset(void *destination,int value,size_t count)
{
    unsigned char *d=destination;
    for(size_t i=0;i<count;++i) d[i]=(unsigned char)value;
    return destination;
}
void *memmove(void *destination,const void *source,size_t count)
{
    unsigned char *d=destination;
    const unsigned char *s=source;
    if(d<s) for(size_t i=0;i<count;++i) d[i]=s[i];
    else for(size_t i=count;i>0;--i) d[i-1]=s[i-1];
    return destination;
}
