#include "arp.h"          // 引入 ARP 相關的定義和結構
#include "netdev.h"       // 引入網路設備的定義
#include "skbuff.h"       // 引入 socket 緩衝區 (sk_buff) 的定義
#include "list.h"         // 引入鏈表的定義

/*
 * https://tools.ietf.org/html/rfc826
 * 這是 ARP (地址解析協議) 的 RFC 文檔，提供了 ARP 的標準規範
 */

static uint8_t broadcast_hw[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }; // 廣播 MAC 地址
static LIST_HEAD(arp_cache);  // 初始化 ARP 快取的鏈表頭
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; // 初始化互斥鎖以保護 ARP 快取的訪問

// 分配一個 sk_buff 並為 ARP 設置相關的頭部
static struct sk_buff *arp_alloc_skb()
{
    // 分配一個 socket 緩衝區，大小為以太網頭部 + ARP 頭部 + ARP 數據的總大小
    struct sk_buff *skb = alloc_skb(ETH_HDR_LEN + ARP_HDR_LEN + ARP_DATA_LEN);
    skb_reserve(skb, ETH_HDR_LEN + ARP_HDR_LEN + ARP_DATA_LEN); // 預留空間
    skb->protocol = htons(ETH_P_ARP); // 設置協議為 ARP

    return skb; // 返回分配的 sk_buff
}

// 分配一個新的 ARP 快取條目
static struct arp_cache_entry *arp_entry_alloc(struct arp_hdr *hdr, struct arp_ipv4 *data)
{
    struct arp_cache_entry *entry = malloc(sizeof(struct arp_cache_entry)); // 分配內存
    list_init(&entry->list); // 初始化鏈表節點

    // 設置條目的狀態和相關的硬件和 IP 地址
    entry->state = ARP_RESOLVED;
    entry->hwtype = hdr->hwtype;
    entry->sip = data->sip; // 源 IP 地址
    memcpy(entry->smac, data->smac, sizeof(entry->smac)); // 複製源 MAC 地址

    return entry; // 返回新分配的 ARP 快取條目
}

// 將 ARP 轉換表條目插入 ARP 快取
static int insert_arp_translation_table(struct arp_hdr *hdr, struct arp_ipv4 *data)
{
    struct arp_cache_entry *entry = arp_entry_alloc(hdr, data); // 分配新條目

    pthread_mutex_lock(&lock); // 鎖定互斥鎖以保護對 ARP 快取的訪問
    list_add_tail(&entry->list, &arp_cache); // 將條目添加到 ARP 快取
    pthread_mutex_unlock(&lock); // 解鎖

    return 0; // 返回成功
}

// 更新 ARP 轉換表中的條目
static int update_arp_translation_table(struct arp_hdr *hdr, struct arp_ipv4 *data)
{
    struct list_head *item;
    struct arp_cache_entry *entry;

    pthread_mutex_lock(&lock); // 鎖定互斥鎖
    list_for_each(item, &arp_cache) { // 遍歷 ARP 快取
        entry = list_entry(item, struct arp_cache_entry, list); // 獲取當前條目

        // 檢查硬件類型和源 IP 地址是否匹配
        if (entry->hwtype == hdr->hwtype && entry->sip == data->sip) {
            memcpy(entry->smac, data->smac, 6); // 更新源 MAC 地址
            pthread_mutex_unlock(&lock); // 解鎖
            
            return 1; // 返回成功
        }
    }

    pthread_mutex_unlock(&lock); // 解鎖
    return 0; // 返回失敗
}

// 初始化 ARP 模塊（目前為空）
void arp_init()
{

}

