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

/********************************************************************************
 * ste.c
 * 
 * ドライバの説明
 *
 *    ste.sys
 * 
 *    仮想 NIC（ネットワークインターフェースカード）デバイスドライバ ste の Windows 版。
 *    NDIS 受け取ったデータを Ethernet フレームとしてユーザプロセスである sted に渡し、
 *    また、sted から受け取った Ethernetフレームのデータ部を NDIS に渡す。
 *
 *  本モジュールの説明
 *
 *     ste.c
 *
 *    本モジュールは NDIS から呼び出されるのエントリポイント（SteMiniportXXX）が記述さ
 *    れており、DriverEntry() にて、これらのエントリポイントの登録を行う。
 *
 *  変更履歴:
 *    2005/07/18
 *      o Opackets をカウントするように変更した。
 *     
 ************************************************************************************/
#include "ste.h"

NDIS_HANDLE      NdisWrapperHandle;     // DireverEntry() の中で使われる。触ってはいけない。
NDIS_SPIN_LOCK   SteGlobalSpinLock;     // ドライバのグローバルロック
STE_ADAPTER     *SteAdapterHead = NULL; // STE_ADAPTER のリンクリストのヘッド

// サポートが必須(M)の項目のみ抽出(全 34 OID)
NDIS_OID STESupportedList[] = {
    //
    // 一般的な特性 (22 OID)
    //
    OID_GEN_SUPPORTED_LIST,        // サポートされる OID のリスト
    OID_GEN_HARDWARE_STATUS,       // ハードウェアステータス
    OID_GEN_MEDIA_SUPPORTED,       // NIC がサポートできる（が必須ではない）メディアタイプ
    OID_GEN_MEDIA_IN_USE,          // NIC が現在使っている完全なメディアタイプのリスト
    OID_GEN_MAXIMUM_LOOKAHEAD,     // NIC が lookahead データとして提供できる最大バイト数
    OID_GEN_MAXIMUM_FRAME_SIZE,    // NIC がサポートする、ヘッダを抜いたネットワークパケットサイズ
    OID_GEN_LINK_SPEED,            // NIC がサポートする最大スピード
    OID_GEN_TRANSMIT_BUFFER_SPACE, // NIC 上の送信用のメモリの総量
    OID_GEN_RECEIVE_BUFFER_SPACE,  // NIC 上の受信用のメモリの総量
    OID_GEN_TRANSMIT_BLOCK_SIZE,   // NIC がサポートする送信用のネットワークパケットサイズ
    OID_GEN_RECEIVE_BLOCK_SIZE,    // NIC がサポートする受信用のネットワークパケットサイズ
    OID_GEN_VENDOR_ID,             // IEEE に登録してあるベンダーコード（登録されてない場合 0xFFFFFF）
    OID_GEN_VENDOR_DESCRIPTION,    // NIC のベンダー名
    OID_GEN_VENDOR_DRIVER_VERSION, // ドライバーのバージョン
    OID_GEN_CURRENT_PACKET_FILTER, // プロトコルが NIC から受け取るパケットのタイプ
    OID_GEN_CURRENT_LOOKAHEAD,     // 現在の lookahead のバイト数
    OID_GEN_DRIVER_VERSION,        // NDIS のバージョン
    OID_GEN_MAXIMUM_TOTAL_SIZE,    // NIC がサポートするネットワークパケットサイズ
    OID_GEN_PROTOCOL_OPTIONS,      // オプションのプロトコルフラグ
    OID_GEN_MAC_OPTIONS,           // 追加の NIC のプロパティを定義したビットマスク
    OID_GEN_MEDIA_CONNECT_STATUS,  // NIC 上の connection 状態
    OID_GEN_MAXIMUM_SEND_PACKETS,  // 一回のリクエストで受けられるパケットの最大数
    //
    // 一般的な統計情報 (5 OID)
    //
    OID_GEN_XMIT_OK,               // 正常に送信できたフレーム数
    OID_GEN_RCV_OK,                // 正常に受信できたフレーム数
    OID_GEN_XMIT_ERROR,            // 送信できなかった（もしくはエラーになった）フレーム数
    OID_GEN_RCV_ERROR,             // 受信できなかった（もしくはエラーになった）フレーム数
    OID_GEN_RCV_NO_BUFFER,         // バッファ不足のために受信できなかったフレーム数
    //
    // Ethernet 用の特性 (4 OID)
    //
    OID_802_3_PERMANENT_ADDRESS,   // ハードウェアに書かれている MAC アドレス
    OID_802_3_CURRENT_ADDRESS,     // NIC の現在の MAC アドレス
    OID_802_3_MULTICAST_LIST,      // 現在のマルチキャストパケットのアドレスリスト
    OID_802_3_MAXIMUM_LIST_SIZE,   // NIC ドライバが管理できる最大のマルチキャストアドレスの数
    //
    // Ethernet 用統計情報 (3 OID)
    //
    OID_802_3_RCV_ERROR_ALIGNMENT,   // アライメントエラーの受信フレーム数
    OID_802_3_XMIT_ONE_COLLISION,    // コリジョンが 1 回発生した送信フレーム数
    OID_802_3_XMIT_MORE_COLLISIONS   // コリジョンが 1 回以上発生した送信フレーム数
};

