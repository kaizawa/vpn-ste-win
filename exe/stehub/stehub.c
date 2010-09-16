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
/**********************************************************
 * 2005/03/27
 *
 * stehub.c
 *
 * 仮想ハブ。仮想 NIC デーモンからの Ethernet フレームを受け取り、
 * 他の仮想 NIC デーモンへ転送する役割を持つユーザプロセス。
 *
 *  gcc stehub.c -o stehub -lsocket -lnsl
 *
 * Usage: stehub [ -p port] [-d level]
 *
 *     引数:
 *        -p port  仮想 NIC デーモンからの接続を待ち受けるポート番号を指定する。
 *                 指定されなければ、デフォルトで 80 が使われる。
 *
 * 変更履歴 :
 *    o recv() の バッファサイズを 500byte から 32K bytes に変更。
 *    o listen() するポート番号を起動時に指定できるようにした。
 *  2004/12/15
 *    o 誤った conn_stat 構造体を fee() してしまう問題を修正
 *  2004/12/18
 *   o debug オプションを追加した。
 *   o debug レベルが 0 (デフォルト）の場合は、バックグラウンドで
 *     実行されるようにした。
 *   o debug レベルが 0 (デフォルト）の場合は、各種エラーメッセージ
 *     を syslog に出すようにした。
 *  2004/12/29
 *   o include ファイルを追加
 *  2004/12/30
 *   o Linux、でもコンパイルできるよう include ファイルを追加
 *   o SFU 3.5 をインストールした Windows 上でもコンパイル、実行ができるように
 *     多少変更を行った。
 *  2005/01/08
 *   o send() のエラーを確認するようにし、EWOULDBLOCK、EINTR の場合は無視するようにした。
 *  2005/01/09
 *   o send() のエラー処理が間違っていたので修正。
 *  2005/03/27
 *   o Windows 上でも利用可能なように修正した（まだ未使用）
 *   o recv() のエラー処理が間違っていたので修正した。
 * 
 ***********************************************************/

#ifdef STE_WINDOWS
#include <winsock2.h>   /* for windows */
#include <Windows.h>    /* for windows */
#include <time.h>       /* for windows */
#include <winioctl.h>   /* for windows */
#include <setupapi.h>   /* for windows */
#include <dbt.h>        /* for windows */
#include <direct.h>
#include "getopt_win.h"
#else 
#define  WINAPIV        /* for solaris */
#include <strings.h>    /* for solaris */
#include <unistd.h>     /* for solaris */
#include <sys/socket.h> /* for solaris */
#include <netinet/in.h> /* for solaris */
#include <netdb.h>      /* for solaris */
#include <syslog.h>     /* for solaris */
#include <libgen.h>     /* for solaris */
#include <arpa/inet.h>  /* for solaris */
#include <sys/time.h>   /* for solaris */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h> 
#include <sys/stat.h>
#include <fcntl.h>
#include "sted.h"

#define PORT_NO        80     /* 接続を待ち受けるデフォルトのポート番号 */
#define SOCKBUFSIZE    32768  /* recv(), send() 用のバッファのサイズ  */

#ifdef  FD_SETSIZE
#undef  FD_SETSIZE
#endif
#define FD_SETSIZE     1024


#ifdef  STE_WINDOWS
HANDLE  hStedLog;          /* デバッグログ用のファイルハンドル */
#define STEHUB_LOG_FILE   "C:\\stehub.log" /* ログファイル */
#endif

struct conn_stat {
    struct conn_stat *next;
    int fd;
    struct in_addr addr;
};

void  add_conn_stat(int, struct in_addr);
void  delete_conn_stat(int);
struct conn_stat *find_conn_stat(int);
int   become_daemon();
void  print_err(int, char *, ...);
void  print_usage(char *);
extern char *basename(char *); /* for Interix */

struct conn_stat   conn_stat_head[1];
int           use_log = 0;      /* メッセージを STDERR でなく、syslog に出力する */
static int    debuglevel = 0;   /* デバッグレベル。 1 以上ならフォアグラウンドで実行 */
extern char  *optarg;
extern int    optind;
extern int    optopt;
extern int    opterr;
extern int    optreset;

