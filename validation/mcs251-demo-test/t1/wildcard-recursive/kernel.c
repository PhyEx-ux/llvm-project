/* Extracted T1 kernel: demo-84 recursive wildcard matcher shape. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

#define WILDCARD_TEXT_CAPACITY 64u

#ifdef SDCC_FW
#define MCS251_DATA __xdata
#define MCS251_PTR __xdata
#else
#define MCS251_DATA
#define MCS251_PTR
#endif
extern MCS251_DATA u8 wildcard_pattern[64];
extern MCS251_DATA u8 wildcard_name[64];

typedef struct {
    u16 skip;
    u16 recurse;
    u16 pattern_pos;
    u16 name_pos;
} match_args;

static u8 upper_ascii(u8 c)
{
    if (c >= (u8)'a' && c <= (u8)'z') return (u8)(c - (u8)0x20);
    return c;
}

static u8 wildcard_match(MCS251_PTR const match_args *args)
{
    u16 pattern_pos;
    u16 name_pos;
    u16 scan_name_pos;
    u16 skip;
    u16 branch_skip;
    u8 pchr;
    u8 nchr;
    u8 c;
    MCS251_DATA match_args branch;

    pattern_pos = args->pattern_pos;
    name_pos = args->name_pos;
    skip = args->skip;
    while ((skip & 0xffu) != 0) {
        c = upper_ascii(wildcard_name[name_pos]);
        name_pos++;
        skip--;
        if (!c) return 0;
    }
    if (wildcard_pattern[pattern_pos] == 0 && skip) return 1;

    do {
        pattern_pos = args->pattern_pos;
        scan_name_pos = name_pos;
        for (;;) {
            c = wildcard_pattern[pattern_pos];
            if (c == '?' || c == '*') {
                branch_skip = 0;
                if (args->recurse == 0) return 0;
                do {
                    c = wildcard_pattern[pattern_pos];
                    pattern_pos++;
                    if (c == '?') branch_skip++;
                    else branch_skip |= 0x100u;
                } while (wildcard_pattern[pattern_pos] == '?' ||
                         wildcard_pattern[pattern_pos] == '*');
                branch.skip = branch_skip;
                branch.recurse = (u16)(args->recurse - 1u);
                branch.pattern_pos = pattern_pos;
                branch.name_pos = scan_name_pos;
                if (wildcard_match(&branch)) return 1;
                nchr = wildcard_name[scan_name_pos];
                break;
            }
            pchr = upper_ascii(wildcard_pattern[pattern_pos]);
            pattern_pos++;
            nchr = upper_ascii(wildcard_name[scan_name_pos]);
            scan_name_pos++;
            if (pchr != nchr) break;
            if (pchr == 0) return 1;
        }
        name_pos++;
    } while (skip && nchr);

    return 0;
}

/* Native pattern_match(pat, nam, skip, recur) is packed into one context
 * pointer; recursion is preserved and the return value is observable. */
u8 wildcard_match_packed(MCS251_PTR const match_args *args)
{
    return wildcard_match(args);
}
