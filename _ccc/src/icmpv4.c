#include "ethernet.h"
#include "icmpv4.h"
#include "ip.h"
#include "utils.h"

// 處理接收到的 ICMPv4 封包
void icmpv4_incoming(struct sk_buff *skb) 
{
    struct iphdr *iphdr = ip_hdr(skb); // 獲取 IP 頭部
    struct icmp_v4 *icmp = (struct icmp_v4 *) iphdr->data; // 獲取 ICMP 數據部分

    // TODO: 檢查 ICMP 校驗和

    switch (icmp->type) { // 根據 ICMP 類型進行處理
    case ICMP_V4_ECHO: // 如果是 ECHO 請求（Ping）
        icmpv4_reply(skb); // 回覆 ICMP 請求
        return;
    case ICMP_V4_DST_UNREACHABLE: // 如果是目標不可達的 ICMP 消息
        print_err("ICMPv4 received 'dst unreachable' code %d, "
                  "check your routes and firewall rules\n", icmp->code);
        goto drop_pkt; // 處理完錯誤後丟棄封包
    default: // 不支持的 ICMP 類型
        print_err("ICMPv4 did not match supported types\n");
        goto drop_pkt; // 丟棄封包
    }

drop_pkt:
    free_skb(skb); // 釋放 sk_buff
    return; // 返回
}

// 發送 ICMP 回覆
void icmpv4_reply(struct sk_buff *skb)
{
    struct iphdr *iphdr = ip_hdr(skb); // 獲取 IP 頭部
    struct icmp_v4 *icmp; // ICMP 數據指針
    struct sock sk; // 套接字結構
    memset(&sk, 0, sizeof(struct sock)); // 初始化套接字結構
    
    uint16_t icmp_len = iphdr->len - (iphdr->ihl * 4); // 計算 ICMP 數據長度

    skb_reserve(skb, ETH_HDR_LEN + IP_HDR_LEN + icmp_len); // 預留空間
    skb_push(skb, icmp_len); // 將 ICMP 數據推入 sk_buff
    
    icmp = (struct icmp_v4 *)skb->data; // 獲取 ICMP 數據指針
        
    icmp->type = ICMP_V4_REPLY; // 設置 ICMP 類型為回覆
    icmp->csum = 0; // 清空校驗和字段
    icmp->csum = checksum(icmp, icmp_len, 0); // 計算 ICMP 校驗和

    skb->protocol = ICMPV4; // 設置 sk_buff 的協議為 ICMPv4
    sk.daddr = iphdr->saddr; // 設置目標地址為來源地址（發送回覆）

    ip_output(&sk, skb); // 通過 IP 協議發送回覆
    free_skb(skb); // 釋放 sk_buff
}