int WINAPIV
main(int argc,char *argv[])
{
    int                 listener_fd, new_fd;
    int                 remotelen;
    int                 port = 0;
    int                 c, on;
    struct sockaddr_in  local_sin, remote_sin;
    static              fd_set  fdset, fdset_saved;
    struct conn_stat   *rconn, *wconn;
#ifdef STE_WINDOWS
    u_long              param = 0; /* FIONBIO コマンドのパラメータ Non-Blocking ON*/
    int                 nRtn;
    WSADATA             wsaData;
    nRtn = WSAStartup(MAKEWORD(1, 1), &wsaData);        
#endif

    while ((c = getopt(argc, argv, "p:d:")) != EOF){
        switch (c) {
            case 'p':
                port = atoi(optarg);
                break;                
            case 'd':
                debuglevel = atoi(optarg);
                break;
            default:
                print_usage(argv[0]);
        }
    }    

    conn_stat_head->next = NULL;
    conn_stat_head->fd = 0;

    if(( listener_fd = socket( AF_INET, SOCK_STREAM,0 )) < 0 ) {
        SET_ERRNO();
        print_err(LOG_ERR,"socket: %s (%d)\n", strerror(errno), errno);
        exit(1);
    }

    on = 1;
    if((setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on))) <0){
        SET_ERRNO();        
        print_err(LOG_ERR,"setsockopt:%s\n", strerror(errno));                
        exit(1);
    }

    if(port == 0)
        port = PORT_NO;
    memset((char *)&remote_sin, 0x0, sizeof(struct sockaddr_in));
    memset((char *)&local_sin, 0x0, sizeof(struct sockaddr_in));
    local_sin.sin_port   = htons((short)port);
    local_sin.sin_family = AF_INET;
    local_sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(listener_fd,(struct sockaddr *)&local_sin,sizeof(struct sockaddr_in)) < 0 ){
        SET_ERRNO();
        print_err(LOG_ERR,"bind:%s\n", strerror(errno));                        
        exit(1);
    }


    /*
     * accept() でブロックされるのを防ぐため、non-blocking mode に設定
     */
