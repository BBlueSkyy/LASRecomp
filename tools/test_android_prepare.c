#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../runtime/platform/android_prepare.h"
static void nso(const char *path, int main_module) {
    unsigned char h[0x101] = {0};
    memcpy(h,"NSO0",4);
    const unsigned char id[] = {0xae,0x16,0xf7,0x1e,0,0x2a,0xf8,0xcb};
    if(main_module) memcpy(h+0x40,id,8);
    FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(h,1,sizeof h,f)==sizeof h); fclose(f);
}
int main(void) {
    char tmp[]="/tmp/las-prepare-XXXXXX",p[1024],why[512]; assert(mkdtemp(tmp));
    assert(!android_prepare_game(tmp,why,sizeof why));
    snprintf(p,sizeof p,"%s/exefs",tmp); assert(!mkdir(p,0700));
    snprintf(p,sizeof p,"%s/romfs",tmp); assert(!mkdir(p,0700));
    snprintf(p,sizeof p,"%s/exefs/main",tmp); nso(p,0);
    snprintf(p,sizeof p,"%s/exefs/sdk",tmp); nso(p,0);
    assert(!android_prepare_game(tmp,why,sizeof why)); assert(strstr(why,"v1.0.0"));
    snprintf(p,sizeof p,"%s/exefs/main",tmp); nso(p,1);
    snprintf(p,sizeof p,"%s/romfs/region_common",tmp); assert(!mkdir(p,0700));
    snprintf(p,sizeof p,"%s/romfs/region_common/data",tmp);
    FILE *f=fopen(p,"wb"); assert(f); fputs("private fixture",f); fclose(f);
    assert(android_prepare_game(tmp,why,sizeof why));
    snprintf(p,sizeof p,"%s/romfs.bin",tmp); struct stat st; assert(!stat(p,&st)); assert(st.st_size>0x50);
    f=fopen(p,"rb"); unsigned long long h=0; assert(fread(&h,1,8,f)==8); fclose(f); assert(h==0x50);
    assert(android_prepare_game(tmp,why,sizeof why));
    snprintf(p,sizeof p,"%s/romfs/region_common/data",tmp); unlink(p);
    snprintf(p,sizeof p,"%s/romfs/region_common",tmp); rmdir(p);
    snprintf(p,sizeof p,"%s/romfs",tmp); rmdir(p);
    snprintf(p,sizeof p,"%s/exefs/main",tmp); unlink(p);
    snprintf(p,sizeof p,"%s/exefs/sdk",tmp); unlink(p);
    snprintf(p,sizeof p,"%s/exefs",tmp); rmdir(p);
    snprintf(p,sizeof p,"%s/romfs.bin",tmp); unlink(p); rmdir(tmp);
    puts("Android game preparation tests passed");
}
