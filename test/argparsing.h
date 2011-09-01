#ifndef TEST_ARGPARSING_H
#define TEST_ARGPARSING_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#ifndef SST
# define CHECK_VERBOSE() verbose = (getenv("VERBOSE") != NULL)
#else
# define CHECK_VERBOSE()
#endif

#define NUMARG(var,name) do { \
    char *str; \
    if ((str = getenv(name)) != NULL) { \
	char *stre = NULL; \
	size_t tmp = strtoul(str, &stre, 0); \
	if (stre == NULL || stre == str) { \
	    fprintf(stderr, "unparsable "name" (%s)\n", str); \
	} else { \
	    var = tmp; \
	} \
    } \
    iprintf(name" = %lu\n", (unsigned long)var); \
} while (0)

// Given an environment variable "FOO=,,,80,,"
// And buffer "size_t foo[5]"
// Then NUMARRARG(foo, bar, 5, 100) 
//   => foo = {100,100,100,80,100}
#define NUMARRARG(var,name,size,val) do { \
    char *str = getenv(name); \
    int i = 0; \
    if (str != NULL && *str != '\0') { \
        char *rest = NULL; \
        do { \
            var[i] = strtoul(str, &rest, 0); \
            if (rest == str) var[i] = val; \
            if (*rest == ',') rest++; \
            else if (*rest != '\0') { \
                fprintf(stderr, "malformed "name" ending (%s)\n", rest); \
                abort(); \
                } \
            i++; \
            str = rest; \
        } while (*rest != '\0' && i < size); \
        if (*rest != '\0') \
            fprintf(stderr, "extra values in "name" (%s)\n", rest); \
    } \
    while (i < size) { \
        var[i] = val; \
        i++; \
    } \
    iprintf(name" = [%lu", (unsigned long)var[0]); \
    for (int i = 1; i < size; i++) \
        iprintf(", %lu", (unsigned long)var[i]); \
    iprintf("]\n"); \
} while (0)

#ifdef SST
static int verbose = 1;
#else
static int verbose = 0;
#endif

#if defined(__tile__) || defined(__CYGWIN32__)
# define iprintf printf
#else
static void iprintf(const char * restrict format, ...)
{
    if (verbose != 0) {
	va_list ap;

	va_start(ap, format);
	vprintf(format, ap);
	fflush(stdout);
	va_end(ap);
    }
}
#endif

#endif
