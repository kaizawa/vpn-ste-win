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

/********************************************************************
 * 仮想 NIC ドライバと仮想 NIC デーモン（サービス）の通信に
 * 使われる IOCTL コマンド。
 *
 *  REGSVC    仮想 NIC デーモンを登録する 
 *  UNREGSVC  仮想 NIC デーモンを登録解除する
 *
 * Windows の IOCTL コマンドは METHOD_NEITHER を使っているので、
 * IRP は User-mode の仮想アドレス を提供する。
 * User-mode のアドレスは Parameters.DeviceIoControl.Type3InputBuffer
 * に入る。
 ********************************************************************/
#ifndef __STE_H
#define __STE_H

#ifdef STE_WINDOWS
/* Windows 用 */
#define REGSVC   (ULONG) CTL_CODE(FILE_DEVICE_UNKNOWN, 2, METHOD_NEITHER, FILE_ANY_ACCESS)
#define UNREGSVC (ULONG) CTL_CODE(FILE_DEVICE_UNKNOWN, 3, METHOD_NEITHER, FILE_ANY_ACCESS)
#else
/* Solaris 用 */
#define REGSVC   0xabcde0
#define UNREGSVC 0xabcde1       
#endif /* End of #ifdef STE_WINDOWS */

#ifdef _KERNEL
#ifdef  STE_WINDOWS
#include "ste_win.h"
#endif // #ifdef STE_WINDOWS
#endif // #ifdef _KERNEL

#endif // #ifndef __STE_H 
