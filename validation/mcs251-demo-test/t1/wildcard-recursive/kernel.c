/* Extracted T1 kernel: demo-84 recursive wildcard matcher shape. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif

typedef struct {
    const u8 *pattern;
    const u8 *name;
    u16 skip;
    u16 recurse;
} match_args;

static u8 wildcard_match(const match_args *args)
{
    const u8 *p = args->pattern;
    const u8 *n = args->name;
    u16 skip = args->skip;
    while ((skip & 0xffu) != 0) {
        if (*n == 0) return 0;
        n++;
        skip--;
    }
    while (*p != 0) {
        if (*p == '?' || *p == '*') {
            match_args branch;
            u16 branch_skip = 0;
            if (args->recurse == 0) return 0;
            do {
                if (*p++ == '?') branch_skip++;
                else branch_skip |= 0x100u;
            } while (*p == '?' || *p == '*');
            branch.pattern = p;
            branch.name = n;
            branch.skip = branch_skip;
            branch.recurse = (u16)(args->recurse - 1u);
            if (wildcard_match(&branch)) return 1;
            if (*n == 0) return 0;
            n++;
        } else {
            if (*p++ != *n++) return 0;
        }
    }
    if (*p == 0 && skip != 0) return 1;
    return *n == 0;
}

/* Native pattern_match(pat, nam, skip, recur) is packed into one context
 * pointer; recursion is preserved and the return value is observable. */
u8 wildcard_match_packed(const match_args *args)
{
    return wildcard_match(args);
}