/*****************************************************************************
 * DriverEntry()
 *
 *   この関数のは System がこのドライバをロードするときに呼ばれ、ドライバを
 *   NDIS と関連付け、エントリポイントを登録する。
 * 
 * 引数:
 *     DriverObject :  ドライバーオブジェクトのポインタ
 *     RegistryPath :  ドライバーのレジストリのパス 関連付け
 *  
 * 返り値:
 * 
 *     NDIS_STATUS 
 * 
 ********************************************************************************/
NDIS_STATUS
DriverEntry(
    IN PVOID DriverObject,
    IN PVOID RegistryPath)
{
    NDIS_MINIPORT_CHARACTERISTICS  MiniportCharacteristics;
    NDIS_STATUS                    Status;

    DEBUG_PRINT0(3, "DriverEntry called\n");        

    NdisZeroMemory(&MiniportCharacteristics, sizeof(NDIS_MINIPORT_CHARACTERISTICS));

    /*
     * ミニポートドライバを NDIS に関連付けし、NdisWrapperHandle を得る。
     */
    NdisMInitializeWrapper(
        &NdisWrapperHandle, // OUT PNDIS_HANDLE  
        DriverObject,       // IN ドライバーオブジェクト
        RegistryPath,       // IN レジストリパス
        NULL                // IN 必ず NULL
        );

    if(NdisWrapperHandle == NULL){
        DEBUG_PRINT0(1, "NdisInitializeWrapper failed\n");
        return(STATUS_INVALID_HANDLE);
    }

    MiniportCharacteristics.MajorNdisVersion         = STE_NDIS_MAJOR_VERSION; // Major Version 
    MiniportCharacteristics.MinorNdisVersion         = STE_NDIS_MINOR_VERSION; // Minor Version 
    MiniportCharacteristics.CheckForHangHandler      = SteMiniportCheckForHang;     
    MiniportCharacteristics.HaltHandler              = SteMiniportHalt;             
    MiniportCharacteristics.InitializeHandler        = SteMiniportInitialize;       
    MiniportCharacteristics.QueryInformationHandler  = SteMiniportQueryInformation; 
    MiniportCharacteristics.ResetHandler             = SteMiniportReset ;            
    MiniportCharacteristics.SetInformationHandler    = SteMiniportSetInformation;   
    MiniportCharacteristics.ReturnPacketHandler      = SteMiniportReturnPacket;    
    MiniportCharacteristics.SendPacketsHandler       = SteMiniportSendPackets; 

    Status = NdisMRegisterMiniport(     
        NdisWrapperHandle,                    // IN NDIS_HANDLE                     
        &MiniportCharacteristics,             // IN PNDIS_MINIPORT_CHARACTERISTICS  
        sizeof(NDIS_MINIPORT_CHARACTERISTICS) // IN UINT                            
        );

    if( Status != NDIS_STATUS_SUCCESS){
        DEBUG_PRINT1(1, "NdisMRegisterMiniport failed(Status = 0x%x)\n", Status);
        NdisTerminateWrapper(
            NdisWrapperHandle, // IN NDIS_HANDLE  
            NULL               
            );        
        return(Status);
    }

    // グローバルロックを初期化
    NdisAllocateSpinLock(&SteGlobalSpinLock);

    NdisMRegisterUnloadHandler(NdisWrapperHandle, SteMiniportUnload);    
    
    return(NDIS_STATUS_SUCCESS);
}

/*************************************************************************
 * SteMiniportinitialize()
 *
 * 　NDIS エントリポイント
 *   ネットワーク I/O 操作のために NIC ドライバがネットワーク I/O 操作を
 *   するために必要なリソースを確保する。
 *
 *  引数:
 *
 *         OpenErrorStatus              OUT PNDIS_STATUS 
 *         SelectedMediumIndex          OUT PUINT        
 *         MediumArray                  IN PNDIS_MEDIUM  
 *         MediumArraySize              IN UINT          
 *         MiniportAdapterHandle        IN NDIS_HANDLE   
 *         WrapperConfigurationContext  IN NDIS_HANDLE   
 *
 *  返り値:
 *    
 *     正常時: NDIS_STATUS_SUCCESS
 *
 *************************************************************************/
