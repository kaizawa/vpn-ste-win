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
 *  ste_win.h
 *    
 *************************************************/

#ifndef __STED_WIN_H
#define __STED_WIN_H

#define STED_LOG_FILE     "C:\\sted.log" // ログファイル

/*
 * もし syslog 用の header ファイルが include されて
 * 無かったら、ここでいくつかの LOG レベルを定義する。
 */
#ifndef _SYS_SYSLOG_H
#define LOG_EMERG       0       /* system is unusable */
#define LOG_ALERT       1       /* action must be taken immediately */
#define LOG_CRIT        2       /* critical conditions */
#define LOG_ERR         3       /* error conditions */
#define LOG_WARNING     4       /* warning conditions */
#define LOG_NOTICE      5       /* normal but signification condition */
#define LOG_INFO        6       /* informational */
#define LOG_DEBUG       7       /* debug-level messages */
#endif
/*
 * もし ethernet.h が include されていなかったらここで ETHERMAX を定義する。
 */
#ifndef _SYS_ETHERNET_H
#define ETHERMAX 1514
#endif

/*
 * EWOULDBLOCK, ECONNABORTED が定義されてなかったらここで定義。
 * こんな、個別に define するやり方でよいのだろうか？
 */
#ifndef EWOULDBLOCK
#define EWOULDBLOCK             WSAEWOULDBLOCK
#endif
#ifndef ECONNABORTED
#define ECONNABORTED            WSAECONNABORTED
#endif
/*
 * sted の Windows 用の関数のプロトタイプ
 */
extern void WINAPI
sted_svc_main(
    DWORD argc,
    LPTSTR *argv
    );

extern DWORD WINAPI
sted_svc_ctrl_handler(
    DWORD dwControl,
    DWORD dwEventType,
    LPVOID lpEventData,
    LPVOID lpContext
    );

extern BOOL
sted_install_svc();

extern BOOL
sted_delete_svc();

extern int
ioctl_ste(
    IN HANDLE ste_handle,
    IN UINT cmd
    );

#endif /* #ifndef __STED_WIN_H */
