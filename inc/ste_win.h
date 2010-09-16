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
#ifndef __STE_WIN_H
#define __STE_WIN_H

#include <ndis.h>

#define      DebugLevel               3       // デバッグレベル
                                              //  1: エラーのみ
                                              //  2: 警告レベル
                                              //  3: 冗長出力
                                              //  4: さらに冗長出力

#define      ETHERMAX                 1514    // ヘッダ込みの Ethernet フレームの最大サイズ 
#define      ETHERMTU                 1500    // ヘッダ無しの Ethernet フレームの最大サイズ 
#define      ETHERMIN                 60      // ヘッダ無しの Ethernet フレームの最小サイズ
#define      ETHERADDRL               6       // Ethernet アドレス長
#define      ETHERLINKSPEED           1000000 // リンク速度 100Mbps
#define      MAX_MULTICAST            16      // 最大登録可能マルチキャストアドレス

#define      STE_DRIVER_VERSION       0x01    // Ste ドライバのバージョン
#define      STE_NDIS_MAJOR_VERSION   0x05    // NDIS のバージョン（Major)
#define      STE_NDIS_MINOR_VERSION   0x00    // NDIS のバージョン（Minor)
#define      STE_NDIS_VERSION         ((STE_NDIS_MAJOR_VERSION << 8) + (STE_NDIS_MINOR_VERSION))
#define      STE_MAX_SEND_PACKETS     5       // 一回のリクエストで受けられるパケットの最大数
#define      STE_VENDOR_NAME          "Admin2" // ベンダー名
#define      STE_MAX_MCAST_LIST       5       // 登録可能な最大マルチキャストアドレス数
#define      STE_DEVICE_NAME          "\\Device\\STE"     // デバイス名
#define      STE_SYMBOLIC_NAME        "\\DosDevices\\STE" // シンボリック名
#define      STE_MAX_TXDS             64      // 最大送信ディスクリプター数
#define      STE_MAX_RXDS             64      // 最大受信ディスクリプター数
#define      STE_QUEUE_MAX            64      // キューサイズ
#define      STE_MAX_WAIT_FOR_RECVINDICATE 5  // 未処理の受信通知パケットの確認回数
#define      STE_MAX_WAIT_FOR_RESET   10      // リセット処理の最大試行回数
#define      STE_MAX_DEVICE_NAME      30      // デバイス名の最大長
#define      STE_MAX_ADAPTERS         10      // 登録可能なアダプターの最大数（最大インスタンス数）
#define      STE_MAX_DEBUG_MSG        256     // デバッグ出力するメッセージの最大文字数

#ifdef       DEBUG
#define      DEBUG_PRINT0(level, format)              SteDebugPrint(level, format)
#define      DEBUG_PRINT1(level, format, val1)        SteDebugPrint(level, format, val1)
#define      DEBUG_PRINT2(level, format, val1, val2)  SteDebugPrint(level, format, val2)
#else
#define      DEBUG_PRINT0(level, format)
#define      DEBUG_PRINT1(level, format, val1)       
#define      DEBUG_PRINT2(level, format, val1, val2) 
#endif

#define SET_INFORMATION_BY_VALUE(length, value) \
            { \
                InformationLength = length; \
                if(InformationBufferLength < InformationLength){ \
                    Information = NULL; \
                    break; \
                } \
                ulTemp = value; \
                Information = (PVOID) &ulTemp; \
                break; \
            }

#define SET_INFORMATION_BY_POINTER(length, pointer) \
            { \
                InformationLength = length; \
                if(InformationBufferLength < InformationLength){ \
                    Information = NULL; \
                    break; \
                } \
                Information = (PVOID) pointer; \
                break; \
            }


/*
 * Queue 構造体
 */
typedef struct SteQueue
{
    LIST_ENTRY      List;       // パケットをキューイングするリスト
    UINT            QueueMax;   // キューに保持できるパケット数
    UINT            QueueCount; // キューに入っているパケット数
    NDIS_SPIN_LOCK  SpinLock;   // キューのロック
} STE_QUEUE;