NDIS_STATUS 
SteMiniportInitialize(
    OUT PNDIS_STATUS OpenErrorStatus,
    OUT PUINT        SelectedMediumIndex,
    IN PNDIS_MEDIUM  MediumArray,
    IN UINT          MediumArraySize,
    IN NDIS_HANDLE   MiniportAdapterHandle,
    IN NDIS_HANDLE   WrapperConfigurationContext
    )
{
    UINT             i;
    NDIS_STATUS     Status  = NDIS_STATUS_SUCCESS;
    STE_ADAPTER    *Adapter = NULL;
    BOOLEAN          MediaFound = FALSE;

    DEBUG_PRINT0(3, "SteMiniportInitialize called\n");    

    *SelectedMediumIndex = 0;
    
    for ( i = 0 ; i < MediumArraySize ; i++){
        if (MediumArray[i] == NdisMedium802_3){
            *SelectedMediumIndex = i;
            MediaFound = TRUE;
            break;
        }
    }

    // 途中で break するためだけの Do-While 文
    do {
        if(!MediaFound){
            // 上記の for 文で見つけられなかったようだ
            DEBUG_PRINT0(1, "SteMiniportInitialize: No Media much\n");        
            Status = NDIS_STATUS_UNSUPPORTED_MEDIA;
            break;
        }

        //
        // Adapter を確保し、初期化する
        //
        if ((Status = SteCreateAdapter(&Adapter)) != NDIS_STATUS_SUCCESS){
            DEBUG_PRINT0(1, "SteMiniportInitialize: Can't allocate memory for STE_ADAPTER\n");
            Status = NDIS_STATUS_RESOURCES;
            break;
        }

        DEBUG_PRINT1(3, "SteMiniportInitialize: Adapter = 0x%p\n", Adapter);        

        Adapter->MiniportAdapterHandle = MiniportAdapterHandle;

        //
        // Registory を読む処理。...省略。
        //    NdisOpenConfiguration();
        //    NdisReadConfiguration();
        //
        // NIC のためのハードウェアリソースのリストを得る。...省略。
        //    NdisMQueryAdapterResources()
        //

        //
        // NDIS に NIC の情報を伝える。
        // かならず NdisXxx 関数を呼び出すより前に、以下の NdisMSetAttributesEx
        // を呼び出さなければならない。
        //
        NdisMSetAttributesEx(
            MiniportAdapterHandle,       //IN NDIS_HANDLE 
            (NDIS_HANDLE) Adapter,       //IN NDIS_HANDLE 
            0,                           //IN UINT  
            NDIS_ATTRIBUTE_DESERIALIZE,  //IN ULONG  Deserialized ミニポートドライバ
            NdisInterfaceInternal        //IN NDIS_INTERFACE_TYPE 
            );

        //
        // NDIS 5.0 の場合はかならず SHUTDOWN_HANDLER を登録しなければならない。
        //
        NdisMRegisterAdapterShutdownHandler(
            MiniportAdapterHandle,                         // IN NDIS_HANDLE
            (PVOID) Adapter,                               // IN PVOID 
            (ADAPTER_SHUTDOWN_HANDLER) SteMiniportShutdown // IN ADAPTER_SHUTDOWN_HANDLER  
            );

        //
        // 仮想 NIC デーモンからの IOCT/ReadFile/WriteFile 用の
        // デバイスを作成し、Dispatch　ルーチンを登録する。
        //
        SteRegisterDevice(Adapter);
    
        //
        // SteRecvTimerFunc() を呼ぶためのタイマーオブジェクトを初期化
        //

        NdisInitializeTimer(
            &Adapter->RecvTimer,     //IN OUT PNDIS_TIMER  
            SteRecvTimerFunc,        //IN PNDIS_TIMER_FUNCTION      
            (PVOID)Adapter           //IN PVOID
            );
        //
        // SteResetTimerFunc() を呼ぶためのタイマーオブジェクトを初期化
        //
        NdisInitializeTimer(
            &Adapter->ResetTimer,    //IN OUT PNDIS_TIMER  
            SteResetTimerFunc,       //IN PNDIS_TIMER_FUNCTION      
            (PVOID)Adapter           //IN PVOID
            );
        
    } while (FALSE);
    
    
    return(Status);
}

/****************************************************************************
 * SteMiniportShutdown()
 *
 *   NDIS エントリポイント
 *   システムのシャットダウンや、予期せぬシステムエラー時に、NIC を初期状態
 *   に戻すために呼ばれる。ここではメモリリソースを解放したり、パケットの転
 *   送完了を待ったりはしない
 *
 * 引数:
 *
 *    ShutdownContext : STE_ADAPTER 構造体のポインタ
 *
 * 戻り値:
 *
 *    無し
 *
***************************************************************************/
VOID
SteMiniportShutdown(
    IN PVOID  ShutdownContext
    )
{
    STE_ADAPTER     *Adapter;

    DEBUG_PRINT0(3, "SteMiniportShutdown called\n");        

    Adapter = (STE_ADAPTER *)ShutdownContext;

    return;
}

