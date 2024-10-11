# 讓我們來寫一個 TCP/IP 堆疊，1：乙太網路和 ARP

翻譯自：https://www.saminiir.com/lets-code-tcp-ip-stack-1-ethernet-arp/

編寫自己的 TCP/IP 堆疊似乎是一項艱鉅的任務。事實上，TCP 在其三十多年的生命週期中累積了許多規範。然而，核心規範看似緊湊1 - 重要部分是 TCP 標頭解析、狀態機、擁塞控制和重傳逾時計算。

最常見的第 2 層和第 3 層協定（分別是乙太網路和 IP）與 TCP 的複雜性相比顯得蒼白無力。在本部落格系列中，我們將為 Linux 實作一個最小的使用者空間 TCP/IP 堆疊。

這些貼文和由此產生的軟體的目的純粹是教育性的 - 更深入地學習網路和系統程式設計。

## TUN/TAP 設備

為了攔截來自 Linux 核心的低階網路流量，我們將使用 Linux TAP 設備。簡而言之，網路用戶空間應用程式經常使用 TUN/TAP 設備分別操作 L3/L2 流量。一個流行的例子是隧道，其中一個資料包被包裝在另一個資料包的有效負載內。

TUN/TAP 設備的優點是它們很容易在用戶空間程式中設置，而且它們已經在許多程式中使用，例如OpenVPN。

由於我們想要從第 2 層開始建置網路堆疊，因此我們需要一個 TAP 設備。我們像這樣實例化它：

```c
/*
 * Taken from Kernel Documentation/networking/tuntap.txt
 */
int tun_alloc(char *dev)
{
    struct ifreq ifr;
    int fd, err;

    if( (fd = open("/dev/net/tap", O_RDWR)) < 0 ) {
        print_error("Cannot open TUN/TAP dev");
        exit(1);
    }

    CLEAR(ifr);

    /* Flags: IFF_TUN   - TUN device (no Ethernet headers)
     *        IFF_TAP   - TAP device
     *
     *        IFF_NO_PI - Do not provide packet information
     */
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if( *dev ) {
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    }

    if( (err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ){
        print_error("ERR: Could not ioctl tun: %s\n", strerror(errno));
        close(fd);
        return err;
    }

    strcpy(dev, ifr.ifr_name);
    return fd;
}
```

此後，傳回的檔案描述符fd可用於將read資料write寫入虛擬裝置的乙太網路緩衝區。

該標誌IFF_NO_PI在這裡至關重要，否則我們最終會在乙太網路幀前面添加不必要的資料包資訊。您實際上可以查看 tun-device 驅動程式的核心原始碼並自行驗證。

## 乙太網路幀格式

多種不同的乙太網路技術是連接區域網路(LAN) 中電腦的支柱。與所有實體技術一樣，乙太網路標準自 1980 年由 Digital Equipment Corporation、Intel 和 Xerox 發布的第一個版本2以來已經有了很大的發展。

按照當今的標準，乙太網路的第一個版本速度很慢 - 大約 10Mb/s，並且它使用半雙工通信，這意味著您要么發送或接收數據，但不能同時發送或接收數據。這就是為什麼必須合併媒體存取控制(MAC) 協定來組織資料流的原因。即使到今天，如果在半雙工模式下運行乙太網路接口，仍需要載波偵聽、帶衝突檢測的多路存取(CSMA/CD) 作為 MAC 方法。

100BASE-T乙太網路標準的發明使用雙絞線來實現​​全雙工通訊和更高的吞吐速度。此外，乙太網路交換器的日益普及使得 CSMA/CD 基本上已經過時。

不同的乙太網路標準由 IEEE 802.3 3工作組維護。

接下來，我們將看一下乙太網路幀頭。可以將其聲明為 C 結構體，如下所示：

```c
#include <linux/if_ether.h>

struct eth_hdr
{
    unsigned char dmac[6];
    unsigned char smac[6];
    uint16_t ethertype;
    unsigned char payload[];
} __attribute__((packed));
```

dmac和smac是非常不言自明的欄位。它們包含通訊雙方的 MAC 位址（分別是目標和來源）。

重載字段ethertype是一個 2 個八位元組的字段，根據其值，指示有效負載的長度或類型。具體來說，如果該欄位的值大於或等於1536，則該欄位包含有效負載的類型（例如IPv4、ARP）。如果該值小於該值，則它包含有效負載的長度。

在類型欄位之後，乙太網路幀可能有幾個不同的標籤。這些標籤可用於描述訊框的虛擬 LAN (VLAN) 或服務品質(QoS) 類型。乙太網路幀標籤被排除在我們的實作之外，因此對應的欄位也不會出現在我們的協定聲明中。

此欄位payload包含指向乙太網路訊框有效負載的指標。在我們的例子中，這將包含 ARP 或 IPv4 封包。如果有效負載長度小於所需的最小48位元組（不含標籤），則將填充位元組附加到有效負載的末端以滿足要求。

我們還包含if_ether.hLinux 標頭來提供以太類型及其十六進位值之間的對應。

最後，乙太網路幀格式末尾還包含幀校驗序列字段，該字段與循環冗餘校驗（CRC）一起用於檢查幀的完整性。我們將在實作中省略對該欄位的處理。

## 乙太網路幀解析
結構體聲明中打包的屬性是實作細節 - 它用於指示 GNU C 編譯器不要優化結構體記憶體佈局以實現與填充位元組4 的資料對齊。這個屬性的使用純粹源自於我們「解析」協定緩衝區的方式，它只是使用正確的協定結構對資料緩衝區進行類型轉換：

```c
struct eth_hdr *hdr = (struct eth_hdr *) buf;
```

