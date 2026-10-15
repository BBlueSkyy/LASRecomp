#include "android_prepare.h"
#include "../romfs_build.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool android_prepare_game(const char *root, char *why, size_t n) {
    static const unsigned char build_id[] = {0xae,0x16,0xf7,0x1e,0,0x2a,0xf8,0xcb};
    char path[1200],romfs[1200],output[1200],stage[1200];
    for(int i=0;i<2;i++) {
        if(snprintf(path,sizeof path,"%s/exefs/%s",root,i?"sdk":"main") >= (int)sizeof path) {
            snprintf(why,n,"Game path is too long"); return false;
        }
        FILE *f=fopen(path,"rb"); unsigned char h[0x100];
        size_t got=f?fread(h,1,sizeof h,f):0; if(f) fclose(f);
        if(got!=sizeof h || memcmp(h,"NSO0",4)) {
            snprintf(why,n,"Missing or truncated extracted NSO: %s",i?"sdk":"main"); return false;
        }
        if(!i && memcmp(h+0x40,build_id,sizeof build_id)) {
            snprintf(why,n,"The original static recompiler requires Link's Awakening v1.0.0"); return false;
        }
    }
    if(snprintf(romfs,sizeof romfs,"%s/romfs",root)>=(int)sizeof romfs ||
       snprintf(output,sizeof output,"%s/romfs.bin",root)>=(int)sizeof output ||
       snprintf(stage,sizeof stage,"%s/romfs.bin.part",root)>=(int)sizeof stage) {
        snprintf(why,n,"Game path is too long"); return false;
    }
    struct stat st;
    snprintf(path,sizeof path,"%s/romfs/region_common",root);
    if(stat(path,&st) || !S_ISDIR(st.st_mode)) {
        snprintf(why,n,"Missing romfs/region_common game data"); return false;
    }
    if(!stat(output,&st) && S_ISREG(st.st_mode) && st.st_size>=0x50) return true;
    fprintf(stderr,"[android] Building the original runtime's raw RomFS image\n");
    // Incomplete writes never replace a valid image. This is data preparation,
    // not compilation or a synthetic guest frame.
    if(!romfs_build(romfs,stage,NULL,why,n)) { unlink(stage); return false; }
    if(rename(stage,output)) {
        snprintf(why,n,"Cannot publish RomFS image: %s",strerror(errno)); unlink(stage); return false;
    }
    return true;
}