#ifndef STE_WINDOWS            
    if( fcntl (listener_fd, F_SETFL, O_NONBLOCK) < 0) {
#else
    if( ioctlsocket(listener_fd, FIONBIO, &param) < 0){
#endif                
        SET_ERRNO();
        print_err(LOG_ERR, "Failed to set nonblock: %s (%d)\n",strerror(errno), errno);
        exit(1);
    }

    if(listen(listener_fd, 5) < 0) {
        SET_ERRNO();
        print_err(LOG_ERR,"listen:%s\n", strerror(errno));                                
        exit(1);
    }

    FD_ZERO(&fdset_saved);
    FD_SET(listener_fd, &fdset_saved);

    /*
     * syslog のための設定。Facility は　LOG_USER とする
     * Windows の場合はログファイルをオープンする。
     */
#ifdef STE_WINDOWS
    hStedLog = CreateFile(STEHUB_LOG_FILE,
                          GENERIC_READ|GENERIC_WRITE,
                          FILE_SHARE_READ| FILE_SHARE_WRITE,
                          NULL,
                          CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    SetFilePointer(hStedLog, 0, NULL, FILE_END);
#else
     openlog(basename(argv[0]),LOG_PID,LOG_USER);
#endif
     
    /*
     * ここまではとりあえず、フォアグラウンドで実行。
     * ここからは、デバッグレベル 0 （デフォルト）なら、バックグラウンド
     * で実行し、そうでなければフォアグラウンド続行。
     * Windows の場合は、いづれにしてもフォアグラウンドで実行
     * し、デバッグレベル 1 以上の場合はログをファイルに書く。
     */
    if (debuglevel == 0){
#ifndef STE_WINDOWS             
        print_err(LOG_NOTICE,"Going to background mode\n");
        if(become_daemon() != 0){
            print_err(LOG_ERR,"can't become daemon\n");
            print_err(LOG_ERR,"Exit\n");            
            exit(1);
        }
#else 
        use_log = 1;
#endif    
    }
    print_err(LOG_NOTICE,"Started\n");        

    /*
     * メインループ
     * 仮想 NIC デーモンからの接続要求を待ち、接続後は仮想 NIC デーモン
     * からのデータを待つ。１つの仮想デーモンからのデータを他方に転送する。
     */
    for(;;){
        fdset = fdset_saved;
        if( select(FD_SETSIZE, &fdset, NULL, NULL, NULL) < 0){
            SET_ERRNO();
            print_err(LOG_ERR,"select:%s\n", strerror(errno));
        }

        if(FD_ISSET(listener_fd, &fdset)){
            remotelen = sizeof(struct sockaddr_in);
            if((new_fd = accept(listener_fd,(struct sockaddr *)&remote_sin, &remotelen)) < 0){
                SET_ERRNO();
                if(errno == EINTR || errno == EWOULDBLOCK || errno == ECONNABORTED){
                    print_err(LOG_NOTICE, "accept: %s\n", strerror(errno));
                    continue;
                } else {
                    print_err(LOG_ERR, "accept: %s\n", strerror(errno));                    
                    return(-1);
                }
            }
            
            FD_SET(new_fd, &fdset_saved);
            print_err(LOG_NOTICE,"fd%d: connection from %s\n",new_fd, inet_ntoa(remote_sin.sin_addr));
            add_conn_stat(new_fd, remote_sin.sin_addr);
            /*
             * recv() でブロックされるのを防ぐため、non-blocking mode に設定
             */
#ifndef STE_WINDOWS            
            if (fcntl(new_fd, F_SETFL, O_NONBLOCK) < 0 ) {
#else
                if(ioctlsocket(new_fd, FIONBIO, &param) < 0){
#endif                
                    SET_ERRNO();
                    print_err(LOG_ERR, "fd%d: Failed to set nonblock: %s (%d)\n",
                              new_fd,strerror(errno),errno);
                    return(-1);
                }
                continue;
            }

            for( rconn = conn_stat_head->next ; rconn != NULL ; rconn = rconn->next){
                int rfd, wfd;

                rfd = rconn->fd;
            
                if (FD_ISSET(rfd, &fdset)){
                    int   rsize;
                    char  databuf[SOCKBUFSIZE];
                    char *bufp;
                    int   datalen;

                    bufp = (char *)databuf;
                    rsize = recv(rfd, bufp, SOCKBUFSIZE,0);
                    if(rsize == 0){
                        /*
                         * コネクションが切断されたようだ。
                         * socket を close してループを抜ける
                         */
                        print_err(LOG_ERR,"fd%d: Connection closed by %s\n", rfd, inet_ntoa(rconn->addr));
                        CLOSE(rfd);
                        print_err(LOG_ERR,"fd%d: closed\n", rfd);
                        FD_CLR(rfd, &fdset_saved);
                        delete_conn_stat(rfd);
                        break;
                    }
                    if(rsize < 0){
                        SET_ERRNO();                    
                        /*
                         * 致命的でない error の場合は無視してループを継続
                         */
                        if(errno == EINTR || errno == EWOULDBLOCK){
                            print_err(LOG_NOTICE, "fd%d: recv: %s\n", rfd, strerror(errno));
                            continue;
                        }
                        /*
                         * エラーが発生したようだ。
                         * socket を close して forループを抜ける
                         */
                        print_err(LOG_ERR,"fd%d: recv: %s\n", rfd,strerror(errno));
                        CLOSE(rfd);
                        print_err(LOG_ERR,"fd%d: closed\n", rfd);
                        FD_CLR(rfd, &fdset_saved);
                        delete_conn_stat(rfd);
                        break;
                    }
                    /*
                     * 他の仮想 NIC にパケットを転送する。
                     * 「待ち」が発生すると、パフォーマンスに影響があるので、EWOULDBLOCK
                     *  の場合は配送をあきらめる。
                     */
                    for(wconn = conn_stat_head->next ; wconn != NULL ; wconn = wconn->next){
                    
                        wfd = wconn->fd;

                        if (rfd == wfd)
                            continue;

                        if( debuglevel > 1){
                            print_err(LOG_ERR,"fd%d(%s) ==> ", rfd, inet_ntoa(rconn->addr));
                            print_err(LOG_ERR,"fd%d(%s)\n", wfd,inet_ntoa(wconn->addr));
                        }
                
                        if ( send(wfd, bufp, rsize, 0) < 0){
                            SET_ERRNO();                    
                            if(errno == EINTR || errno == EWOULDBLOCK ){
                                print_err(LOG_NOTICE,"fd%d: send: %s\n", wfd ,strerror(errno));
                                continue;
                            } else {
                                print_err(LOG_ERR,"fd%d: send: %s (%d)\n",wfd,strerror(errno), errno);
                                CLOSE(wfd);
                                print_err(LOG_ERR,"fd%d: closed\n", wfd);
                                FD_CLR(wfd, &fdset_saved);
                                delete_conn_stat(wfd);
                                break;                            
                            }
                        }                    
                    } /* End of loop for send()ing */
                }
            } /* End of loop for each connection */
        } /* End of main loop */
}

/*****************************************************************************
 * add_conn_stat()
 *
 * conn_stat 構造体のリンクリストに新規 conn_stat を追加する。
 *
 *  引数：
 *          fd: 新規コネクションの socket 番号
 *          addr: 接続してきたホストのアドレス
 * 戻り値：
 *          無し
 *****************************************************************************/
void
add_conn_stat(int fd, struct in_addr addr)
{
    struct conn_stat *conn, *conn_stat_new;

    int i = 0;
    
    for( conn = conn_stat_head ; conn->next != NULL ; conn = conn->next);
    
    conn_stat_new = (struct conn_stat *)malloc(sizeof(struct conn_stat));
    conn_stat_new->fd = fd;
    conn_stat_new->addr = addr;
    conn_stat_new->next = NULL;

    conn->next = conn_stat_new;
}

/*****************************************************************************
 * delete_conn_stat()
 *
 * conn_stat 構造体のリンクリストから指定された conn_stat を削除する
 *
 *  引数：
 *          fd: 削除する conn_stat 構造体に含まれる socket 番号
 *  戻り値：
 *          無し
 *****************************************************************************/
void
delete_conn_stat(int fd)
{
    struct conn_stat *conn, *conn_stat_delete;
    int i = 0;

    conn = conn_stat_head;

    while( conn != NULL){
        if(conn->next->fd == fd){
            conn_stat_delete = conn->next;
            conn->next = conn_stat_delete->next;
            free(conn_stat_delete);
            return;
        }
        conn = conn->next;
    }
}
/*****************************************************************************
 * find_conn_stat()
 *
 * fd によって指定された conn_stat 構造体のを探し、return する。
 *
 *  引数：
 *          fd: 検索する conn_stat 構造体に含まれる socket 番号
 *  戻り値：
 *          conn_stat 構造体のポインタ
 *****************************************************************************/
struct conn_stat *
find_conn_stat(int fd)
{
    struct conn_stat *conn;
    int i = 0;

    conn = conn_stat_head;

    while( conn != NULL){
        if(conn->next->fd == fd){
            return(conn->next);
        }
        conn = conn->next;
    }
    return((struct conn_stat *)NULL);
}

#ifndef STE_WINDOWS
/*****************************************************************************
 * become_daemon()
 * 
 * 標準入出力、標準エラー出力をクローズし、バックグラウンドに移行する。
 *****************************************************************************/
int
become_daemon()
{
    chdir("/");
    umask(0);
    signal(SIGHUP,SIG_IGN);

    if( fork() == 0){
        use_log = 1;
        close (0);
        close (1);
        close (2);
        /* 新セッションの開始 */
        if (setsid() < 0)
            return(-1);
    } else {
        exit(0);
    }
    return(0);
}
#endif

/***********************************************************
 * print_err()
 *
 * エラーメッセージを表示するルーチン。
 * 
 ***********************************************************/
void
print_err(int level, char *format, ...)
{
    va_list ap;
    char buf[ERR_MSG_MAX];
    int length;    
    
    va_start(ap, format);
    vsprintf(buf, format, ap);
    va_end(ap);

    if(use_log){
#ifdef STE_WINDOWS
        WriteFile(hStedLog, buf, strlen(buf), &length, NULL);
#else    
        syslog(level, buf);
#endif            
    } else {
        fprintf(stderr, buf);
    }
}
/*****************************************************************************
 * print_usage()
 * 
 * Usage を表示し、終了する。
 *****************************************************************************/
void
print_usage(char *argv)
{
    printf ("Usage: %s [ -p port] [-d level]\n",argv);    
    printf ("\t-p port   : Port nubmer\n");
    printf ("\t-d level  : Debug level[0-2]\n");
    exit(0);
}
