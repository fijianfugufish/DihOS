#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "utils/errors.h"

bool html_redraw_printing = false;
int html_redraw_printing_border = 0;
int html_redraw_printing_top_cropped = 0;

nserror save_pdf(const char *path)
{
    (void)path;
    return NSERROR_NOT_IMPLEMENTED;
}

int inet_aton(const char *text, struct in_addr *address)
{
    uint32_t value = 0;
    for (int part = 0; part < 4; ++part) {
        char *end;
        unsigned long octet = strtoul(text, &end, 10);
        if (end == text || octet > 255 || (part < 3 && *end != '.'))
            return 0;
        value = (value << 8) | (uint32_t)octet;
        text = part < 3 ? end + 1 : end;
    }
    if (*text != 0)
        return 0;
    address->s_addr = value;
    return 1;
}

int inet_pton(int family, const char *text, void *address)
{
    if (family == AF_INET)
        return inet_aton(text, (struct in_addr *)address);
    return 0;
}
