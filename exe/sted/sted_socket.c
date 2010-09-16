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
/****************************************************************************
 * 2005/02/01
 * 
 * sted_socket.o
 *
 * 仮想 NIC のユーザプロセスのデーモンが使う socket 用ルーチン。
 * 
 *    gcc -c sted_socket.c
 *
 * 変更履歴：
 *   2005/02/01
 *     o sted.c から socket のルーチンを抜き出した。
 *   2005/03/27
 *     o Windows 上でもコンパイル・実行できるように変更した。
 *   2005/05/14
 *     o EAGAIN を EWOULDBLOCK に変更した。
 *     o Windows の為に sted_win.h に EWOULDBLOCK を define するようにした。
 *    
 *****************************************************************************/

#ifdef STE_WINDOWS
#include <windows.h>    /* for windows */
#include <WinSock2.h>   /* for windows */
#include <setupapi.h>   /* for windows */
#include <winioctl.h>   /* for windows */
#include <dbt.h>        /* for windows */
#else
#include <sys/socket.h>   
#include <netdb.h>        
#include <syslog.h>       
#include <sys/ethernet.h> 
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "ste.h"
#include "sted.h"

#ifdef STE_WINDOWS
extern WSAEVENT   EventArray[2]; // socket と ste ドライバ用の 2 つの Event の配列
#endif

extern int debuglevel;
extern int write_ste(stedstat_t *);

/*****************************************************************************
 * open_socket()
 * 
 * HUB(stehub) と TCP connection を確立し、Socket を返す。
 * Proxy サーバが指定されていれば、そちらと TCP connection を確立する。
 *
 *  引数：
 *           stedstat: sted 管理用構造体
 *           hub     : HUB のホスト名（と「:」でくぎられたポート番号）
 *           proxy   : Proxy のホスト名（と「:」でくぎられたポート番号）
 * 戻り値：
 *         成功時 :  ソケット番号
 *         失敗時 :  -1
 *****************************************************************************/
int
open_socket(stedstat_t *stedstat, char *hub, char *proxy)
{
    static	struct  sockaddr_in sin;
    static	struct  hostent	   *hp;
    char *hub_name, *proxy_name;
    char *hub_port_string, *proxy_port_string;
    int hub_port, proxy_port;
    int   sock;
    char *temp;
    int nRtn;    

#ifdef STE_WINDOWS    
    WSADATA wsaData;
    nRtn = WSAStartup(MAKEWORD(1, 1), &wsaData);        
#endif

    memset((char *) &sin,0,sizeof(sin));
    if((hub_name = strtok(hub, ":")) == NULL){
        print_err(LOG_ERR, "hub name was not given\n");
        return(-1);
    }
    if((hub_port_string = strtok(NULL, ":")) == NULL){
        hub_port = PORT_NO;
    } else {
        hub_port = atoi(hub_port_string);
    }
    
    /*
     * stedstat 構造体に hub のホスト名、ポート番号を記録
     */
    strncpy(stedstat->hub_name, hub_name, MAXHOSTNAME);
    stedstat->hub_port = hub_port;

    if( proxy == NULL){
        /*
         * proxy が指定されていないので、直接 仮想ハブホストに接続詞に行くため
         * に仮想ハブホストの hostent 得る。
         */
        if(( hp = gethostbyname(hub_name)) == NULL) {
            print_err(LOG_ERR,"hostname %s not found.\n",hub_name);
            return(-1);
        }
        /*
         * sockaddr_in の sin_port に 仮想ハブホストのポート番号をセット
         */        
        sin.sin_port	= htons((short)hub_port);
        /*
         * stedstat 構造体の proxy には NULL をセット
         */

        memset(stedstat->proxy_name, 0x0, MAXHOSTNAME);
        
        stedstat->proxy_port = 0;
    } else {
        /*
         * proxy が指定されているので、proxy に接続しにいく必要がある。
         * proxy サーバの hostent 得る。
         */
        if((proxy_name = strtok(proxy, ":")) == NULL){
            print_err(LOG_ERR,"proxy name was not given\n");
            return(-1);
        }
        if((proxy_port_string = strtok(NULL, ":")) == NULL){
            proxy_port = PORT_NO;
        } else {
            proxy_port = atoi(proxy_port_string);
        }
        if(( hp = gethostbyname(proxy_name)) == NULL) {
            print_err(LOG_ERR,"hostname %s not found.\n",proxy_name);
            return(-1);
        }
        /*
         * sockaddr_in の sin_port に proxy サーバのポート番号をセット
         */
        sin.sin_port	= htons((short)proxy_port);
        /*
         * stedstat 構造体に proxy サーバのホスト名、ポート番号を記録
         */
        strncpy(stedstat->proxy_name, proxy_name, MAXHOSTNAME);
        stedstat->proxy_port = proxy_port;
    }
    
    memcpy((char *)&sin.sin_addr,hp->h_addr,hp->h_length);
    sin.sin_family = AF_INET;

    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        SET_ERRNO();
        print_err(LOG_ERR, "socket:%s\n", strerror(errno));
        return(-1);
    }

    if(connect(sock,(struct sockaddr *)&sin, sizeof sin) < 0) {
        SET_ERRNO();
        print_err(LOG_ERR, "connect: %s\n", strerror(errno));        
        return(-1);
    }

    /*
     * recv() でブロックされるのを防ぐため、non-blocking mode に設定
     */      
