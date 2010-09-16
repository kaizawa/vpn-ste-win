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
 * sted.c
 *
 *  仮想 NIC デーモンの Windows 版。
 *  起動時に -I オプションを指定することによって、Windows サービスとして
 *  登録することができる。
 *
 *   Usage: sted [ -I | -U ] [ [-i instance] | [-h hub[:port]] | [-p proxy[:port]] ]
 *
 *  引数:
 *  
 *       -I : サービスとして登録。
 *       -U : 登録解除
 *
 *    以下の引数は、コンソールコマンドして実行させる時の引数、もしくは
 *    はコントロールパネルの「サービス」にて「開始パラメータ」として
 *    渡せる引数です。
 *
 *    -i instance     ste デバイスのインスタンス番号
 *                    指定されなければ、デフォルトで 0(=\\\\.\\STE0)。
 *                 
 *    -h hub[:port]   仮想ハブ（stehub）が動作するホストを指定する。
 *                    指定されなければ、デフォルトで localhost:80。
 *                    コロン(:)の後にポート番号が指定されていれば
 *                    そのポート番号に接続にいく。デフォルトは 80。
 *
 *    -p proxy[:port] 経由するプロキシサーバを指定する。
 *                    デフォルトではプロキシサーバは使われない。
 *                    コロン(:)の後にポート番号が指定されていれば
 *                    そのポート番号に接続にいく。デフォルトは 80。
 *
 *****************************************************************************/
#include <stdio.h>
#include <winsock2.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <Windows.h>
#include <Winsvc.h>   // Windows サービスのためのヘッダ
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <setupapi.h>
#include <dbt.h>
#include <winioctl.h>
#include <ntddndis.h> // for IOCTL_NDIS_QUERY_GLOBAL_STATS
#include <ndisguid.h> // for GUID_NDIS_LAN_CLASS
#include "ste.h"
#include "sted.h"
#include "getopt_win.h"
#include <io.h>

WSAEVENT                  EventArray[2]; // socket と ste ドライバ用の 2 つの Event の配列
GUID                      InterfaceGuid; // = GUID_NDIS_LAN_CLASS
int                       debuglevel;    // デバッグレベル。
BOOL                      bRunning = TRUE;
HANDLE                    hStedLog;      // デバッグログ用のファイルハンドル
SERVICE_STATUS            stedServiceStatus;
SERVICE_STATUS_HANDLE     stedServiceStatusHandle;
BOOL                      isTerminal;    // コンソールから起動されたかどうか？

/**************************************************************************
 * main()
 * 
 * 引数が -I もしくは -U だった場合には本プログラム（仮想 NIC デーモン）
 * を Windows のサービスとして登録/登録解除する。
 * それ以外の引数が渡された場合には ste_svc_main() を直接呼び出し、引数も
 * そのまま ste_svc_main() に渡す。
 * 
 * 引数(argvとして）:
 * 
 *      -I : サービスとして登録。
 *      -U : 登録解除
 * 
 **************************************************************************/
int WINAPIV
main(int argc, char *argv[])
{
    int c;

    SERVICE_TABLE_ENTRY  DispatchTable[] = {
        {  "Sted", sted_svc_main},
        {  NULL,   NULL         }
    };

    isTerminal = _isatty(_fileno(stdout))? TRUE:FALSE;    

    //
    // 引数が無ない場合。
    // コマンドプロンプトから呼ばれた時は sted_svc_main() を呼び、
    // そうでなければ StartServiceCtlDispatcher() を呼ぶ。
    //
    if(argc == 1 ){
        if(isTerminal == TRUE){
            sted_svc_main(argc, argv);
            return(0);
        } else {
            StartServiceCtrlDispatcher(DispatchTable);
            return(0);
        }
    }
    
    while((c = getopt(argc, argv, "IU")) != EOF ){        
        switch(c){
            case 'I':
                // sted.exe をサービスとして登録
                if(sted_install_svc())
                    printf("Service Installed Sucessfully\n");
                else
                    printf("Error Installing Service\n");
                break;
            case 'U':
                // sted.exe のサービスとして登録を解除
                if(sted_delete_svc())
                    printf("Service UnInstalled Sucessfully\n");
                else
                    printf("Error UnInstalling Service\n");
                break;
            default :
                //
                // 引数が -U、-I でなければコマンドプロンプト内で
                // sted.exe を起動したいのだと判断し、引数を全て
                // sted_svc_main() に渡して呼び出す。
                //
                sted_svc_main(argc, argv);
                return(0);
        }
    }
    return(0);    
}

