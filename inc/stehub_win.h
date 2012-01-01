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
/*************************************************
 *  stehub_win.h
 *    
 *************************************************/

#ifndef __STEHUB_WIN_H
#define __STEHUB_WIN_H

#include <winsock2.h>   
#include <Windows.h>    
#include <time.h>       
#include <winioctl.h>   
#include <setupapi.h>   
#include <dbt.h>        
#include <direct.h>
#include <Winsvc.h>
#include <io.h>
#include "getopt_win.h"

void WINAPI stehub_svc_main(DWORD , LPTSTR *);
DWORD WINAPI stehub_svc_ctrl_handler(DWORD ,DWORD,LPVOID, LPVOID);
BOOL stehub_delete_svc();
BOOL stehub_install_svc();

HANDLE  hStedLog;          /* デバッグログ用のファイルハンドル */
SERVICE_STATUS            stehubServiceStatus;
SERVICE_STATUS_HANDLE     stehubServiceStatusHandle;
BOOL isTerminal;    // コンソールから起動されたかどうか？
GUID InterfaceGuid; // = GUID_NDIS_LAN_CLASS;

#define STEHUB_LOG_FILE   "C:\\stehub.log" /* ログファイル */


/*
 * stehub デーモンが使う stehub の管理用構造体
 */
typedef struct stehub_stat
{
    BOOL        state;      /* 今のところ使っていない。*/
} stehubstat_t;

#endif /* #ifndef __STEHUB_WIN_H */