#ifdef STE_WINDOWS
    if( WSAEventSelect(sock , EventArray[0] , FD_READ ) == SOCKET_ERROR ){
        SET_ERRNO();
        print_err(LOG_ERR,"WSAEventSelect failed: %s\n", strerror(errno));
        return(-1);
    }        
#else    
    if( fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
        SET_ERRNO();
        print_err(LOG_ERR, "Failed to set nonblock: %s\n",sock, strerror(errno));
        return(-1);
    }
#endif

    /*
     * stedstat 構造体に FD を記録
     */
    stedstat->sock_fd = sock;

    /*
     * HUB 経由の場合CONNECT リクエストを作成。
     */
    if(proxy != NULL){
        int stat;
        if((stat = send_connect_req(stedstat)) != 0){
            if ( stat > 0)
                print_err(LOG_ERR, "proxy server %s returned \"%d - %s\"\n",
                          proxy_name, stat, stat2string(stat));
            print_err(LOG_ERR, "CONNECT request to %s failed.\n",proxy_name);
            return(-1);
        }
    }
    print_err(LOG_NOTICE, "Successfully connected with HUB\n");
    
    return(sock);
}

/*****************************************************************************
 * read_socket()
 * 
 * HUB(stehub) からのデータを読み込み、 ste ドライバに転送する。
 *
 *  引数：
 *           stedstat   : sted 管理用構造体
 *           
 * 戻り値：
 *          正常時 : recv() で読み込んだサイズ(0) もある。
 *          障害時 :  -1
 *****************************************************************************/