/************************************************************************
 * SteMiniportQueryInformation()
 *
 *  NDIS エントリポイント
 *  OID の値を問い合わせるために NDIS によって呼ばれる。
 * 
 * 引数:
 * 
 *     MiniportAdapterContext  :   STE_ADAPTER 構造体のポインタ
 *     Oid                     :   この問い合わせの OID
 *     InformationBuffer       :   情報のためのバッファー
 *     InformationBufferLength :   バッファのサイズ
 *     BytesWritten            :   いくつの情報が記述されたか
 *     BytesNeeded             :   バッファが少ない場合に必要なサイズを指示
 * 
 * 返り値:
 *
 *     正常時 :  NDIS_STATUS_SUCCESS
 *     
 ************************************************************************/
NDIS_STATUS
SteMiniportQueryInformation(
    IN NDIS_HANDLE  MiniportAdapterContext,
    IN NDIS_OID     Oid,
    IN PVOID        InformationBuffer,
    IN ULONG        InformationBufferLength,
    OUT PULONG      BytesWritten,
    OUT PULONG      BytesNeeded
    )
{
    NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;
    STE_ADAPTER    *Adapter;
    PVOID           Information = NULL;     // 提供する情報へのポインタ
    ULONG           InformationLength = 0;  // 提供する情報の長さ
    ULONG           ulTemp;                 // 整数値の情報のための領域（マクロ内で利用）
    CHAR            VendorName[] = STE_VENDOR_NAME; // ベンダー名

    DEBUG_PRINT0(3, "SteMiniportQueryInformation called\n");    

    Adapter = (STE_ADAPTER *)MiniportAdapterContext;

    DEBUG_PRINT1(3, "SteMiniportQueryInformation: Oid = 0x%x\n", Oid);        

    switch(Oid) {    
        // 一般的な特性 （22個)
        case OID_GEN_SUPPORTED_LIST:        //サポートされる OID のリスト
            SET_INFORMATION_BY_POINTER(sizeof(STESupportedList), &STESupportedList);
            
        case OID_GEN_HARDWARE_STATUS:       // ハードウェアステータス
            SET_INFORMATION_BY_VALUE(sizeof(NDIS_HARDWARE_STATUS), NdisHardwareStatusReady);
            
        case OID_GEN_MEDIA_SUPPORTED:       // NIC がサポートできる（が必須ではない）メディアタイプ
        case OID_GEN_MEDIA_IN_USE:          // NIC が現在使っている完全なメディアタイプのリスト
            SET_INFORMATION_BY_VALUE(sizeof(NDIS_MEDIUM), NdisMedium802_3);
            
        case OID_GEN_MAXIMUM_LOOKAHEAD:     // NIC が lookahead データとして提供できる最大バイト数
        case OID_GEN_MAXIMUM_FRAME_SIZE:    // NIC がサポートする、ヘッダを抜いたネットワークパケットサイズ
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), ETHERMTU);
            
        case OID_GEN_LINK_SPEED:            //NIC がサポートする最大スピード
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), ETHERLINKSPEED);
            
        case OID_GEN_TRANSMIT_BUFFER_SPACE: // NIC 上の送信用のメモリの総量
            // TODO: これでいいのか？
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), ETHERMTU);
            
        case OID_GEN_RECEIVE_BUFFER_SPACE:  // NIC 上の受信用のメモリの総量
            // TODO: これでいいのか？
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), ETHERMTU);            
            
        case OID_GEN_TRANSMIT_BLOCK_SIZE:   // NIC がサポートする送信用のネットワークパケットサイズ
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), ETHERMAX);                        

        case OID_GEN_RECEIVE_BLOCK_SIZE:    // NIC がサポートする受信用のネットワークパケットサイズ
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), ETHERMAX);                                    

        case OID_GEN_VENDOR_ID:             // IEEE に登録してあるベンダーコード
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), 0xFFFFFF);            
            
        case OID_GEN_VENDOR_DESCRIPTION:    // NIC のベンダー名
            SET_INFORMATION_BY_POINTER(sizeof(VendorName), VendorName);
            
        case OID_GEN_VENDOR_DRIVER_VERSION: // ドライバーのバージョン
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), STE_DRIVER_VERSION);                        

        case OID_GEN_CURRENT_PACKET_FILTER: // プロトコルが NIC から受け取るパケットのタイプ
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), Adapter->PacketFilter);
            
        case OID_GEN_CURRENT_LOOKAHEAD:     // 現在の lookahead のバイト数
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), ETHERMTU);
            
        case OID_GEN_DRIVER_VERSION:        // NDIS のバージョン
            SET_INFORMATION_BY_VALUE(sizeof(USHORT), STE_NDIS_VERSION);
            
        case OID_GEN_MAXIMUM_TOTAL_SIZE:    // NIC がサポートするネットワークパケットサイズ
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), ETHERMAX);
            
       // case OID_GEN_PROTOCOL_OPTIONS:      // オプションのプロトコルフラグ。Set のみ必須
                    
        case OID_GEN_MAC_OPTIONS:           // 追加の NIC のプロパティを定義したビットマスク
            SET_INFORMATION_BY_VALUE(sizeof(ULONG),
                                     NDIS_MAC_OPTION_NO_LOOPBACK |
                                     NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                                     NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA);

        case OID_GEN_MEDIA_CONNECT_STATUS:  // NIC 上の connection 状態
            // TODO: 状態を確認し、NdisMediaStateDisconnected を返すようにする
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), NdisMediaStateConnected);
            
        case OID_GEN_MAXIMUM_SEND_PACKETS:  // 一回のリクエストで受けられるパケットの最大数
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), STE_MAX_SEND_PACKETS);

        // 一般的な統計情報 (5個)
        case OID_GEN_XMIT_OK:               // 正常に送信できたフレーム数
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), Adapter->Opackets);

        case OID_GEN_RCV_OK:                // 正常に受信できたフレーム数
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), Adapter->Ipackets);            

        case OID_GEN_XMIT_ERROR:            // 送信できなかった（もしくはエラーになった）フレーム数
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), Adapter->Oerrors);
            
        case OID_GEN_RCV_ERROR:             // 受信できなかった（もしくはエラーになった）フレーム数
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), Adapter->Ierrors);
            
        case OID_GEN_RCV_NO_BUFFER:         // バッファ不足のために受信できなかったフレーム数
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), Adapter->NoResources);            
            
       // Ethernet 用の特性 (4個)
        case OID_802_3_PERMANENT_ADDRESS:   // ハードウェアに書かれている MAC アドレス
        case OID_802_3_CURRENT_ADDRESS:     // NIC の現在の MAC アドレス            
            SET_INFORMATION_BY_POINTER(ETHERADDRL, Adapter->EthernetAddress);
            
        case OID_802_3_MULTICAST_LIST:     // 現在のマルチキャストパケットのアドレスリスト
            // TODO: マルチキャストリストをセットする。
            // 今のところ 0 を返す
            SET_INFORMATION_BY_VALUE(ETHERADDRL, 0);            
            
        case OID_802_3_MAXIMUM_LIST_SIZE:  // NIC ドライバが管理できる最大のマルチキャストアドレスの数
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), STE_MAX_MCAST_LIST);
            
        // Ethernet 用統計情報  (3個)
        case OID_802_3_RCV_ERROR_ALIGNMENT:   // アライメントエラーの受信フレーム数
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), Adapter->AlignErrors);

        case OID_802_3_XMIT_ONE_COLLISION:    // コリジョンが 1 回発生した送信フレーム数
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), Adapter->OneCollisions);

        case OID_802_3_XMIT_MORE_COLLISIONS:  // コリジョンが 1 回以上発生した送信フレーム数
            SET_INFORMATION_BY_VALUE(sizeof(ULONG), Adapter->Collisions);            

        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;                        
    }

    if(Information != NULL) {
        NdisMoveMemory(InformationBuffer, Information, InformationLength);        
        *BytesWritten = InformationLength;
    } else if(InformationLength > 0) {
        // バッファが小さい場合は、必要なサイズを通知する。
        *BytesNeeded = InformationLength;
        Status = NDIS_STATUS_BUFFER_TOO_SHORT;
    }
    return(Status);    
}

