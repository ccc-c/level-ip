# 讓我們來寫一個 TCP/IP 堆疊，4：TCP 資料流和套接字 API

翻譯自： https://www.saminiir.com/lets-code-tcp-ip-stack-4-tcp-data-flow-socket-api/

之前，我們介紹了 TCP 標頭以及如何在兩方之間建立連線。

在這篇文章中，我們將研究 TCP 資料通訊及其管理方式。

此外，我們將從網路堆疊中提供一個接口，應用程式可以使用該接口進行網路通訊。然後，我們的範例應用程式利用此Socket API向網站發送簡單的 HTTP 請求。


## Transmission Control Block 變速箱控制塊
透過定義記錄資料流狀態的變數來開始討論 TCP 資料管理是有益的。

簡而言之，TCP 必須追蹤它已發送和接收確認的資料序列。為了實現這一點，為每個開啟的連接初始化一個稱為傳輸控制塊（TCB）的資料結構。

傳出（發送）端的變數是：

```
    Send Sequence Variables
	
      SND.UNA - send unacknowledged
      SND.NXT - send next
      SND.WND - send window
      SND.UP  - send urgent pointer
      SND.WL1 - segment sequence number used for last window update
      SND.WL2 - segment acknowledgment number used for last window update
      ISS     - initial send sequence number
```

依次，為接收方記錄以下資料：

```
    Receive Sequence Variables
											  
      RCV.NXT - receive next
      RCV.WND - receive window
      RCV.UP  - receive urgent pointer
      IRS     - initial receive sequence number
```

此外，目前正在處理的段落的輔助變數定義如下：

```
    Current Segment Variables
	
      SEG.SEQ - segment sequence number
      SEG.ACK - segment acknowledgment number
      SEG.LEN - segment length
      SEG.WND - segment window
      SEG.UP  - segment urgent pointer
      SEG.PRC - segment precedence value
```

這些變數共同構成了給定連線的大部分 TCP 控制邏輯。

## TCP資料通訊
一旦建立連接，就開始明確處理資料流。 TCB 中的三個變數對於狀態的基本追蹤非常重要：

```
SND.NXT- 發送者將追蹤要在 中使用的下一個序號SND.NXT。
RCV.NXT- 接收方記錄下一個期望的序號RCV.NXT。
SND.UNA- 發送方將在 中記錄最早的SND.UNA未確認序號。
```

給定 TCP 管理資料通訊並且不發生傳輸的足夠時間段，所有這三個變數將相等。

例如，當 A 決定將包含資料的段傳送給 B 時，會發生以下情況：

1. TCP A 發送一個資料段並SND.NXT在其自己的記錄 (TCB) 中前進。
2. TCB B 接收該段並透過前進來確認它RCV.NXT並發送 ACK。
3. TCB A 接收到 ACK 並前進SND.UNA。

變數前進的量就是段中資料的長度。

這是 TCP 資料傳輸控制邏輯的基礎。讓我們看看tcpdump(1)使用 捕獲網路流量的流行實用程式會是什麼樣子：

```sh
[saminiir@localhost level-ip]$ sudo tcpdump -i any port 8000 -nt
IP 10.0.0.4.12000 > 10.0.0.5.8000: Flags [S], seq 1525252, win 29200, length 0
IP 10.0.0.5.8000 > 10.0.0.4.12000: Flags [S.], seq 825056904, ack 1525253, win 29200, options [mss 1460], length 0
IP 10.0.0.4.12000 > 10.0.0.5.8000: Flags [.], ack 1, win 29200, length 0
位址 10.0.0.4（主機 A）發起從連接埠 12000 到監聽埠 8000 的主機 10.0.0.5（主機 B）的連線。
```

三次握手後，連線建立，並且它們的內部 TCP 套接字狀態設定為ESTABLISHED。 A 的初始序號為 1525252，B 的初始序號為 825056904。

```
IP 10.0.0.4.12000 > 10.0.0.5.8000: Flags [P.], seq 1:18, ack 1, win 29200, length 17
IP 10.0.0.5.8000 > 10.0.0.4.12000: Flags [.], ack 18, win 29200, length 0
```