int
read_socket(stedstat_t *stedstat)
{
    stehead_t   *steh;
    int          flags = 0;
    int          recvsize;  // recv() で実際に読み込んだサイズ        
    int          cnt;       // recvsize の中で、未処理のデータサイズ
    int          sock_fd = stedstat->sock_fd;
    u_char      *recvbuf = stedstat->recvbuf;
    u_char      *readp;
    u_char      *wdatabuf = stedstat->wdatabuf;

    if(debuglevel > 1){    
        print_err(LOG_DEBUG, "read_socket called\n");
    }
    
    if( (recvsize = recv(sock_fd, recvbuf, SOCKBUFSIZE,0)) < 0)  {
        SET_ERRNO();
        if(errno == EINTR || errno == EWOULDBLOCK || errno == 0){
            if(debuglevel > 1){
                print_err(LOG_NOTICE, "read_socket: recv %s\n", strerror(errno));
                print_err(LOG_DEBUG, "read_socket returned\n");
            }
            return(0);
        }        
        print_err(LOG_ERR,"read_socket: recv %s (%d)\n", strerror(errno),errno);
            if(debuglevel > 1){        
                print_err(LOG_DEBUG, "read_socket returned\n");
            }            
        return(-1);
    }
    if( recvsize == 0){
        print_err(LOG_ERR, "connection with hub is being closed\n");
        if(debuglevel > 1){                        
            print_err(LOG_DEBUG, "read_socket returned\n");
        }        
        return(-1);
    }
    
    if(debuglevel > 1){
        print_err(LOG_DEBUG, "========= from hub %d bytes ===================\n", recvsize);
        print_err(LOG_DEBUG, "datalen(Frame size)                   = %d\n", stedstat->datalen);        
        print_err(LOG_DEBUG, "dataleft(needed to complete Frame)    = %d\n",stedstat->dataleft);
        if (debuglevel > 2){
            int i;
            for (i = 0; i < recvsize; i++){
                if((i)%16 == 0){
                    print_err(LOG_DEBUG, "\n%04d: ", i);                    
                }
                print_err(LOG_DEBUG, "%02x ", recvbuf[i] & 0xff);                
            }
            print_err(LOG_DEBUG, "\n\n");
        }
    }

    /* 未処理データサイズをセット */
    cnt = recvsize;
    /* 処理用のポインタをセット */
    readp = recvbuf;
            
    while(1){ 
        if (stedstat->dataleft == 0){
            /*
             * 受信データの先頭部分。先頭部分は stehead なので、
             * 読み込んで、フレームのサイズを得る。
             */
            u_char *newp;
            if((newp = read_socket_header(stedstat, &cnt, readp)) == NULL){
                /* stehead はまだ不完全。次のデータの到着を待つ */
                break;
            }
            readp = newp;
        }
        /*
         * 読み取った stehead から、オリジナルの Ethernet フレームのサイズを
         * 確認し、 0 より大きく、ETHERMAX(1514bytes)以下であることを確かめる。
         */
        if( stedstat->orgdatalen <= 0 || stedstat->orgdatalen > ETHERMAX ){
            /*
             * stehead は壊れていると思われる。以降の受信データは無視し、ループを抜ける。
             * （仮想ハブが受信パケットをこちらに転送せずに破棄した可能性が高い）
             * この方法だと、未読のデータをすべて破棄するしかなく、また次の受信データの
             * 先頭が stehead であるという保証も無いため、一度ここに入り込むと、しばらく
             * ここに来続ける可能性がある・・ヘッダーはデータの境界情報を持つ重要なもの
             * なので、もっと確実・安全に取り出せる仕組みを検討する必要がある。
             */
            stedstat->dataleft = stedstat->datalen = stedstat->dummyheadlen = 0;
            print_err(LOG_NOTICE, "read_socket: header is broken\n");
            break;
        }
        if (debuglevel > 1) {
            print_err(LOG_DEBUG, "---------------------\n");                            
            print_err(LOG_DEBUG, "cnt(data size not yet processed)      = %d\n", cnt);
            print_err(LOG_DEBUG, "datalen(Frame size)                   = %d\n", stedstat->datalen);
            print_err(LOG_DEBUG, "dataleft(needed to complete Frame)    = %d\n", stedstat->dataleft);
        }
        if(cnt < stedstat->dataleft){
            /* この受信データだけでは元のフレームを再構成できない */
            /* とりあえずコピーしておいて、次のデータの到着を待つ */
            /* 書き込み位置は、書き込み済みの位置の次 */
            memcpy(wdatabuf + (stedstat->datalen - stedstat->dataleft) , readp, cnt);
            stedstat->dataleft = stedstat->dataleft - cnt;
            if (debuglevel > 1) {
                print_err(LOG_DEBUG, "Need more %d bytes to complete a framed.\n", stedstat->dataleft);
            }            
            break;
        }
        if (cnt == stedstat->dataleft){
            /* この受信データで元のフレームを再構成できる */
            /* 丁度１フレーム分しか含んでいない */
            if (debuglevel > 1){
                print_err(LOG_DEBUG, "Frame completed. And no unread data left.\n");
            }
            memcpy(wdatabuf + (stedstat->datalen - stedstat->dataleft), readp, cnt);

            write_ste(stedstat);

            if (debuglevel > 1){
                print_err(LOG_DEBUG, "wrote %d bytes to driver completed.\n",stedstat->orgdatalen);
            }            
            stedstat->dataleft = stedstat->datalen = stedstat->dummyheadlen = 0;
            break;
        }
        if (cnt > stedstat->dataleft){
            /* この受信データだけで元のフレームを再構成できる */
            /* さらに別のフレームのデータも含まれている */
            memcpy(wdatabuf  + (stedstat->datalen - stedstat->dataleft), readp, stedstat->dataleft);
            write_ste(stedstat);            
            
            readp = readp + stedstat->dataleft;
            cnt = cnt - stedstat->dataleft;
            stedstat->dataleft = stedstat->datalen = stedstat->dummyheadlen = 0;
            if (debuglevel > 1){
                print_err(LOG_DEBUG, "Frame completed. Still %d bytes of unread data.\n", cnt);
                print_err(LOG_DEBUG, "wrote %d bytes to driver completed.\n",stedstat->orgdatalen);
            }
            /* while ループを続ける */
            continue;
        }
    } /* while loop end */

    if(debuglevel > 1){                    
        print_err(LOG_DEBUG, "read_socket returned\n");
    }
    return(recvsize);
}