/*
 * 仮想 NIC のインスタンス毎に確保される構造体
 */
typedef struct SteAdapter 
{
    //
    // アダプタ固有のデータ
    //
    struct SteAdapter      *NextAdapter;          // リストの次のアダプター
    NDIS_HANDLE             MiniportAdapterHandle;// アダプターハンドル
    NDIS_TIMER              RecvTimer;            // タイマーハンドル
    NDIS_TIMER              ResetTimer;           // タイマーハンドル    
    ULONG                   PacketFilter;         // フィルター
    UCHAR                   EthernetAddress[6];   // Ethernet アドレス
    PDEVICE_OBJECT          DeviceObject;         // IOCTL/READ/WRITE 用のデバイスオブジェクト
    NDIS_HANDLE             DeviceHandle;         // NdisMRegisterDevice で得られるデバイスハンドル
    NDIS_HANDLE             RxBufferPoolHandle;   // 受信バッファープールハンドル
    NDIS_HANDLE             RxPacketPoolHandle;   // 受信パケットプールハンドル
    STE_QUEUE               SendQueue;            // 送信キュー
    STE_QUEUE               RecvQueue;            // 受信キュー
    ULONG                   RecvIndicatedPackets; // NdisMindicateReceivePacket() を呼んだパケット数
    ULONG                   ResetTrys;            // リセット試行回数(SteResetTimverFunc が呼ばれた回数）
    PRKEVENT                EventObject;          // イベントオブジェクト
    NDIS_SPIN_LOCK          AdapterSpinLock;      // アダプター毎のロック
    NDIS_SPIN_LOCK          SendSpinLock;         // 送信処理用のロック
    NDIS_SPIN_LOCK          RecvSpinLock;         // 送信処理用のロック
    ULONG                   Instance;             // アダプターのインスタンス番号
    //
    // 統計情報
    //
    ULONG                   Ipackets;             // 正常受信フレーム数
    ULONG                   Opackets;             // 正常送信フレーム数
    ULONG                   Ierrors;              // エラー受信フレーム数
    ULONG                   Oerrors;              // エラー送信フレーム数
    ULONG                   NoResources;          // バッファ不足による受信エラー
    ULONG                   AlignErrors;          // アライメントエラー数
    ULONG                   OneCollisions;        // コリジョンが 1 回発生した送信フレーム数
    ULONG                   Collisions;           // コリジョンが 1 回以上発生した送信フレーム数    
} STE_ADAPTER, *PSTE_ADAPTER;


extern NDIS_HANDLE      NdisWrapperHandle;  // ste.c の中で定義されている
extern NDIS_OID         STESupportedList[]; // ste.c の中で定義されている
extern NDIS_SPIN_LOCK   SteGlobalSpinLock;  // ste.c の中で定義されている
extern STE_ADAPTER     *SteAdapterHead;     // ste.c の中で定義されている


/*
 * NDIS ミニポートドライバのエントリポイントのプロトタイプ宣言
 */
NDIS_STATUS 
SteMiniportInitialize(
    OUT PNDIS_STATUS OpenErrorStatus,
    OUT PUINT        SelectedMediumIndex,
    IN PNDIS_MEDIUM  MediumArray,
    IN UINT          MediumArraySize,
    IN NDIS_HANDLE   MiniportAdapterHandle,
    IN NDIS_HANDLE   WrapperConfigurationContext
    );

VOID 
SteMiniportShutdown(
    IN PVOID  ShutdownContext
    );

NDIS_STATUS 
SteMiniportQueryInformation(
    IN NDIS_HANDLE  MiniportAdapterContext,
    IN NDIS_OID     Oid,
    IN PVOID        InformationBuffer,
    IN ULONG        InformationBufferLength,
    OUT PULONG      BytesWritten,
    OUT PULONG      BytesNeeded
    );

NDIS_STATUS 
SteMiniportSetInformation(
    IN NDIS_HANDLE  MiniportAdapterContext,
    IN NDIS_OID     Oid,
    IN PVOID        InformationBuffer,
    IN ULONG        InformationBufferLength,
    OUT PULONG      BytesRead,
    OUT PULONG      BytesNeeded
    );