/***************************************************************************
 * SteMiniportSetInformation()
 *
 *  NDIS エントリポイント
 *  OID の値を問い合わせるために NDIS によって呼ばれる。
 * 
 * 引数:
 * 
 *     MiniportAdapterContext  :    Adapter 構造体のポインタ
 *     Oid                     :    この問い合わせの OID
 *     InformationBuffer       :    情報のためのバッファー
 *     InformationBufferLength :    バッファのサイズ
 *     BytesRead               :    いくつの情報が読まれたか
 *     BytesNeeded             :    バッファが少ない場合に必要なサイズを指示
 * 
 * 返り値:
 * 
 *     正常時 : NDIS_STATUS_SUCCESS
 * 
 *******************************************************************************/
NDIS_STATUS
SteMiniportSetInformation(
    IN NDIS_HANDLE  MiniportAdapterContext,
    IN NDIS_OID     Oid,
    IN PVOID        InformationBuffer,
    IN ULONG        InformationBufferLength,
    OUT PULONG      BytesRead,
    OUT PULONG      BytesNeeded)
{
    NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;
    STE_ADAPTER    *Adapter;
    ULONG           NewFilter;

    DEBUG_PRINT0(3, "SteMiniportSetInformation called\n");        

    Adapter   =    (STE_ADAPTER *)MiniportAdapterContext;

    DEBUG_PRINT1(3, "SteMiniportSetInformation: Oid = 0x%x\n", Oid);
    
    // 必須 OID のセット要求だけを実装
    // TODO:
    // 今のところ、パケットフィルター以外は実際にはセットしていないので、セットできるようにする。
    //
    switch(Oid) {
        // 一般的な特性         
        case OID_GEN_CURRENT_PACKET_FILTER:
            if(InformationBufferLength != sizeof(ULONG)){
                *BytesNeeded = sizeof(ULONG);
                *BytesRead = 0;
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }
            NewFilter = *(ULONG *)InformationBuffer;
            Adapter->PacketFilter = NewFilter;
            *BytesRead = InformationBufferLength;
            DEBUG_PRINT1(3,"SteMiniportSetInformation: New filter = 0x%x (See ntddndis.h)\n", NewFilter);
            break;
        case OID_GEN_CURRENT_LOOKAHEAD:
            *BytesRead = InformationBufferLength;
            break;
        case OID_GEN_PROTOCOL_OPTIONS:
            *BytesRead = InformationBufferLength;
            break;        
        // Ethernet の特性
        case OID_802_3_MULTICAST_LIST:
            *BytesRead = InformationBufferLength;
            break;            
        default:
            Status = NDIS_STATUS_INVALID_OID;            
            break;
    }
    
    return(Status);
}