/*****************************************************************************
 * read_socket_header()
 * 
 * ヘッダー（stehead 構造体）を読み取り、dummyhead にコピーする。
 * 受け取ったポインタにヘッダーの一部しかない場合は、受信部分だけを dummyhead
 * にコピーしておき、次のパケットの到着を待つ。
 *
 *  引数：
 *           stedstat : sted 管理用構造体
 *           cnt      : readp に含まれるデータのサイズ
 *           readp    : 受信データ（stehead 構造体を含むと思われる）
 * 戻り値：
 *          stehead 完成時  : stehead をとばしたデータの読み込み開始位置
 *          stehead 未完成時: NULL
 *****************************************************************************/
u_char *
read_socket_header(stedstat_t *stedstat, int *cnt, u_char *readp)
{
    int unreadlen; /* まだ recv されていない header の長さ */
    
    unreadlen = sizeof(stehead_t) - stedstat->dummyheadlen;
    
    if( *cnt >= unreadlen){
        /* stehead の足りないところは無い */
        memcpy((char *)&stedstat->dummyhead + stedstat->dummyheadlen, readp, unreadlen);
        readp = readp + unreadlen;
        *cnt = *cnt - unreadlen;
        stedstat->dummyheadlen = sizeof(stehead_t);
        stedstat->datalen = stedstat->dataleft = ntohl(stedstat->dummyhead.len);
        stedstat->orgdatalen = ntohl(stedstat->dummyhead.orglen);
        if (debuglevel > 1) {
            print_err(LOG_DEBUG, "---------------------\n");            
            print_err(LOG_DEBUG, "read_socket_header: ste header is completed\n");
            print_err(LOG_DEBUG, "Data size = %d , Without PAD = %d\n",
                      stedstat->datalen, stedstat->orgdatalen);
        }
        return(readp);
    } else {
        /*
         * ヘッダー部を取得後でないとその後の
         * 処理がはじめられないので、stehead の
         * サイズ分が到着するまで recv を繰り返す
         */
        memcpy((char *)&stedstat->dummyhead + stedstat->dummyheadlen, readp, *cnt);
        stedstat->dummyheadlen = stedstat->dummyheadlen + *cnt;
        if (debuglevel > 1) {
            print_err(LOG_DEBUG, "---------------------\n");            
            print_err(LOG_DEBUG, "read_socket_header: Insuficient header.\n");
        }
        return((u_char *)NULL);
    }
}

/*****************************************************************************
 * write_socket()
 * 
 * stedstat 構造体の sendbuf に溜まっているデータを HUB(stehub) へ転送する。
 *
 *  引数：
 *           stedstat   : sted 管理用構造体
 *           
 * 戻り値：
 *          正常時 : 0
 *          障害時 : -1
 *****************************************************************************/
