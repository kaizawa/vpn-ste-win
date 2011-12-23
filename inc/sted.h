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
#ifndef __STED_H
#define __STED_H

#ifdef  STE_WINDOWS
#include "sted_win.h"
#define STEPATH "\\\\.\\STE"    /* ste デバイスのパス */
#else
#define STEPATH "/dev/ste"      /* ste デバイスのパス */
#endif

/*
 * Windows の場合 SAGetLastError() を使って errno にエラー番号をセットする
 * また、Windows の場合 socket の close には closesocket() を使う。
 */
#ifdef STE_WINDOWS
#define SET_ERRNO()   errno = WSAGetLastError()
#define CLOSE(fd)     closesocket(fd)
#else
#define SET_ERRNO()
#define CLOSE(fd)     close(fd)
#endif

/*******************************************************
 * o 仮想 NIC デーモンが利用する各種パラメータ
 *
 *  CONNECT_REQ_SIZE     proxy に対する CONNECT 要求の文字長 
 *  CONNECT_REQ_TIMEOUT  Proxy から CONNECT のレスポンスを受け取るタイムアウト
 *  STRBUFSIZE           getmsg(9F),putmsg(9F) 用のバッファのサイズ 
 *  PORT_NO              デフォルトの仮想ハブのポート番号
 *  SOCKBUFSIZE          recv(), send() 用のバッファのサイズ                
 *  ERR_MSG_MAX          syslog や、STDERR に出力するメッセージのサイズ   
 *  SENDBUF_THRESHOLD    送信一時バッファのデータを送信するしきい値。
 *  SELECT_TIMEOUT       select() 用のタイムアウト（Solaris 用)
 *  HTTP_STAT_OK         HTTP のステータスコード OK
 *  MAXHOSTNAME          ホスト名（HUBやProxy）の最大長 
 *  GETMSG_MAXWAIT       getmsg(9F) のタイムアウト値（Solaris 用)
 ********************************************************/
#define  CONNECT_REQ_SIZE         200    
#define  CONNECT_REQ_TIMEOUT      10  
#define  STRBUFSIZE               32768       
#define  PORT_NO                  80            
#define  SOCKBUFSIZE              32768     
#define  ERR_MSG_MAX              300         
#define  SENDBUF_THRESHOLD        3028    // ETHERMAX(1514) x 2
#define  SELECT_TIMEOUT           400000  // 400m sec = 0.4 sec
#define  HTTP_STAT_OK             200        
#define  MAXHOSTNAME              30          
#define  GETMSG_MAXWAIT           15
#define  STE_MAX_DEVICE_NAME      30

/*
 * 仮想 NIC デーモン sted と、仮想ハブデーモン stehub が通信を
 * 行う際、送受信する Ethernet フレームのデータに付加されるヘッダ。
 */
typedef struct stehead 
{
    int           len;    /* パディング後のデータサイズ */
    int           orglen; /* パディングする前のサイズ。*/
} stehead_t;

/*
 * sted デーモンが使う sted の管理用構造体
 * HUB との通信の情報や、仮想 NIC ドライバの情報を持っている。
 */
typedef struct sted_stat
{
    /* Socket 通信用用情報 */
    int           sock_fd;                 /* HUB または Proxy との通信につかう FD  */
    char          hub_name[MAXHOSTNAME];   /* 仮想ハブ名 */
    int           hub_port;                /* 仮想ハブのポート番号 */
    char          proxy_name[MAXHOSTNAME]; /* プロキシーサーバ名   */ 
    int           proxy_port;              /* プロキシーサーバのポート番号  */
    int           sendbuflen;              /* 送信バッファへの現在の書き込みサイズ  */
    int           datalen;                 /* パッドを含む Ethernet フレームのサイズ*/
    int           orgdatalen;              /* 元の Ethernet フレームのサイズ        */
    int           dataleft;                /* 未受信の Ethernet フレームのサイズ    */
    stehead_t     dummyhead;               /* 受信途中の stehead のコピー           */
    int           dummyheadlen;            /* 受信済みの stehead のサイズ           */
    int           use_syslog;              /* メッセージを STDERR でなく、syslog に出力する */
    unsigned char sendbuf[SOCKBUFSIZE];    /* Socket 送信用バッファ */
    unsigned char recvbuf[SOCKBUFSIZE];    /* Socket 受信用バッファ */
    /* ste ドライバ用情報 */
#ifdef STE_WINDOWS
    HANDLE        ste_handle;              /* 仮想 NIC デバイスをオープンしたファイルハンドル */
#else    
    int           ste_fd;                  /* 仮想 NIC デバイスをオープンした FD */
#endif    
    unsigned char wdatabuf[STRBUFSIZE]; /* ドライバへの書き込み用バッファ  */
    unsigned char rdatabuf[STRBUFSIZE]; /* ドライバからの読み込み用バッファ*/    
} stedstat_t;

/*
 * sted の内部関数のプロトタイプ
 */
extern void     print_err(int, char *, ...);
extern int      open_socket(stedstat_t *, char *, char *);
extern int      read_socket(stedstat_t *);
extern int      write_socket(stedstat_t *);
extern u_char  *read_socket_header(stedstat_t *, int *, unsigned char *);
extern int      send_connect_req(stedstat_t *);
extern char    *stat2string(int);
extern void     print_usage(char *);
extern int      open_ste(stedstat_t *, char *, int);
extern int      write_ste(stedstat_t *);
extern int      read_ste(stedstat_t *);

#endif /* #ifndef __STED_H */