// 處理接收到的 ARP 數據包
void arp_rcv(struct sk_buff *skb)
{
    struct arp_hdr *arphdr; // ARP 頭部指針
    struct arp_ipv4 *arpdata; // ARP 數據指針
    struct netdev *netdev; // 網路設備指針
    int merge = 0; // 用於跟蹤是否合併

    arphdr = arp_hdr(skb); // 獲取 ARP 頭部

    // 將網路字節序轉換為主機字節序
    arphdr->hwtype = ntohs(arphdr->hwtype);
    arphdr->protype = ntohs(arphdr->protype);
    arphdr->opcode = ntohs(arphdr->opcode);
    arp_dbg("in", arphdr); // 記錄接收到的 ARP 數據

    // 檢查硬件類型
    if (arphdr->hwtype != ARP_ETHERNET) {
        printf("ARP: Unsupported HW type\n"); // 錯誤處理
        goto drop_pkt; // 丟棄數據包
    }

    // 檢查協議類型
    if (arphdr->protype != ARP_IPV4) {
        printf("ARP: Unsupported protocol\n"); // 錯誤處理
        goto drop_pkt; // 丟棄數據包
    }

    arpdata = (struct arp_ipv4 *) arphdr->data; // 獲取 ARP 數據

    // 將網路字節序轉換為主機字節序
    arpdata->sip = ntohl(arpdata->sip); // 源 IP 地址
    arpdata->dip = ntohl(arpdata->dip); // 目標 IP 地址
    arpdata_dbg("receive", arpdata); // 記錄接收到的 ARP 數據

    merge = update_arp_translation_table(arphdr, arpdata); // 更新 ARP 轉換表

    // 獲取與目標 IP 地址相關聯的網路設備
    if (!(netdev = netdev_get(arpdata->dip))) {
        printf("ARP was not for us\n"); // 錯誤處理
        goto drop_pkt; // 丟棄數據包
    }

    // 如果未合併且插入 ARP 轉換表失敗，報告錯誤
    if (!merge && insert_arp_translation_table(arphdr, arpdata) != 0) {
        print_err("ERR: No free space in ARP translation table\n"); // 錯誤處理
        goto drop_pkt; // 丟棄數據包
    }

    // 根據操作碼執行相應的動作
    switch (arphdr->opcode) {
    case ARP_REQUEST: // ARP 請求
        arp_reply(skb, netdev); // 回應 ARP 請求
        return;
    default: // 不支援的操作碼
        printf("ARP: Opcode not supported\n"); // 錯誤處理
        goto drop_pkt; // 丟棄數據包
    }

drop_pkt:
    free_skb(skb); // 釋放 sk_buff
    return; // 返回
}

// 發送 ARP 請求，查詢目標 IP 地址的 MAC 地址
int arp_request(uint32_t sip, uint32_t dip, struct netdev *netdev)
{
    struct sk_buff *skb; // socket 緩衝區指針
    struct arp_hdr *arp; // ARP 頭部指針
    struct arp_ipv4 *payload; // ARP 數據指針
    int rc = 0; // 返回碼

    skb = arp_alloc_skb(); // 分配 sk_buff 用於 ARP 數據

    if (!skb) return -1; // 如果分配失敗，返回錯誤

    skb->dev = netdev; // 設置 sk_buff 的網路設備

    payload = (struct arp_ipv4 *) skb_push(skb, ARP_DATA_LEN); // 將 ARP 數據推入 sk_buff

    // 填充源 MAC 和源 IP 地址
    memcpy(payload->smac, netdev->hwaddr, netdev->addr_len);
    payload->sip = sip;

    // 設置目標 MAC 為廣播地址，填充目標 IP 地址
    memcpy(payload->dmac, broadcast_hw, netdev->addr_len);
    payload->dip = dip;

    arp = (struct arp_hdr *) skb_push(skb, ARP_HDR_LEN); // 推入 ARP 頭部

    arp_dbg("req", arp); // 記錄 ARP 請求
    arp->opcode = htons(ARP_REQUEST); // 設置操作碼為 ARP 請求
    arp->hwtype = htons(ARP_ETHERNET); // 設置硬件類型為以太網
    arp->protype = htons(ETH_P_IP); // 設置協議類型為 IP
    arp->hwsize = netdev->addr_len; // 設置硬件地址長度
    arp->prosize = 4; // 設置協議地址長度為 4 字節（IPv4）

    arpdata_dbg("req", payload); // 記錄 ARP 數據
    payload->sip = htonl(payload->sip); // 將源 IP 轉換為網路字節序
    payload->dip = htonl(payload->dip); // 將目標 IP 轉換為網路字節序
    
    rc = netdev_transmit(skb, broadcast_hw, ETH_P_ARP); // 發送 ARP 請求
    free_skb(skb); // 釋放 sk_buff
    return rc; // 返回發送結果
}