int
write_socket(stedstat_t *stedstat)
{
    if (debuglevel > 1) {        
        print_err(LOG_DEBUG,"write_socket called\n");
    }
    
    if( stedstat->sendbuflen == 0){
        if (debuglevel > 1) {
            print_err(LOG_ERR,"sendbuflen == 0\n");
            print_err(LOG_ERR,"write_socket returned\n");
        }
        return(0);
    }
    
    if ( send(stedstat->sock_fd, stedstat->sendbuf, stedstat->sendbuflen, 0) < 0){
        SET_ERRNO();
        if(errno == EINTR || errno == EWOULDBLOCK || errno == 0){
            if (debuglevel > 1) {            
                print_err(LOG_NOTICE, "write_socket: send: %s\n", strerror(errno));
            }                        
        } else {
            print_err(LOG_ERR,"write_socket: send %s (%d)\n", strerror(errno), errno);
            if (debuglevel > 1) {
                print_err(LOG_DEBUG,"write_socket returned\n");
            }            
            return(-1);
        }
    }
    stedstat->sendbuflen = 0;  /* 書き込み済みサイズを 0 に戻す */
    if (debuglevel > 1) {                        
        print_err(LOG_DEBUG,"write_socket returned\n");
    }    
    return(0);
}

/*****************************************************************************
 * send_connect_req()
 * 
 * Proxy サーバに CONNECT リクエストを投げ、レスポンスを待つ。
 * もし、レスポンスのなかに「200」(=OK)が含まれていなかったら
 * Proxy サーバは CONNECT メソッドをサポートしてない（もしくは
 * アクセスコントロールとか・）と思われる。あきらめて -1 を返す。
 *
 *  引数：
 *           stedstat : sted 管理用構造体
 * 戻り値：
 *          正常時 : Proxy サーバから返ってきたステータスコード
 *          障害時 : -1
 *****************************************************************************/
int
send_connect_req(stedstat_t *stedstat)
{
    static fd_set fds;
    struct timeval timeout;
    int ret, cnt;
    u_char buf[SOCKBUFSIZE];
    char *http_ver;       /* レスポンスに含まれる HTTP バージョン */
    char *http_stat_char; /* レスポンスに含まれる ステータスコード */
    int  http_stat;       /* レスポンスに含まれる ステータスコード */
    char connect_req[CONNECT_REQ_SIZE];
    char *hub_name = stedstat->hub_name;
    int hub_port = stedstat->hub_port;
    int  sock = stedstat->sock_fd;
        
    timeout.tv_sec = CONNECT_REQ_TIMEOUT;
    timeout.tv_usec = 0;

    FD_ZERO(&fds);
    FD_SET(sock, &fds);
        
    sprintf(connect_req, 
             "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n\r\n",
             hub_name, hub_port, hub_name, hub_port);
    if ( send(sock, connect_req , strlen(connect_req), 0) < 0){
        /* この時点では全ての error を error として終了処理する */
        SET_ERRNO();
        print_err(LOG_ERR, "send_connect_req: send %s (%d)\n", strerror(errno), errno);
        return(-1);
    }
    ret = select(sock+1, &fds, NULL, NULL, &timeout);
    if( ret = 0){
        SET_ERRNO();    
        print_err(LOG_ERR,"send_connect_req: select %s\n", strerror(errno));
        return(-1);
    } else if(ret < 0){
        SET_ERRNO();            
        print_err(LOG_ERR,"send_connect_req: select %s\n", strerror(errno));            
        return(-1);
    }

    if(FD_ISSET(sock, &fds)) {
        if( (cnt = recv(sock, buf, SOCKBUFSIZE,0)) < 0)  {
            SET_ERRNO();
            if(errno == EINTR || errno == EWOULDBLOCK || errno == 0){
                if(debuglevel > 1){                    
                    print_err(LOG_NOTICE, "send_connect_req: recv %s\n", strerror(errno));
                }
                return(0);
            }
            print_err(LOG_ERR,"send_connect_req: recv %s (%d)\n", strerror(errno), errno);
            return(-1);
        }
        if( cnt == 0){
            print_err(LOG_ERR, "connection with proxy server is being closed\n");
            return(-1);
        }
        /*
         * 受信データに HTTP ステータスラインが途中までしかて含まれていないかもし
         * れないし、CRLF の後に、他の仮想ハブからのデータがもう含まれているかも
         * しれないが、ここではそれらは無視する。
         * （ここではデータを救いようがないので・・）
         */
        if((http_ver = strtok((char *)buf, " ")) == NULL){
            print_err(LOG_ERR, "send_connect_req: Illegal responce from Proxy server\n");
            return(-1);
        }
        if((http_stat_char = strtok(NULL, " "))  == NULL){
            print_err(LOG_ERR, "send_connect_req: Illegal responce from Proxy server\n");
            return(-1);
        }
        http_stat = atoi(http_stat_char);
        /*
         * Proxy から返されたステータスコードを return する
         */
        if( http_stat != HTTP_STAT_OK){
            return(http_stat);
        }
        return(0);
    }
    /* 起こりえないと思うが */
    return(-1);
}