主機 A 傳送一個包含 17 個位元組資料的資料段，主機 B 透過 ACK 資料段進行確認。預設顯示相對序號，以tcpdump方便閱讀。因此，ack 18實際上是 1525253 + 17。

在內部，接收主機 (B) 的 TCP 已前進RCV.NXT到數字 17。

```
IP 10.0.0.4.12000 > 10.0.0.5.8000: Flags [.], ack 1, win 29200, length 0
IP 10.0.0.5.8000 > 10.0.0.4.12000: Flags [P.], seq 1:18, ack 18, win 29200, length 17
IP 10.0.0.4.12000 > 10.0.0.5.8000: Flags [.], ack 18, win 29200, length 0
IP 10.0.0.5.8000 > 10.0.0.4.12000: Flags [P.], seq 18:156, ack 18, win 29200, length 138
IP 10.0.0.4.12000 > 10.0.0.5.8000: Flags [.], ack 156, win 29200, length 0
IP 10.0.0.5.8000 > 10.0.0.4.12000: Flags [P.], seq 156:374, ack 18, win 29200, length 218
IP 10.0.0.4.12000 > 10.0.0.5.8000: Flags [.], ack 374, win 29200, length 0
```

發送數據和​​確認數據的相互作用仍在繼續。可以看出，這些段落length 0僅設定了 ACK 標誌，但確認序號是根據先前接收到的段落的長度精確遞增的。

```
IP 10.0.0.5.8000 > 10.0.0.4.12000: Flags [F.], seq 374, ack 18, win 29200, length 0
IP 10.0.0.4.12000 > 10.0.0.5.8000: Flags [.], ack 375, win 29200, length 0
```

主機 B 透過產生 FIN 段來通知主機 A 它沒有更多資料可傳送。反過來，主機 A 也承認這一點。

為了完成連接，主機 A 還必須發出訊號表明它沒有更多資料要發送。

## TCP 連線終止
關閉 TCP 連線同樣是一個複雜的操作，可以強制終止（RST）或透過雙方協定（FIN）完成。

基本場景如下：

1. 主動關閉器發送FIN 報文段。
2. 被動關閉者透過發送 ACK 段來確認這一點。
3. 被動關閉器開始自己的關閉操作（當它沒有更多資料要發送時），並有效地成為主動關閉器。
4. 一旦雙方都向對方發送了 FIN 並且向兩個方向都確認了它們，連接就會關閉。

顯然，TCP 連線的關閉需要四個段，而 TCP 連線建立（三次握手）則需要三個段。

此外，TCP 是一種雙向協議，因此可以讓另一端宣布它沒有更多資料要發送，但仍保持在線狀態以接收傳入資料。這稱為TCP 半關閉。

封包交換網路的不可靠特性為連線終止帶來了額外的複雜性 - FIN 區段可能會消失或永遠不會被有意發送，從而使連線處於尷尬的狀態。例如，在 Linux 中，核心參數tcp_fin_timeout控制 TCP 在強制關閉連線之前等待最終 FIN 封包的秒數。這違反了規範，但卻是預防拒絕服務 (DoS) 所必需的。1

中止連接涉及設定了 RST 標誌的段。重置發生的原因有很多，但常見的原因有：

1. 向不存在的連接埠或介面發出連線請求
2. 另一個 TCP 已崩潰並最終處於不同步連線狀態
3. 嘗試幹擾現有連線

因此，TCP 資料傳輸的最佳路徑永遠不會涉及 RST 段。

## 套接字(Socket) API
為了能夠利用網路堆疊，必須為應用程式提供某種介面。 BSD Socket API是最著名的一種，它起源於 1983 年的 4.2BSD UNIX 版本。4

socket(2)透過呼叫 並將套接字類型和協定作為參數傳遞，可以從網路堆疊中保留套接字。通用值適用AF_INET於類型和SOCK_STREAM網域。這將預設為 TCP-over-IPv4 套接字。

成功從網路堆疊保留 TCP 套接字後，它將連接到遠端端點。這是connect(2)使用的地方，呼叫它將啟動 TCP 握手。