/*******************************************************************************
 *sted_svc_main()
 *
 * 仮想 NIC デーモンのメインルーチン。
 *
 * o コンソール上から起動された場合には、起動時に main() に渡された引数がこの関数に
 *   そのまま渡される。
 * 
 * o サービスとして起動された場合には サービス(services.exe)の「開始パラメータ」
 *   に記述された文字列が引数として渡される。
 *
 * 引数:
 *
 *    -i instance     ste デバイスのインスタンス番号
 *                    指定されなければ、デフォルトで 0(=\\\\.\\STE0)。
 *                 
 *    -h hub[:port]   仮想ハブ（stehub）が動作するホストを指定する。
 *                    指定されなければ、デフォルトで localhost:80。
 *                    コロン(:)の後にポート番号が指定されていれば
 *                    そのポート番号に接続にいく。デフォルトは 80。
 *
 *    -p proxy[:port] 経由するプロキシサーバを指定する。
 *                    デフォルトではプロキシサーバは使われない。
 *                    コロン(:)の後にポート番号が指定されていれば
 *                    そのポート番号に接続にいく。デフォルトは 80。
 *                    
 *    -d level        デバッグレベル。1 以上にした場合は フォアグランド
 *                    で実行され、標準エラー出力にデバッグ情報が
 *                    出力される。デフォルトは 0。
 * 
 *******************************************************************************/
