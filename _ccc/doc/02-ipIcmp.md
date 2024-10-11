## 讓我們來寫一個 TCP/IP 堆疊，2：IPv4 和 ICMPv4

翻譯自：https://www.saminiir.com/lets-code-tcp-ip-stack-2-ipv4-icmpv4/

這次，在我們的使用者空間 TCP/IP 堆疊中，我們將實作一個最小可行的 IP 層，並使用 ICMP 回顯請求（也稱為ping）進行測試。

我們將了解 IPv4 和 ICMPv4 的格式並描述如何檢查它們的完整性。某些功能（例如 IP 分段）留給讀者當作練習。

對於我們的網路堆疊，選擇 IPv4 而不是 IPv6，因為它仍然是 Internet 的預設網路協定。然而，這種情況正在迅速改變1，我們的網路堆疊將來可以透過 IPv6 進行擴展。

## 網際網路協定版本 4
我們的實作中的下一層 (L3) 2（在乙太網路訊框之後）處理將資料傳送到目的地的情況。也就是說，網際網路協定(IP) 的發明是為了為 TCP 和 UDP 等傳輸協定提供基礎。它是無連接的，這意味著與 TCP 不同，所有資料封包在網路堆疊中都是相互獨立處理的。這也意味著 IP 資料封包可能會無序到達。3

此外，IP 並不保證成功交付。這是協議設計者有意識的選擇，因為 IP 旨在為同樣不保證交付的協定提供基礎。 UDP 就是這樣一種協定。

如果通訊雙方之間需要可靠性，則可以在 IP 之上使用 TCP 等協定。在這種情況下，更高層級的協定負責檢測遺失的資料並確保所有資料均已交付。

## 標頭格式
IPv4 標頭的長度通常為 20 個八位元組。標頭可以包含尾隨選項，但我們的實作中省略了它們。字段的含義相對簡單，可以用 C 結構體來描述：

```c
struct iphdr {
    uint8_t version : 4;
    uint8_t ihl : 4;
    uint8_t tos;
    uint16_t len;
    uint16_t id;
    uint16_t flags : 3;
    uint16_t frag_offset : 13;
    uint8_t ttl;
    uint8_t proto;
    uint16_t csum;
    uint32_t saddr;
    uint32_t daddr;
} __attribute__((packed));
```

4 位元欄位version指示 Internet 標頭的格式。在我們的例子中，IPv4 的值為 4。

網路標頭長度欄位的長度同樣為 4 位，指示IP 標頭中32 位元字ihl的數量。由於該欄位的大小為 4 位，因此它只能容納最大值 15。

服務類型欄位源自tos第一IP規格4。在後來的規範中它已被劃分為更小的字段，但為了簡單起見，我們將按照原始規範中的定義來對待該字段。此欄位傳達 IP 資料封包的服務品質。

總長度欄位傳達len 整個 IP 資料封包的長度。由於它是 16 位元字段，因此最大長度為 65535 位元組。大型 IP 資料報會分割成更小的資料報，以滿足不同通訊介面的最大傳輸單元(MTU)。

此id欄位用於索引資料報，最終用於重組分片的IP資料報。該欄位的值只是一個由發送方遞增的計數器。反過來，接收方知道如何對傳入的片段進行排序。

此flags欄位定義了資料封包的各種控制標誌。具體來說，發送方可以指定資料封包是否允許分片，是否是最後一個分片，或者還有更多分片傳入。

片段偏移欄位frag_offset指示片段在資料封包中的位置。當然，第一個資料報的索引設定為 0。

ttl或生存時間是一個常見屬性，用於倒數資料報的生命週期。它通常由原始發送方設定為 64，每個接收方都會將該計數器減 1。當它達到零時，資料封包將被丟棄，並且可能會回覆 ICMP 訊息以指示錯誤。

此proto欄位為資料封包提供了在其有效負載中攜帶其他協定的固有能力。此欄位通常包含 16 (UDP) 或 6 (TCP) 等值，僅用於向接收方傳達實際資料的類型。

報頭校驗和字段，csum用於驗證 IP 標頭的完整性。它的演算法相對簡單，將在本教程中進一步解釋。

最後，saddr和daddr欄位分別指示資料封包的來源位址和目標位址。儘管這些欄位的長度為 32 位，因此提供了大約 45 億個位址的池，但位址範圍將在不久的將來耗盡5。 IPv6 協定將此長度擴展到 128 位元，因此，可以永久地保證網際網路協定的位址範圍不會過時。

## 網路校驗和
網際網路校驗和欄位用於檢查 IP 資料封包的完整性。計算校驗與比較簡單，在原始規範4中定義：

校驗和欄位是標頭中所有16位元字的反碼和的16位元反碼。為了計算校驗和，校驗和欄位的值為零。

演算法的實際程式碼如下：

```c
uint16_t checksum(void *addr, int count)
{
    /* Compute Internet Checksum for "count" bytes
     *         beginning at location "addr".
     * Taken from https://tools.ietf.org/html/rfc1071
     */

    register uint32_t sum = 0;
    uint16_t * ptr = addr;

    while( count > 1 )  {
        /*  This is the inner loop */
        sum += * ptr++;
        count -= 2;
    }

    /*  Add left-over byte, if any */
    if( count > 0 )
        sum += * (uint8_t *) ptr;

    /*  Fold 32-bit sum to 16 bits */
    while (sum>>16)
        sum = (sum & 0xffff) + (sum >> 16);

    return ~sum;
}
```

以 IP 標頭為例45 00 00 54 41 e0 40 00 40 01 00 00 0a 00 00 04 0a 00 00 05：