/************************************************************************
 * SteMiniportHalt()
 *
 *     NDIS Miniport エントリポイント
 *     Halt ハンドラは NDIS が PNP マネージャから IRP_MN_STOP_DEVICE、
 *     IRP_MN_SUPRISE_REMOVE、IRP_MN_REMOVE_DEVICE 要求を受け取ったと
 *     きに呼ばれる。SteMiniportInitialize で確保された全てのリソース
 *     を解放する。（特定のミニポートドライバインスタンスに限定される)
 *     
 *     o 全ての I/O リソースを free し、unmap する。
 *     o NdisMRegisterAdapterShutdownHandler によって登録されたシャッ
 *       トダウンハンドラを登録解除する。
 *     o NdisMCancelTimer を呼んでキューイングされているコールバック
 *       ルーチンをキャンセルする。
 *     o 全ての未処理の受信パケットが処理され終わるまで待つ。
 * 
 * 引数:
 *      MiniportAdapterContext	アダプタへのポインタ
 *  
 * 返り値:
 * 
 *     無し
********************************************************************/
VOID 
SteMiniportHalt(
    IN  NDIS_HANDLE    MiniportAdapterContext
    )
{
    STE_ADAPTER       *Adapter;
    BOOLEAN            bTimerCancelled;
    INT                i;

    DEBUG_PRINT0(3, "SteMiniportHalt called\n");        

    Adapter = (STE_ADAPTER *) MiniportAdapterContext;
    
    SteMiniportShutdown(
        (PVOID) Adapter   //IN PVOID
        );

    //
    // NdisMCancelTimer を呼んでキューイングされているコールバック
    // ルーチンをキャンセルする。
    //

    // ReceiveIndication タイマーをキャンセル
    NdisCancelTimer(
        &Adapter->RecvTimer,  // IN  PNDIS_TIMER
        &bTimerCancelled      // OUT PBOOLEAN  
        );
    // Reset タイマーをキャンセル    
    NdisCancelTimer(
        &Adapter->ResetTimer, // IN  PNDIS_TIMER
        &bTimerCancelled      // OUT PBOOLEAN  
        );
    if (bTimerCancelled == TRUE){
        // キャンセルされたコールバックルーチンがあったようだ。
        // 受信キューに残っている Packet はこの後の SteDeleteAdapter()
        // によって Free されるので、ここでは何もしない。
    }

    // NdisMRegisterAdapterShutdownHandler によって登録された
    // シャットダウンハンドラを登録解除する。    
    NdisMDeregisterAdapterShutdownHandler(
        Adapter->MiniportAdapterHandle // IN NDIS_HANDLE 
        );

    //
    // 仮想 NIC デーモンからの IOCT/READ/WRITE 用のデバイスの登録を解除する。
    //
    SteDeregisterDevice(Adapter);

    //
    // 処理中の受信通知済みパケットがないかどうかチェックする。
    // 1 秒おきに、STE_MAX_WAIT_FOR_RECVINDICATE(=5)回確認し、
    // RecvIndicatedPackets が 0 にならないようであれば、
    // なにか問題があったと考え無視してリソースの開放に進む。
    //
    for ( i = 0 ; i < STE_MAX_WAIT_FOR_RECVINDICATE ; i++){
        if (Adapter->RecvIndicatedPackets == 0) {
            break;
        }
        NdisMSleep(1000);
    }
    
    //
    // Adapter を削除する。このなかで、Adapter の為に確保されたリソースの
    // 開放も（Packet や、Buffer）行われる。
    //
    SteDeleteAdapter(Adapter);

    return;
}

