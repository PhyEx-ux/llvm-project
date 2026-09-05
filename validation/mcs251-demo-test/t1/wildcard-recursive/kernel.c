/* Extracted T1 kernel: demo-84 recursive wildcard matcher shape. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned long u32;
#endif

typedef struct {
    const u8 *pattern;
    const u8 *name;
    u16 skip;
    u16 recurse;
} match_args;

static u8 upper_ascii(u8 c)
{
    if (c >= (u8)'a' && c <= (u8)'z') return (u8)(c - (u8)0x20);
    return c;
}

static u8 get_achar(const u8 **ptr)
{
    u8 c = *(*ptr)++;
    return upper_ascii(c);
}

static u8 wildcard_match(const match_args *args)
{
    const u8 *pat = args->pattern;
    const u8 *nam = args->name;
    u16 skip = args->skip;
    u8 nchr = 0;

    while ((skip & 0xffu) != 0) {
        if (!get_achar(&nam)) return 0;
        skip--;
    }
    if (*pat == 0 && skip) return 1;

    do {
        const u8 *pptr = pat;
        const u8 *nptr = nam;
        u8 pchr;
        for (;;) {
            if (*pptr == '?' || *pptr == '*') {
                match_args branch;
                u16 branch_skip = 0;
                if (args->recurse == 0) return 0;
                do {
                    if (*pptr++ == '?') branch_skip++;
                    else branch_skip |= 0x100u;
                } while (*pptr == '?' || *pptr == '*');
                branch.pattern = pptr;
                branch.name = nptr;
                branch.skip = branch_skip;
                branch.recurse = (u16)(args->recurse - 1u);
                if (wildcard_match(&branch)) return 1;
                nchr = *nptr;
                break;
            }
            pchr = get_achar(&pptr);
            nchr = get_achar(&nptr);
            if (pchr != nchr) break;
            if (pchr == 0) return 1;
        }
        (void)get_achar(&nam);
    } while (skip && nchr);

    return 0;
}

/* Native pattern_match(pat, nam, skip, recur) is packed into one context
 * pointer; recursion is preserved and the return value is observable. */
u8 wildcard_match_packed(const match_args *args)
{
    return wildcard_match(args);
}