1. 將欄位相加得到兩個補碼 sum 01 1b 3e。
2. 然後，為了將其轉換為補碼，將進位位元新增至前 16 位元：1b 3e+ 01= 1b 3f。
3. 最後，取總和的反碼，得到校驗和值e4c0。

IP 標頭變為45 00 00 54 41 e0 40 00 40 01 e4 c0 0a 00 00 04 0a 00 00 05.

可以透過再次應用該演算法來驗證校驗和，如果結果為 0，則資料很可能是好的。

## 網際網路控制訊息協定版本 ICMP 4

由於網際網路協定缺乏可靠性機制，因此需要某種方式來通知通訊方可能的錯誤情況。因此，網際網路控制訊息協定(ICMP) 7用於網路中的診斷措施。例如，網關無法存取的情況 - 識別出這種情況的網路堆疊會將 ICMP「網關無法存取」訊息傳回來源。

## 標頭格式

ICMP 標頭駐留在對應 IP 封包的有效負載中。 ICMPv4報頭的結構如下：

```c
struct icmp_v4 {
    uint8_t type;
    uint8_t code;
    uint16_t csum;
    uint8_t data[];
} __attribute__((packed));
```

這裡，該type字段傳達訊息的目的。類型欄位保留了42個不同的8個值，但常用的只有大約8個。在我們的實作中，使用類型 0（Echo Reply）、3（Destination Unreachable）和 8（Echo request）。

該code欄位進一步描述了訊息的含義。例如，當類型為 3（目的地不可達）時，代碼欄位暗示原因。一個常見錯誤是當封包無法路由到網路時：始發主機很可能會收到類型為 3 且代碼為 0（網路不可達）的 ICMP 訊息。

此csum欄位與IPv4標頭中的校驗和欄位相同，並且可以使用相同的演算法來計算它。然而，在 ICMPv4 中，校驗和是端對端的，這意味著計算校驗和時還包括有效負載。

## 訊息及其處理

實際的 ICMP 負載由查詢/資訊訊息和錯誤訊息組成。首先，我們來看看回顯請求/回覆訊息，在網路中通常稱為「ping」：

```c
struct icmp_v4_echo {
    uint16_t id;
    uint16_t seq;
    uint8_t data[];
} __attribute__((packed));
```

訊息格式緊湊。此欄位id由傳送主機設置，以確定回顯應答要傳送給哪個進程。例如，可以在此欄位中設定進程 ID。

這個欄seq位是回顯的序號，它只是一個從零開始的數字，每當形成新的回顯請求時就加一。這用於檢測回顯訊息在傳輸過程中是否消失或重新排序。

此data欄位是可選的，但通常包含回顯時間戳等資訊。然後可以使用它來估計主機之間的往返時間。

也許最常見的 ICMPv4 錯誤訊息Destination Unreachable具有以下格式：

```c
struct icmp_v4_dst_unreachable {
    uint8_t unused;
    uint8_t len;
    uint16_t var;
    uint8_t data[];
} __attribute__((packed));
```

第一個八位元組未使用。然後，該len欄位指示原始資料封包的長度，對於 IPv4，以 4 個八位元位元組為單位。 2 個八位元組欄位的值var取決於 ICMP 代碼。

最後，盡可能多的導致Destination Unreachable狀態的原始IP封包被放入該data欄位中。

## 測試實施
從 shell 中，我們可以驗證使用者空間網路堆疊是否回應 ICMP 回顯請求：

```sh
[saminiir@localhost ~]$ ping -c3 10.0.0.4
PING 10.0.0.4 (10.0.0.4) 56(84) bytes of data.
64 bytes from 10.0.0.4: icmp_seq=1 ttl=64 time=0.191 ms
64 bytes from 10.0.0.4: icmp_seq=2 ttl=64 time=0.200 ms
64 bytes from 10.0.0.4: icmp_seq=3 ttl=64 time=0.150 ms

--- 10.0.0.4 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss, time 1999ms
rtt min/avg/max/mdev = 0.150/0.180/0.200/0.024 ms
```

## 結論

可以相對輕鬆地建立處理乙太網路訊框、ARP 和 IP 的最小可行網路堆疊。然而，原來的規範已經擴展了許多新的規範。在這篇文章中，我們瀏覽了 IP 功能，例如選項、分段以及標頭 DCN 和 DS 欄位。

此外，IPv6 對於互聯網的未來至關重要。它尚未普及，但作為比 IPv4 更新的協議，它絕對應該在我們的網路堆疊中實現。

這篇部落格的原始碼可以在GitHub上找到。

在下一篇部落格文章中，我們將進入傳輸層（L4）並開始實施臭名昭著的傳輸控制協定（TCP）。也就是說，TCP是一種面向連線的協議，確保通訊雙方之間的可靠性。這些面向顯然帶來了更多的複雜性，而且作為一個古老的協議，TCP也有其陰暗的角落。

如果您喜歡這篇文章，您可以 與您的追蹤者分享 並 在 Twitter 上關注我！

## 來源
* https://en.wikipedia.org/wiki/IPv6_deployment  ↩
* https://en.wikipedia.org/wiki/OSI_model  ↩
* https://en.wikipedia.org/wiki/TCP/IP_Illustration#Volume_1:_The_Protocols  ↩
* http://tools.ietf.org/html/rfc791  ↩  ↩ 2
* https://en.wikipedia.org/wiki/IPv4_address_exhaustion  ↩
* https://tools.ietf.org/html/rfc1071  ↩
* https://www.ietf.org/rfc/rfc792.txt  ↩
* http://www.iana.org/assignments/icmp-parameters/icmp-parameters.xhtml  ↩