BOOLEAN
SteMiniportCheckForHang(
    IN NDIS_HANDLE  MiniportAdapterContext
    );

VOID 
SteMiniportHalt(
    IN  NDIS_HANDLE       MiniportAdapterContext
    );

VOID
SteMiniportReturnPacket(
    IN NDIS_HANDLE         MiniportAdapterContext,
    IN PNDIS_PACKET        Packet
    );

VOID 
SteMiniportSendPackets(
    IN  NDIS_HANDLE        MiniportAdapterContext,
    IN  PPNDIS_PACKET      PacketArray,
    IN  UINT               NumberOfPackets
    );

NDIS_STATUS 
SteMiniportReset(
    OUT PBOOLEAN           AddressingReset,
    IN  NDIS_HANDLE        MiniportAdapterContext
    );

VOID 
SteMiniportUnload(
    IN  PDRIVER_OBJECT     DriverObject
    );

/*
 * Ste ドライバの内部関数のプロトタイプ宣言
 */
NDIS_STATUS
SteCreateAdapter(
    IN OUT STE_ADAPTER   **pAdapter
    );

NDIS_STATUS
SteDeleteAdapter(
    IN  STE_ADAPTER      *Adapter
    );

NDIS_STATUS
SteRegisterDevice(
    IN STE_ADAPTER       *Adapter
    );

NDIS_STATUS
SteDeregisterDevice(
    IN STE_ADAPTER       *Adapter
    );

NTSTATUS
SteDispatchRead(
    IN PDEVICE_OBJECT     DeviceObject,
    IN PIRP               Irp
    );

NTSTATUS
SteDispatchWrite(
    IN PDEVICE_OBJECT     DeviceObject,
    IN PIRP               Irp
    );

NTSTATUS
SteDispatchIoctl(
    IN PDEVICE_OBJECT     DeviceObject,
    IN PIRP               Irp
    );

NTSTATUS
SteDispatchOther(
    IN PDEVICE_OBJECT     DeviceObject,
    IN PIRP               Irp
    );

NDIS_STATUS
SteAllocateRecvPacket(
    IN STE_ADAPTER       *Adapter,
    IN OUT NDIS_PACKET   **pPacket
    );

NDIS_STATUS
SteFreeRecvPacket(
    IN STE_ADAPTER       *Adapter,
    IN NDIS_PACKET       *Packet
    );

NDIS_STATUS
StePutQueue(
    IN STE_QUEUE     *Queue,
    IN NDIS_PACKET   *Packet
    );

NDIS_STATUS
SteGetQueue(
    IN STE_QUEUE     *Queue,
    IN NDIS_PACKET   **pPacket
    );

VOID 
SteResetTimerFunc(
    IN PVOID  SystemSpecific1,
    IN PVOID  FunctionContext,
    IN PVOID  SystemSpecific2,
    IN PVOID  SystemSpecific3
    );

VOID 
SteRecvTimerFunc(
    IN PVOID  SystemSpecific1,
    IN PVOID  FunctionContext,
    IN PVOID  SystemSpecific2,
    IN PVOID  SystemSpecific3
    );

NDIS_STATUS
SteInsertAdapterToList(
    IN  STE_ADAPTER  *Adapter
    );

NDIS_STATUS
SteRemoveAdapterFromList(
    IN  STE_ADAPTER    *Adapter
    );

NDIS_STATUS
SteFindAdapterByDeviceObject(
    IN      DEVICE_OBJECT    *DeviceObject,
    IN OUT  STE_ADAPTER      **pAdapter
    );

NDIS_STATUS
SteCopyPacketToIrp(    
    IN     NDIS_PACKET  *Packet,
    IN OUT PIRP          Irp
    );

VOID
StePrintPacket(
    NDIS_PACKET *Packet
    );

VOID
SteDebugPrint(
    IN ULONG     Level,
    IN PCCHAR    Format,
    ...
    );

#endif // #ifndef __STE_WIN_H 
