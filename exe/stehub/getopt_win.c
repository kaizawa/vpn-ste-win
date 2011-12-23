/*
 * Copyright (C) 2004-2010 Kazuyoshi Aizawa. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/****************************************************************
 * getopt_win.c
 *
 * Solaris との互換のために作った擬似 getopt() 関数。
 * getopt() を完全に実装しているわけではないので注意。
 *
 * sted.exe stehub.exe では期待通りに動く。
 *
 * **************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <Windows.h>

char *optarg;

int getopt(int argc, char * const argv[], const  char  *optstring)
{
    static int    count = 1;
    static char  *optstring_saved = NULL;
    int optstrings;
    int i;
    char *opt;

    // optstring が変わったら別の option の調査とみなす。
    // TODO: 実際の getopt() の実装方法を調べてもっとまともな方法を使う。
    if(optstring_saved != NULL && strcmp(optstring_saved, optstring) != 0)
        count = 1;
    optstring_saved = (char *)optstring;

    if(count > argc - 1)
        return(EOF);

    //printf("argc = %d, count = %d\n",argc,  count);

    optstrings = strlen(optstring);

    opt = argv[count];

    //printf("opt[0] = %c, opt[1] = %c optstrings = %d\n", opt[0], opt[1], optstrings);
    
    if(opt[0] == '-'){
        for ( i = 0; i < optstrings ; i++){
            /* 渡されたオプションが optstring に含まれているかチェック */
            if(opt[1] == optstring[i]){
                /* optstring に次があるかチェック。なければオプションに値は無い */
                if( i + 1 <= optstrings){
                    /* オプションに値(:)が必要とされているかどうかのチェック */
                    if(optstring[i+1] == ':'){
                        /* オプション値が次の argv として渡されているかどうかを確認 */
                        if( count + 1 <= argc - 1){
                            /* 次の argv が '-' から始まっていないかどうかを確認 */
                            if( argv[count+1][0] == '-'){
                                count++;
                                //printf("return 1\n");                                    
                                return(':');
                            } else {
                                optarg = argv[count+1];
                                count = count + 2;
                                //printf("return 2\n");                                    
                                return(opt[1]);
                            }
                        } else {
                            
                            count++;
                            //printf("return 3\n");                                
                            return(':');
                        }
                    } else {
                        count++;
                        //printf("return 4\n");                            
                        return(opt[1]);                        
                    }
                } else {
                    count++;
                    //printf("return 5\n");                                                
                    return(opt[1]);
                }
            }
            continue;
        } /* for end */
    } 
    count++;
    //printf("return 6\n");    
    return(':');
}