/******************************************************************************
 * SteMiniportReset()
 *
 *    NDIS Miniport エントリポイント
 *    以下の条件のとき NIC がハングしていると判断しドライバーのソフトステート
 *    をリセットするために呼ばれる。
 *
 *     1) SteMiniportCheckForHang() が TRUE を返してきた
 *     2) 未処理の送信パケットを検出した（シリアライズドドライバのみ）
 *     3) 一定時間内に完了することができなかった未処理の要求があった
 *
 *   この関数の中では以下を行う
 *   
 *     1) キューイングされている送信パケットの処理をやめ、NDIS_STATUS_REQUEST_ABORTED を返す。
 *     2) 上位に通知した全てのパケットがリターンしてきたか確認する。
 *     3) 上記２つに問題がありリセット処理をただちに完了できない場合、ResetTimer をセットし、
 *        NDIS_STATUS_PENDING を返す。

 * 
 * 引数:
 * 
 *    AddressingReset        : マルチキャストや、MAC アドレス、lookahead サイズ
 *                             に変更があった場合にはここを TRUE にしなければならない。
 *                             
 *    MiniportAdapterContext : STE_ADAPTER 構造体のポインタ                  
 * 
 * 
 *   返り値:
 * 
 *     NDIS_STATUS
 * 
 **************************************************************************/
NDIS_STATUS
SteMiniportReset(
    OUT PBOOLEAN     AddressingReset,
    IN  NDIS_HANDLE  MiniportAdapterContext
    )
{
    NDIS_STATUS        Status;
    STE_ADAPTER       *Adapter;
    NDIS_PACKET       *Packet = NULL;

    DEBUG_PRINT0(3, "SteMiniportReset called\n");

    Adapter = (STE_ADAPTER *)MiniportAdapterContext;

    // MAC アドレス、マルチキャストアドレスの変更はない！？ときめつけて FALSE を返す
    *AddressingReset = FALSE;    

    Status = NDIS_STATUS_REQUEST_ABORTED;

    // 送信キューに入っているパケットを処理する
    while (SteGetQueue(&Adapter->SendQueue, &Packet) == NDIS_STATUS_SUCCESS) {
        NdisMSendComplete(
            Adapter->MiniportAdapterHandle,  //IN NDIS_HANDLE   
            Packet,                          //IN PNDIS_PACKET  
            Status                           //IN NDIS_STATUS   
            );
    }

    // 受信キューに入っているパケットを処理する
    while (SteGetQueue(&Adapter->RecvQueue, &Packet) == NDIS_STATUS_SUCCESS) {
        SteFreeRecvPacket(Adapter, Packet);
    }
    // これが終われば、全ての受信パケットがフリーされているはず。
    // 後は上位プロトコルに通知済みでまだ戻ってきていないパケットがあるかどうかを
    // 確認する。

    if (Adapter->RecvIndicatedPackets > 0) {
        // まだ戻ってきてないパケットがあるようだ。
        // リセットタイマーをセットして、後ほど SteResetTimerFunc() から
        // NdisMResetComplete() を呼んでリセットを完了することにする。
        NdisSetTimer(&Adapter->ResetTimer, 300);
        Status = NDIS_STATUS_PENDING;
    } else {
        // リセット操作完了！
        Status = NDIS_STATUS_SUCCESS;
    }

    return(Status);
}
 
/***************************************************************************
 * SteMiniportUnload()
 *
 *     NDIS Miniport エントリポイント
 *     アンドロードハンドラは DriverEntry の中で獲得されたリソースを解放
 *     するために、ドライバのアンロード中に呼ばれる。
 *     このハンドラは NdisMRegisterUnloadHandler を通して登録される。
 *
 *     ** 注意**
 *     MiniportUnload() は MiniportHalt() とは違う！！
 *     MiniportUload() はより広域のスコープを持つのに対し、MiniportHalt は
 *     特定の miniport ドライバインスタンスに限定される。
 * 
 * 引数:
 * 
 *     DriverObject  : 使ってない
 * 
 * 返り値:
 * 
 *     None
 *
 *************************************************************************/
VOID 
SteMiniportUnload(
    IN  PDRIVER_OBJECT      DriverObject
    )
{
    // DriverEntry() でリソースを確保していないので
    // 何もしなくてよいものか？
    DEBUG_PRINT0(3, "SteMiniportUnload called\n");            

    return;
}