/*****************************************************************************
 * stat2string()
 *
 * Proxy から返されたステータスコードの短い説明分を返す。(RFC 2616 より)
 * ステータスラインにも含まれているはずだが、内部で解決することにした。
 * あまり意味は無い・・・
 *
 *  引数：
 *           stat : proxy サーバから返って来たステータスコード
 * 戻り値：
 *          ステータスコードに対応する説明の文字列へのポインタ
 *****************************************************************************/
char *
stat2string(int stat)
{
    static char desc[50];
    switch(stat){
        case 100 :
            strcpy(desc, "Continue");
            break;                                 
        case 101 :
            strcpy(desc, "Switching Protocols");
            break;                       
        case 200 :
            strcpy(desc, "OK");
            break;                                        
        case 201 :
            strcpy(desc, "Created");
            break;                                   
        case 202 :
            strcpy(desc, "Accepted");
            break;                                  
        case 203 :
            strcpy(desc, "Non-Authoritative Information");
            break;             
        case 204 :
            strcpy(desc, "No Content");
            break;                                
        case 205 :
            strcpy(desc, "Reset Content");
            break;                             
        case 206 :
            strcpy(desc, "Partial Content");
            break;                           
        case 300 :
            strcpy(desc, "Multiple Choices");
            break;                          
        case 301 :
            strcpy(desc, "Moved Permanently");
            break;                         
        case 302 :
            strcpy(desc, "Found");
            break;                                     
        case 303 :
            strcpy(desc, "See Other");
            break;                                 
        case 304 :
            strcpy(desc, "Not Modified");
            break;                              
        case 305 :
            strcpy(desc, "Use Proxy");
            break;                                 
        case 307 :
            strcpy(desc, "Temporary Redirect");
            break;                        
        case 400 :
            strcpy(desc, "Bad Request");
            break;                               
        case 401 :
            strcpy(desc, "Unauthorized");
            break;                              
        case 402 :
            strcpy(desc, "Payment Required");
            break;                          
        case 403 :
            strcpy(desc, "Forbidden");
            break;                                 
        case 404 :
            strcpy(desc, "Not Found");
            break;                                 
        case 405 :
            strcpy(desc, "Method Not Allowed");
            break;                        
        case 406 :
            strcpy(desc, "Not Acceptable");
            break;                            
        case 407 :
            strcpy(desc, "Proxy Authentication Required");
            break;             
        case 408 :
            strcpy(desc, "Request Time-out");
            break;                          
        case 409 :
            strcpy(desc, "Conflict");
            break;                                  
        case 410 :
            strcpy(desc, "Gone");
            break;                                      
        case 411 :
            strcpy(desc, "Length Required");
            break;                           
        case 412 :
            strcpy(desc, "Precondition Failed");
            break;                       
        case 413 :
            strcpy(desc, "Request Entity Too Large");
            break;                  
        case 414 :
            strcpy(desc, "Request-URI Too Large");
            break;                     
        case 415 :
            strcpy(desc, "Unsupported Media Type");
            break;                    
        case 416 :
            strcpy(desc, "Requested range not satisfiable");
            break;           
        case 417 :
            strcpy(desc, "Expectation Failed");
            break;                        
        case 500 :
            strcpy(desc, "Internal Server Error");
            break;                     
        case 501 :
            strcpy(desc, "Not Implemented");
            break;                           
        case 502 :
            strcpy(desc, "Bad Gateway");
            break;                               
        case 503 :
            strcpy(desc, "Service Unavailable");
            break;                       
        case 504 :
            strcpy(desc, "Gateway Time-out");
            break;                          
        case 505 :
            strcpy(desc, "HTTP Version not supported");
            break;
        default:
            strcpy(desc, "Unknown Code");
            break;
    }
    return(desc);
}