void WINAPI
sted_svc_main(DWORD argc, LPTSTR *argv)
{
    int                 sock_fd;
    char               *hub   = NULL;
    char               *proxy = NULL;    
    int                 instance = 0;
    stedstat_t          stedstat[1];        
    struct timeval      timeout;
    int                 Index;
    char                localhost[] = "localhost:80";    
    int                 c;

    isTerminal = _isatty(_fileno(stdout))? TRUE:FALSE;

    if (argc > 1){
        while((c = getopt(argc, argv, "d:i:h:p:")) != EOF){
            switch(c){
                case 'i':
                    instance = atoi(optarg);                
                    break;
                case 'h':
                    hub   = optarg;
                    break;
                case 'p':
                    proxy = optarg;
                    break;
                case 'd':
                    debuglevel = atoi(optarg);
                    break;
                default:
                    if(isTerminal == TRUE){
                        print_usage(argv[0]);
                        return;
                    }
                    debuglevel = 0;
            }
        }
    } else {
        debuglevel = 0;
    }

    if(hub == NULL)
        hub = localhost;    

    /* socket および ste ドライバのデータ受信通知用のイベントオブジェクトを作成 */    
    EventArray[0] = CreateEvent(NULL, FALSE, FALSE, NULL); // Socket 用     
    EventArray[1] = CreateEvent(NULL, FALSE, FALSE, NULL); // ste ドライバ用 

    /* ログファイルをオープン */
    hStedLog = CreateFile(STED_LOG_FILE,
                          GENERIC_READ|GENERIC_WRITE,
                          FILE_SHARE_READ| FILE_SHARE_WRITE,
                          NULL,
                          CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    SetFilePointer(hStedLog, 0, NULL, FILE_END);

    print_err(LOG_DEBUG, "debuglevel = %d\n", debuglevel);

    if(isTerminal == FALSE){
        /* サービスとして呼ばれている（コマンドプロンプトから呼ばれていない）場合 */
        stedServiceStatus.dwServiceType = SERVICE_WIN32;
        stedServiceStatus.dwCurrentState = SERVICE_START_PENDING;
        stedServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
        stedServiceStatus.dwWin32ExitCode = 0;
        stedServiceStatus.dwServiceSpecificExitCode = 0;
        stedServiceStatus.dwCheckPoint = 0;
        stedServiceStatus.dwWaitHint = 0;
        InterfaceGuid = GUID_NDIS_LAN_CLASS;
    
        /* サービスコントロールハンドラーを登録 */
        stedServiceStatusHandle = RegisterServiceCtrlHandlerEx(
            "Sted",                //LPCTSTR
            sted_svc_ctrl_handler, //LPHANDLER_FUNCTION_EX
            (LPVOID)stedstat       //LPVOID 
            );

        if (stedServiceStatusHandle == (SERVICE_STATUS_HANDLE)0){
            return;
        }
    
        stedServiceStatus.dwCurrentState = SERVICE_RUNNING;
        stedServiceStatus.dwCheckPoint = 0;
        stedServiceStatus.dwWaitHint = 0;
    
        if (!SetServiceStatus (stedServiceStatusHandle, &stedServiceStatus)){
            // SetServiceStatus が失敗した場合の処理・・        
            print_err(LOG_ERR, "SetServiceStatus Failed\n");
        }
    }
    

    /* 仮想 NIC デバイスをオープン */
    if( open_ste(stedstat, STEPATH , instance ) < 0){
        print_err(LOG_ERR,"failed to open STE device\n");
        goto err;
    }
    print_err(LOG_INFO, "Successfully opened STE device\n");    
  
    /* HUB との間の Connection をオープン */
    if ((sock_fd = open_socket(stedstat, hub, proxy)) < 0){
        print_err(LOG_ERR,"failed to open connection with hub\n");
        goto err;
    }

    bRunning = TRUE;

    while(bRunning){
        int ret;
        
        Index = WSAWaitForMultipleEvents( 2 , EventArray , FALSE , 5000 , FALSE ) ;

        switch(Index){
            case 0 + WSA_WAIT_EVENT_0:
                /* Socket 用の Event だ */
                ret = read_socket(stedstat);
                if( ret < 0){
                    /* socket にエラーが発生した模様。再接続に行く */
                    CLOSE(sock_fd);
                    stedstat->sock_fd = -1;
                    if ((sock_fd = open_socket(stedstat, hub, proxy)) < 0){
                        print_err(LOG_ERR,"failed to re-open connection with hub\n");
                        bRunning = FALSE;
                        goto err;
                    }
                }
                break;
            case 1 + WSA_WAIT_EVENT_0:
                /* Driver 用の Event だ */
                do {
                    /* 戻り値が 0 以上（データ有）である限り read_ste() を繰り返す */
                    ret = read_ste(stedstat);
                    if(ret < 0){
                        /* socket への send() にてエラーが発生した模様。再接続に行く */
                        CLOSE(sock_fd);
                        stedstat->sock_fd = -1;                        
                        if ((sock_fd = open_socket(stedstat, hub, proxy)) < 0){
                            print_err(LOG_ERR,"failed to re-open connection with hub\n");
                            bRunning = FALSE;
                        }
                    }
                } while( ret > 0);
                break;
            case WSA_WAIT_FAILED:
                if(debuglevel > 1){
                    print_err(LOG_ERR,"WSAWaitForMultipleEvents returns WSA_WAIT_FAILED\n");
                }
                break ;
            case WSA_WAIT_TIMEOUT:
                if(debuglevel > 2){                
                    print_err(LOG_ERR,"WSAWaitForMultipleEvents returns WSA_WAIT_TIMEOUT\n");
                }
                break ;
            default :
                if(debuglevel > 1){                
                    print_err(LOG_ERR,"WSAWaitForMultipleEvents returns unknown index: %d\n", Index);
                }
                break;
        }
    }

  err:
    if(stedstat->ste_handle != NULL){
        ioctl_ste(stedstat->ste_handle, UNREGSVC);
        stedstat->ste_handle = INVALID_HANDLE_VALUE;
    }
    print_err(LOG_ERR,"Stopped\n");
    return;
}

/****************************************
 * Windows サービス登録ルーチン
 * 
 ****************************************/
BOOL sted_install_svc()
{
    LPCTSTR lpszBinaryPathName;
    TCHAR strDir[1024];
    HANDLE schSCManager,schService;
    
    GetCurrentDirectory(1024, strDir);
    strcat((char *)strDir, "\\sted.exe"); 
    schSCManager = OpenSCManager(NULL,NULL,SC_MANAGER_ALL_ACCESS);

    if (schSCManager == NULL) 
        return FALSE;

    lpszBinaryPathName=strDir;

    schService = CreateService(schSCManager,"Sted", 
                               "Sted Virtual NIC daemon", // 表示用サービス名
                               SERVICE_ALL_ACCESS,        // アクセス
                               SERVICE_WIN32_OWN_PROCESS, // サービスタイプ
                               SERVICE_DEMAND_START,      // スタートタイプ
                               SERVICE_ERROR_NORMAL,      // エラーコントロールタイプ
                               lpszBinaryPathName,        // バイナリへのパス
                               NULL, // No load ordering group 
                               NULL, // No tag identifier 
                               NULL, // No dependencies
                               NULL, // LocalSystem account
                               NULL);// No password

    if (schService == NULL)
        return FALSE; 

    CloseServiceHandle(schService);
    return TRUE;
}

/****************************************
 * Windows サービス登録解除ルーチン
 * 
 ****************************************/
BOOL sted_delete_svc()
{
    HANDLE schSCManager;
    SC_HANDLE hService;
    schSCManager = OpenSCManager(NULL,NULL,SC_MANAGER_ALL_ACCESS);

    if (schSCManager == NULL)
        return FALSE;
    hService=OpenService(schSCManager, "Sted", SERVICE_ALL_ACCESS);
    if (hService == NULL)
        return FALSE;
    if(DeleteService(hService)==0)
        return FALSE;
    if(CloseServiceHandle(hService)==0)
        return FALSE;

    return TRUE;
}

/***********************************************************
 * print_err()
 *
 * エラーメッセージを表示するルーチン。
 *
 * 引数: 
 *       level : エラーレベル。つかってない。
 *       format: printf() のフォーマット
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

    if(isTerminal == TRUE){
        printf("%s", buf);
    } else {
        WriteFile(hStedLog, buf, strlen(buf), &length, NULL);
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
    printf ("Usage: %s [[ -i instance] [-h hub[:port]] [-p proxy[:port]] [-d level]] [-I|-U]\n",argv);
    printf ("\t-i instance     : Instance number of the ste device\n");
    printf ("\t-h hub[:port]   : Virtual HUB and its port number\n");
    printf ("\t-p proxy[:port] : Proxy server and its port number\n");
    printf ("\t-d level        : Debug level[0-3]\n");
    printf ("\t-I              : Install Service\n");
    printf ("\t-U              : Uninstall Service\n");

    exit(0);
}

/*****************************************************************************
 * open_ste()
 *
 * コントロールデバイスへのハンドルをオープンする。
 * IOCTL コマンドの REGSVC（ste オリジナル）を発行する。
 *
 *  引数：
 *           stedstat :  sted 管理構造体
 *           devname    : デバイス名(\\\\.\\STE)
 *           ppa        : 仮想 NIC のインスタンス番号
 * 戻り値：
 *         正常時   : ファイルディスクリプタ
 *         エラー時 :  -1
 *****************************************************************************/
int
open_ste(stedstat_t *stedstat, char *devname, int ppa)
{
    HANDLE         ste_handle;
    char           devpath[STE_MAX_DEVICE_NAME];

    //
    // 実際にオープンするデバイスのファイル名は devname + ppa
    // つまり \\\\.\\STE0 や \\\\.\\STE1 となる。
    //
    sprintf(devpath, "%s%d", devname, ppa);

    print_err(LOG_INFO,"device path = %s\n", devpath);    
    
    /*
     * デバイスをオープンし、ファイルハンドルを得る。
     */
    ste_handle = CreateFile ( 
        TEXT(devpath),
        GENERIC_READ | GENERIC_WRITE,// FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_READ,
        NULL,                        // SECURITY_ATTRIBUTES 無し
        OPEN_EXISTING,               // 特別な作成フラグ無し
        FILE_ATTRIBUTE_NORMAL,       // 特別な属性無し
        NULL);

    if (INVALID_HANDLE_VALUE == ste_handle) {
        print_err(LOG_ERR,"Failed to open the control device: %s\n", devpath);
        return (-1);
    } 

    /* ドライバに REGSVC コマンドの送信する */
    ioctl_ste(ste_handle, REGSVC);
    
    stedstat->ste_handle = ste_handle;

    return(0);
}

/**************************************************************
 *  ioctl_ste()
 *
 *  この中で REGSVC/UNREGSVC ioctl コマンドの送信を行う。
 *
 * 引数
 *
 *    ste_handle : ste デバイスをオープンしたファイルハンドル
 *    cmd        : ste デバイスに送信する IOCTL コマンド
 *
 * 戻り値
 *
 *    正常時 :    0
 *    障害時 :   -1
 *
 *************************************************************/
int
ioctl_ste(    
    IN HANDLE ste_handle,
    IN UINT   cmd
    )
{
    UINT bytes;       
    
    // IOCTL コマンドにてイベントオプジェクトを仮想 NIC ドライバに 渡す
    if(!DeviceIoControl(
           ste_handle,
           cmd,
           (LPVOID) EventArray[1], // sted_svc_main() で確保したイベントオブジェクト
           0,
           NULL,
           0,
           &bytes,
           NULL)){
        print_err(LOG_ERR, "ioctl_ste: DeviceIoControl() failed\n");
        return(-1);
    } else {
        if(debuglevel > 1){
            print_err(LOG_DEBUG, "IOCTL succeeded\n");
        }
        return(0);
    }
}

/**************************************************
 * Ste Virtual NIC driver 書き込み用ルーチン
 *
 * 引数
 *
 *    stedstat: sted_stat 構造体
 *
 * 戻り値
 *
 *    正常時 :    0
 *    障害時 :   -1
 ***************************************************/
int
write_ste(stedstat_t *stedstat)
{
    HANDLE        ste_handle;
    DWORD         writesize;   // WriteFile() で実際に書き込んだサイズ
    BOOL          Ret;
    
    ste_handle = stedstat->ste_handle;

    if(debuglevel > 1){
        print_err(LOG_INFO, "write_ste called\n");
    }

    Ret = WriteFile(
        ste_handle,
        stedstat->wdatabuf,
        stedstat->orgdatalen,
        &writesize,
        NULL
        );

    if ( Ret == FALSE ){
        print_err(LOG_ERR, "write_ste: WriteFile returns with FALSE\n");
        if(debuglevel > 1){
            print_err(LOG_INFO, "write_ste returned\n");            
        }        
        return(-1);
    }

    if(debuglevel > 1){
        print_err(LOG_DEBUG, "wite_ste: WriteFile Succeeded. Wrote %d bytes\n", writesize);
        print_err(LOG_INFO, "write_ste returned\n");
    }
    
    return(0);
}

/*********************************************************************
 * Ste Virtual NIC driver 読み込み用ルーチン
 * 
 * 引数
 *
 *    stedstat: sted_stat 構造体
 *
 * 戻り値
 *
 *    正常時 :   読み込んだデータサイズ。（ 0 もありうる）
 *    障害時 :   -1 (socket への書き込みのエラー時）
 **********************************************************************/
int
read_ste(stedstat_t *stedstat)
{
    HANDLE          ste_handle;
    BOOL            Ret;    
    int             readsize;   // ReadFile で実際に読み込んだサイズ
    int             pad = 0;    // パディング 
    int             remain = 0; // 全データ長を 4 で割った余り 
    stehead_t       steh;
    unsigned char  *rdatabuf = stedstat->rdatabuf; // ドライバからの読み込み用バッファ 
    unsigned char  *sendbuf  = stedstat->sendbuf;  // Socket 送信バッファ        
    unsigned char  *sendp;     // Socket 送信バッファの書き込み位置ポインタ 

    sendp = sendbuf + stedstat->sendbuflen;        
    ste_handle = stedstat->ste_handle;

    if(debuglevel > 1){
        print_err(LOG_INFO, "read_ste called\n");
    }
    
    Ret = ReadFile(
        ste_handle,
        rdatabuf,
        SOCKBUFSIZE,
        &readsize,
        NULL
        );
    
    if ( Ret == FALSE ){
        if(debuglevel > 1)
            print_err(LOG_ERR, "read_ste: ReadFile returns with FALSE\n");
        if(debuglevel > 2)
            print_err(LOG_INFO, "read_ste returned\n");
        return(0);        
    }

    if(debuglevel > 1){
        print_err(LOG_DEBUG, "read_ste: read %d bytes\n", readsize);
    }
            
    if(readsize == 0){
        if(debuglevel > 1){
            print_err(LOG_INFO, "read_ste returned\n");
        }        
        return(0);
    }    

    if (debuglevel > 1){
        print_err(LOG_DEBUG,"========= from ste %d bytes ==================\n", readsize);
        if(debuglevel > 2){
            int i;
            for (i = 0; i < (int)readsize; i++){
                if((i)%16 == 0){
                    print_err(LOG_DEBUG,"\n%04d: ", i);                    
                }
                print_err(LOG_DEBUG, "%02x ", rdatabuf[i] & 0xff);                
            }
            print_err(LOG_DEBUG, "\n\n");
        }
    }
    
    if( remain = ( sizeof(stehead_t) + readsize ) % 4 )
        pad = 4 - remain;
    steh.len = htonl(readsize + pad);
    steh.orglen = htonl(readsize);

    if(debuglevel > 1){
        print_err(LOG_DEBUG, "stehead.len    = %d\n", ntohl(steh.len));
        print_err(LOG_DEBUG, "stehead.pad    = %d\n", pad);                                    
        print_err(LOG_DEBUG, "stehead.orglen = %d\n", ntohl(steh.orglen));
    }
    memcpy(sendp, &steh, sizeof(stehead_t));
    memcpy(sendp + sizeof(stehead_t), rdatabuf, readsize);
    memset(sendp + sizeof(stehead_t) + readsize, 0x0, pad);

    /*
     * 実際に読み込んだデータのサイズ＋ヘッダサイズ＋パッドサイズ
     * を stedstat にセット
     */
    stedstat->sendbuflen += sizeof(stehead_t) + readsize + pad;
    /*
     * ste から受け取ったサイズが ETHERMAX(1514byte)より小さいか、
     * 送信バッファへの書き込み済みサイズが SENDBUF_THRESHOLD 以上
     * になったら送信する
     */
    if( readsize < ETHERMAX || stedstat->sendbuflen > SENDBUF_THRESHOLD){
        if(debuglevel > 1){        
            print_err(LOG_DEBUG, "readsize = %d, sendbuflen = %d\n",
                      readsize, stedstat->sendbuflen);
        }
        if ( write_socket(stedstat) < 0){
            print_err(LOG_ERR, "read_ste returned\n");
            return(-1);
        }
    }

    if(debuglevel > 1){        
        print_err(LOG_ERR, "read_ste returned\n");
    }
    
    return(readsize);
}


/***************************************************************************************
 * sted_svc_ctrl_handler()
 * 
 * サービスステータスハンドラー
 * DEVICEEVENT を拾うためには Handler ではなく、HandlerEx じゃないといけないらしい。
 * http://msdn.microsoft.com/library/default.asp?url=/library/en-us/dllproc/base/handlerex.asp
 *
 * まったく呼ばれていないような・・？
 ****************************************************************************************/
DWORD WINAPI
sted_svc_ctrl_handler(
    DWORD dwControl,
    DWORD dwEventType,
    LPVOID lpEventData,  
    LPVOID lpContext
    )
{
    PDEV_BROADCAST_HDR  p      = (PDEV_BROADCAST_HDR) lpEventData;
    WPARAM              wParam = (WPARAM) dwEventType;
    stedstat_t         *stedstat = NULL;

    stedstat = (stedstat_t *)lpContext;

    if(debuglevel > 1){
        print_err(LOG_DEBUG, "Service Status Handler sted_svc_ctrl_handler called\n");
    }
    
    switch(dwControl){
        case SERVICE_CONTROL_DEVICEEVENT:
            break;
        case SERVICE_CONTROL_PAUSE: 
            stedServiceStatus.dwCurrentState = SERVICE_PAUSED;
            break;
        case SERVICE_CONTROL_CONTINUE:
            stedServiceStatus.dwCurrentState = SERVICE_RUNNING;
            break;
        case SERVICE_CONTROL_STOP:
            stedServiceStatus.dwWin32ExitCode = 0;
            stedServiceStatus.dwCurrentState = SERVICE_STOPPED;
            stedServiceStatus.dwCheckPoint = 0;
            stedServiceStatus.dwWaitHint = 0;

            SetServiceStatus (stedServiceStatusHandle,&stedServiceStatus);
            bRunning=FALSE;

            if(stedstat->ste_handle != INVALID_HANDLE_VALUE){
                ioctl_ste(stedstat->ste_handle, UNREGSVC);
                stedstat->ste_handle = INVALID_HANDLE_VALUE;
            }
            break;
        case SERVICE_CONTROL_INTERROGATE:
            break; 
    }
    return NO_ERROR;
}