/*******************************************************************************
 * SteMiniportCheckForHang()
 *
 *     NDIS Miniport エントリポイント
 *     NIC の状態を報告するためによばれ、またデバイスドライバの反応の有無を
 *     モニターするために呼ばれる。
 *     
 * 引数:
 * 
 *     MiniportAdapterContext :  アダプタのポインタ
 * 
 * 返り値:
 * 
 *     TRUE    NDIS がドライバの MiniportReset を呼び出した
 *     FALSE   正常
 * 
 * Note: 
 *     CheckForHang ハンドラはタイマー DPC のコンテキストで呼び出されます。
 *     この利点は、spinlock を確保/解放するときに得られます。
 * 
 ******************************************************************************/
BOOLEAN
SteMiniportCheckForHang(
    IN NDIS_HANDLE MiniportAdapterContext
    )
{
    // あまりに冗長なので、必要なデバッグレベルを上げる
    DEBUG_PRINT0(4, "SteMiniportCheckForHang called\n");
    
    return(FALSE);
}

/**************************************************************************
 * SteMiniportReturnPacket()   
 *
 *    NDIS Miniport エントリポイント
 *    このドライバが上位プロトコルに通知したパケットが、プロトコルによって
 *    処理が終了した場合に NDIS によって呼ばれる。
 *
 * 引数:
 *
 *    MiniportAdapterContext :  ADAPTER 構造体のポインタ
 *    Packet                 :  リターンされてきたパケット
 *
 * 返り値:
 *
 *    無し
 *
 **************************************************************************/
VOID 
SteMiniportReturnPacket(
    IN NDIS_HANDLE   MiniportAdapterContext,
    IN PNDIS_PACKET  Packet)
{
    STE_ADAPTER         *Adapter;

    Adapter = (STE_ADAPTER *)MiniportAdapterContext;

    DEBUG_PRINT0(3, "SteMiniportReturnPacket called\n");    

    SteFreeRecvPacket(
        Adapter,  //IN STE_ADAPTER
        Packet    //IN NDIS_PACKET
        );

    NdisInterlockedDecrement(
        (PLONG)&Adapter->RecvIndicatedPackets  //IN PLONG  
    );    
    
    return;
}

/***************************************************************************
 * SteMiniportSendPackets()
 *
 *    NDIS Miniport エントリポイント
 *    このドライバにバインドしているプロトコルが、パケットを送信する際に
 *    NDIS によって呼ばれる。
 *
 * 引数:
 *
 *    MiniportAdapterContext :   アダプタコンテキストへのポインタ
 *    PacketArray            :   送信するパケット配列
 *    NumberOfPackets        :   上記の配列の長さ
 *
 * 返り値:
 *
 *    無し
 *    
 *************************************************************************/
VOID 
SteMiniportSendPackets(
    IN NDIS_HANDLE       MiniportAdapterContext,
    IN PPNDIS_PACKET     PacketArray,
    IN UINT              NumberOfPackets
    )
{
    UINT            i;
    NDIS_STATUS     Status;
    STE_ADAPTER    *Adapter;
    BOOLEAN         IsStedRegistered = TRUE;

    DEBUG_PRINT0(3, "SteMiniportSendPackets called\n");            

    Adapter = (STE_ADAPTER *) MiniportAdapterContext;

    // 仮想 NIC デーモンからのイベントオブジェクトが登録されているか
    // TODO: Adapter 毎の Lock を作成し、EventObject の参照時に
    // ロックを取得すべき
    if( Adapter->EventObject == NULL ) {
        DEBUG_PRINT0(2, "SteMiniportSendPackets: sted is not registerd yet\n");
        IsStedRegistered = FALSE;
    }        

    for( i = 0 ; i < NumberOfPackets ; i++){

        if(DebugLevel >= 3)
            StePrintPacket(PacketArray[i]);
        
        if(IsStedRegistered){
            Status = StePutQueue(&Adapter->SendQueue, PacketArray[i]);
        } else {
            Status = NDIS_STATUS_RESOURCES;
        }
        
        if(Status != NDIS_STATUS_SUCCESS){            
            // 送信キューが一杯、もしくは仮想 NIC デーモンが未登録のようだ。
            NdisMSendComplete(
                Adapter->MiniportAdapterHandle,  //IN NDIS_HANDLE   
                PacketArray[i],                  //IN PNDIS_PACKET  
                Status                           //IN NDIS_STATUS
                // NDIS_STATUS_RESOURCES を返すことになる。
                );
            Adapter->Oerrors++;
            continue;
        }
        
        Adapter->Opackets++;        

        /*
         * 仮想 NIC デーモンにイベント通知
         */
        KeSetEvent(Adapter->EventObject, 0, FALSE);
        DEBUG_PRINT0(3, "SteMiniportSendPackets: KeSetEvent Called\n");
    }
    return;
}
