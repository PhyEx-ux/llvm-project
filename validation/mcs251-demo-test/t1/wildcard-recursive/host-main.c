#include <stdio.h>
static void ck8(char t,unsigned char v){putchar(t);printf("%02X",(unsigned)v);}

#include <string.h>
#include "kernel.c"
u8 wildcard_pattern[64],wildcard_name[64];
static void run(const char*p,const char*n,u16 s,u16 r,char t){match_args a;memset(wildcard_pattern,0,64);memset(wildcard_name,0,64);strncpy((char*)wildcard_pattern,p,63);strncpy((char*)wildcard_name,n,63);a.skip=s;a.recurse=r;a.pattern_pos=0;a.name_pos=0;ck8(t,wildcard_match_packed(&a));}
int main(void){putchar('B');run("FOO","foo",0,8,'a');run("F?O","fao",0,8,'b');run("A*D","abcd",0,8,'c');run("A*D","abce",0,8,'d');run("*TXT","myfile.txt",0,12,'e');run("ABC","XABC",1,8,'f');run("ABC","ABC",0,0,'g');puts("PASS");return 0;}