一種可移植但稍微費力的方法是手動序列化協定資料。這樣，編譯器就可以自由地添加填充字節，以更好地符合不同處理器的資料對齊要求。

解析和處理傳入乙太網路幀的整體場景非常簡單：

```c
if (tun_read(buf, BUFLEN) < 0) {
    print_error("ERR: Read from tun_fd: %s\n", strerror(errno));
}

struct eth_hdr *hdr = init_eth_hdr(buf);

handle_frame(&netdev, hdr);
```

該handle_frame函數只是查看ethertype乙太網路標頭的字段，並根據該值決定下一步操作。

## 地址解析協定
位址解析協定（ARP）用於將48位元乙太網路位址（MAC位址）動態對應到協定位址（例如IPv4位址）。這裡的關鍵是，透過 ARP，可以使用多種不同的 L3 協定：不僅是 IPv4，還有其他協議，例如聲明 16 位元協定位址的 CHAOS。

通常的情況是您知道 LAN 中某些服務的 IP 位址，但要建立實際通信，還需要知道硬體位址 (MAC)。因此，ARP用於廣播和查詢網絡，要求IP位址的所有者報告其硬體位址。

ARP封包格式比較簡單：

```c
struct arp_hdr
{
    uint16_t hwtype;
    uint16_t protype;
    unsigned char hwsize;
    unsigned char prosize;
    uint16_t opcode;
    unsigned char data[];
} __attribute__((packed));
```

ARP 標頭 ( arp_hdr) 包含 2 個八位元組hwtype，它決定所使用的連結層類型。在我們的例子中，這是以太網，實際值為0x0001。

2 個八位元protype組欄位指示協定類型。在我們的例子中，這是 IPv4，透過值 進行通訊0x0800。

hwsize和欄位prosize的大小都是 1 個八位元組，它們分別包含硬體和協定欄位的大小。在我們的例子中，MAC 位址為 6 個位元組，IP 位址為 4 個位元組。

2 個八位元組欄位opcode聲明 ARP 訊息的類型。它可以是 ARP 請求 (1)、ARP 應答 (2)、RARP 請求 (3) 或 RARP 應答 (4)。

此data欄位包含 ARP 訊息的實際負載，在我們的例子中，它將包含 IPv4 特定資訊：

```c
struct arp_ipv4
{
    unsigned char smac[6];
    uint32_t sip;
    unsigned char dmac[6];
    uint32_t dip;
} __attribute__((packed));
```

這些欄位非常不言自明。分別smac包含dmac發送方和接收方的 6 位元組 MAC 位址。sip並dip分別包含發送者和接收者的 IP 位址。

## 地址解析演算法

原始規範描述了這個簡單的位址解析演算法：

```
?Do I have the hardware type in ar$hrd?
Yes: (almost definitely)
  [optionally check the hardware length ar$hln]
  ?Do I speak the protocol in ar$pro?
  Yes:
    [optionally check the protocol length ar$pln]
    Merge_flag := false
    If the pair <protocol type, sender protocol address> is
        already in my translation table, update the sender
        hardware address field of the entry with the new
        information in the packet and set Merge_flag to true.
    ?Am I the target protocol address?
    Yes:
      If Merge_flag is false, add the triplet <protocol type,
          sender protocol address, sender hardware address> to
          the translation table.
      ?Is the opcode ares_op$REQUEST?  (NOW look at the opcode!!)
      Yes:
        Swap hardware and protocol fields, putting the local
            hardware and protocol addresses in the sender fields.
        Set the ar$op field to ares_op$REPLY
        Send the packet to the (new) target hardware address on
            the same hardware on which the request was received.
```

即，translation table用於儲存 ARP 結果，以便主機只需查找其快取中是否已有該條目即可。這可以避免向網路發送冗餘 ARP 請求的垃圾郵件。

該演算法在arp.c中實作。

最後，對 ARP 實現的最終測試是看它是否正確回應 ARP 請求：

```
[saminiir@localhost lvl-ip]$ arping -I tap0 10.0.0.4
ARPING 10.0.0.4 from 192.168.1.32 tap0
Unicast reply from 10.0.0.4 [00:0C:29:6D:50:25]  3.170ms
Unicast reply from 10.0.0.4 [00:0C:29:6D:50:25]  13.309ms

[saminiir@localhost lvl-ip]$ arp
Address                  HWtype  HWaddress           Flags Mask            Iface
10.0.0.4                 ether   00:0c:29:6d:50:25   C                     tap0
```

核心的網路堆疊識別出來自我們自訂網路堆疊的 ARP 回复，因此用我們的虛擬網路設備的條目填充其 ARP 快取。成功！

## 結論

乙太網路幀處理和 ARP 的最小實作相對容易，只需幾行程式碼即可完成。相反，獎勵因素相當高，因為您可以使用自己的虛擬乙太網路裝置填充 Linux 主機的 ARP 快取！

該專案的源代碼可以在GitHub上找到。

在下一篇文章中，我們將繼續使用 ICMP 回顯和回應 (ping) 以及 IPv4 封包解析來實現。

如果您喜歡這篇文章，您可以 與您的追蹤者分享 並 在 Twitter 上關注我！

感謝Xiaochen Wang，他的類似實現對於我加快 C 網路程式設計和協議處理的速度來說是無價的。我發現他的原始碼5很容易理解，而且我的一些設計選擇是直接從他的實現中複製的。

## 來源

* https://tools.ietf.org/html/rfc7414  ↩
* http://ethernehistory.typepad.com/papers/EthernetSpec.pdf  ↩
* https://en.wikipedia.org/wiki/IEEE_802.3  ↩
* https://gcc.gnu.org/onlinedocs/gcc/Common-Type-Attributes.html#Common-Type-Attributes  ↩
* https://github.com/chobits/tapip  ↩