// 發送 ARP 回覆
void arp_reply(struct sk_buff *skb, struct netdev *netdev) 
{
    struct arp_hdr *arphdr; // ARP 頭部指針
    struct arp_ipv4 *arpdata; // ARP 數據指針

    arphdr = arp_hdr(skb); // 獲取 ARP 頭部

    // 為 sk_buff 預留空間並推入 ARP 頭部和數據
    skb_reserve(skb, ETH_HDR_LEN + ARP_HDR_LEN + ARP_DATA_LEN);
    skb_push(skb, ARP_HDR_LEN + ARP_DATA_LEN);

    arpdata = (struct arp_ipv4 *) arphdr->data; // 獲取 ARP 數據

    // 準備回覆的 ARP 數據
    memcpy(arpdata->dmac, arpdata->smac, 6); // 目標 MAC 地址設置為源 MAC 地址
    arpdata->dip = arpdata->sip; // 目標 IP 地址設置為源 IP 地址

    memcpy(arpdata->smac, netdev->hwaddr, 6); // 源 MAC 地址設置為網路設備的 MAC 地址
    arpdata->sip = netdev->addr; // 源 IP 地址設置為網路設備的 IP 地址

    arphdr->opcode = ARP_REPLY; // 設置操作碼為 ARP 回覆

    arp_dbg("reply", arphdr); // 記錄 ARP 回覆
    arphdr->opcode = htons(arphdr->opcode); // 將操作碼轉換為網路字節序
    arphdr->hwtype = htons(arphdr->hwtype); // 將硬件類型轉換為網路字節序
    arphdr->protype = htons(arphdr->protype); // 將協議類型轉換為網路字節序

    arpdata_dbg("reply", arpdata); // 記錄 ARP 回覆數據
    arpdata->sip = htonl(arpdata->sip); // 將源 IP 地址轉換為網路字節序
    arpdata->dip = htonl(arpdata->dip); // 將目標 IP 地址轉換為網路字節序

    skb->dev = netdev; // 設置 sk_buff 的網路設備

    netdev_transmit(skb, arpdata->dmac, ETH_P_ARP); // 發送 ARP 回覆
    free_skb(skb); // 釋放 sk_buff
}

/*
 * 根據給定的源 IP 地址返回相應的硬件地址
 * 如果找不到，返回 NULL
 */
unsigned char* arp_get_hwaddr(uint32_t sip)
{
    struct list_head *item; // 用於遍歷的鏈表項目
    struct arp_cache_entry *entry; // ARP 快取條目指針
    
    pthread_mutex_lock(&lock); // 鎖定互斥鎖
    list_for_each(item, &arp_cache) { // 遍歷 ARP 快取
        entry = list_entry(item, struct arp_cache_entry, list); // 獲取當前條目

        // 檢查條目狀態和源 IP 地址是否匹配
        if (entry->state == ARP_RESOLVED && 
            entry->sip == sip) {
            arpcache_dbg("entry", entry); // 記錄找到的條目

            uint8_t *copy = entry->smac; // 獲取源 MAC 地址的指針
            pthread_mutex_unlock(&lock); // 解鎖

            return copy; // 返回找到的 MAC 地址
        }
    }

    pthread_mutex_unlock(&lock); // 解鎖
    return NULL; // 如果未找到，返回 NULL
}

// 釋放 ARP 快取中的所有條目
void free_arp()
{
    struct list_head *item, *tmp; // 用於遍歷的鏈表項目和臨時變量
    struct arp_cache_entry *entry; // ARP 快取條目指針

    list_for_each_safe(item, tmp, &arp_cache) { // 安全遍歷 ARP 快取
        entry = list_entry(item, struct arp_cache_entry, list); // 獲取當前條目
        list_del(item); // 從鏈表中刪除條目

        free(entry); // 釋放條目佔用的內存
    }
}