從那時起，我們就可以從套接字取得資料write(2)了。read(2)

網路堆疊將處理 TCP 流中資料的排隊、重傳、錯誤檢查和重組。對於應用程式來說，TCP的內部行為大多是不透明的。應用程式唯一可以依賴的是 TCP 已確認發送和接收資料流的責任，並且它將透過套接字 API 通知應用程式意外行為。

作為範例，讓我們看一下簡單調用所做的系統呼叫curl(1)：

```
$ strace -esocket,connect,sendto,recvfrom,close curl -s example.com > /dev/null
...
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) = 3
connect(3, {sa_family=AF_INET, sin_port=htons(80), sin_addr=inet_addr("93.184.216.34")}, 16) = -1 EINPROGRESS (Operation now in progress)
sendto(3, "GET / HTTP/1.1\r\nHost: example.co"..., 75, MSG_NOSIGNAL, NULL, 0) = 75
recvfrom(3, "HTTP/1.1 200 OK\r\nCache-Control: "..., 16384, 0, NULL, NULL) = 1448
close(3)                                = 0
+++ exited with 0 +++
```

我們使用strace(1)追蹤系統呼叫和訊號的工具來觀察 Socket API 呼叫。步驟是：

1. 使用 開啟套接字socket，並將類型指定為 IPv4/TCP。
2. connect啟動 TCP 握手。目標位址和連接埠被傳遞給該函數。
3. 連線成功建立後，sendto(3)用於將資料寫入套接字 - 在本例中為通常的 HTTP GET 請求。
4. 從那時起，curl最終使用 讀取傳入的資料recvfrom。

精明的讀者可能已經注意到沒有發出read任何系統調用。write這是因為實際的socket API不包含這些函數，但普通的I/O操作也可以使用。來自man socket(7): 4

此外，標準 I/O 操作（如 write(2)、writev(2)、sendfile(2)、read(2) 和 readv(2)）可用於讀取和寫入資料。

最後，Socket API 包含多個僅用於寫入和讀取資料的函數。 I/O 函數係列使這變得複雜，這些函數也可用於操作套接字的檔案描述符。

## 測試我們的 Socket API
現在我們的網路堆疊提供了套接字接口，我們可以針對它編寫應用程式。

流行的工具curl用於透過給定協定傳輸資料。我們可以透過寫一個最小的實作來複製 HTTP GET 行為curl：

```sh
$ ./lvl-ip curl example.com 80

...
<!doctype html>
<html>
<head>
    <title>Example Domain</title>

    <meta charset="utf-8" />
    <meta http-equiv="Content-type" content="text/html; charset=utf-8" />
</head>

<body>
<div>
    <h1>Example Domain</h1>
    <p>This domain is established to be used for illustrative examples in documents. You may use this
    domain in examples without prior coordination or asking for permission.</p>
    <p><a href="http://www.iana.org/domains/example">More information...</a></p>
</div>
</body>
</html>
```

最後，發送 HTTP GET 請求只能最低程度地鍛鍊底層網路堆疊。

## 結論
現在，我們基本上已經實作了具有簡單資料管理的基本 TCP，並提供了應用程式可以使用的介面。

然而，TCP數據通訊並不是一個簡單的問題。資料包在傳輸過程中可能會被損壞、重新排序或遺失。此外，資料傳輸可能會堵塞網路中的任意元素。

為此，TCP數據通訊需要包含更複雜的邏輯。在下一篇文章中，我們將研究 TCP 視窗管理和 TCP 重傳逾時機制，以更好地應對更具挑戰性的設定。

該專案的源代碼託管在GitHub上。

如果您喜歡這篇文章，您可以 與您的追蹤者分享 並 在 Twitter 上關注我！

## 來源
* https://linux.die.net/man/7/tcp  ↩
* https://en.wikipedia.org/wiki/TCP_reset_attack  ↩
* http://www.kohala.com/start/tcpipiv2.html  ↩
* http://man7.org/linux/man-pages/man7/socket.7.html  ↩  ↩ 2

