# 讓我們來寫一個 TCP/IP 堆疊，5：TCP 重傳

至此，我們有了一個能夠與 Internet 中的其他主機通訊的 TCP/IP 堆疊。到目前為止的實作相當簡單，但缺少一個主要功能：可靠性。

也就是說，我們的 TCP 不保證它呈現給應用程式的資料流的完整性。如果握手資料包在傳輸過程中遺失，即使建立連線也可能失敗。

引入可靠性和控制是我們從頭開始創建 TCP/IP 堆疊的下一個主要重點。

## 自動重複請求
許多可靠協定的基礎是自動重複請求 (ARQ) 1的概念。

在 ARQ 中，接收方會傳送其已接收資料的確認，而傳送者則重新傳送其從未收到確認的資料。

正如我們所討論的，TCP 將傳輸資料的序號保存在記憶體中並以確認進行回應。傳輸的資料被放入重傳佇列中，並且啟動與資料相關的計時器。如果在定時器逾時之前沒有收到資料序列的確認，則會發生重傳。

可以看出，TCP 的可靠性是建立在 ARQ 原理之上的。然而，涉及到ARQ的詳細實現。簡單的問題，例如“發件人應該等待確認多長時間？”很難回答，特別是當需要最大性能時。像是選擇性確認 (SACK) 2這樣的 TCP 擴展可透過確認無序資料並避免不必要的往返來緩解效率問題。

## TCP重傳
TCP 中的重傳在原始規範3中描述為：

當TCP傳輸包含資料的封包段時，它會將副本放入重傳佇列中並啟動計時器；當收到該資料的確認時，該段將從佇列中刪除。如果在定時器逾時之前沒有收到確認，則重新傳送該段。

然而，原來的重傳超時計算公式被認為不適用於不同的網路環境。 Jacobson 5描述了目前的「標準方法」4 ，最新的規範化規範可以從 RFC6298 6中找到。

基本演算法相對簡單。對於給定的 TCP 發送方，定義狀態變數來計算逾時：

1. srtt是平滑的往返時間，用於平均分段的往返時間 (RTT)
2. rttvar儲存往返時間變化
3. rto最終保存重傳超時，例如以毫秒為單位

簡而言之，srtt充當連續 RTT 的低通濾波器。由於 RTT 可能存在較大變化，rttvar因此用於檢測這些變化並防止它們扭曲平均函數。另外，G假定時鐘粒度為秒。

如 RFC6298 所述，計算步驟如下：

在第一次 RTT 測量之前：

    rto = 1000ms

在第一個 RTT 測量R上：

    srtt = R
    rttvar = R/2
    rto = srtt + max(G, 4*rttvar)

關於後續測量：

    alpha = 0.125
    beta = 0.25
    rttvar = (1 - beta) * rttvar + beta * abs(srtt - r)
    srtt = (1 - alpha) * srtt + alpha * r
    rto = srtt + max(g, 4*rttvar)

計算後rto，如果小於1秒，則四捨五入為1秒。可以提供最大數量，但必須至少為 60 秒

TCP 實現的時脈粒度傳統上被估計為相當高，範圍從 500 毫秒到 1 秒。然而，像 Linux 這樣的現代系統使用 1 毫秒4的時脈粒度。

需要注意的一件事是，建議 RTO 始終至少為 1 秒。這是為了防止虛假重傳，即當某個資料段重傳太快時，會導致網路擁塞。實際上，許多實作都採用亞秒舍入：Linux 使用 200 毫秒。

## 卡恩演算法

Karn 演算法7是一種防止 RTT 測量給出錯誤結果的強制演算法。它只是指出不應為重傳的資料包取得 RTT 樣本。

換句話說，TCP 發送方會追蹤其發送的分段是否為重傳，並跳過這些確認的 RTT 例程。這是有道理的，因為否則發送方無法區分原始段和重傳段之間的確認。

然而，當使用時間戳 TCP 選項時，可以測量每個 ACK 段的 RTT。我們將在單獨的部落格文章中討論 TCP 時間戳選項。

## 管理 RTO 定時器
管理重傳定時器相對簡單。 RFC6298推薦以下演算法：

1. 當發送資料段且 RTO 定時器未運行時，將其激活，超時值為rto
2. 當所有未完成的資料段都被確認後，關閉 RTO 定時器
3. 當收到新資料的 ACK 時，用以下值重新啟動 RTO 定時器：rto

當 RTO 定時器到期時：

1. 重傳最早的未確認段
2. 將 RTO 定時器退後 2 倍，即 ( rto = rto * 2)
3. 啟動RTO定時器

此外，當 RTO 值出現回退並且成功進行後續測量時，RTO 值可能會急劇縮小。 TCP 實作可能會在退出並等待確認時清除 srtt 與 rttvar

## 請求重傳
TCP 通常不只是依賴 TCP 發送者的計時器來修復遺失的資料包。接收方也可以通知發送方需要重傳分段。

重複確認是一種對無序段進行確認的演算法，但按最新有序段的序號進行確認。在三個重複確認之後，TCP 發送方應該意識到它需要重新傳輸由重複確認通告的段。

此外，選擇性確認（SACK）是重複確認的更複雜版本。它是一個 TCP 選項，接收器能夠將接收到的序列編碼到其確認中。然後發送者立即註意到任何丟失的資料段並重新發送它們。我們將在後面的部落格文章中討論 SACK TCP 選項。

## 嘗試一下
現在我們已經了解了概念和一般演算法，讓我們看看 TCP 重傳在網路上的樣子。

讓我們更改防火牆規則，在連線建立後丟棄資料包，並嘗試取得 HN 的首頁：

```
$ iptables -I FORWARD --in-interface tap0 \
	-m conntrack --ctstate ESTABLISHED \
	-j DROP
$ ./tools/level-ip curl news.ycombinator.com
curl: (56) Recv failure: Connection timed out
```

觀察連線流量，我們發現HTTP GET重送的時間間隔大約是雙倍：

```
[saminiir@localhost ~]$ sudo tcpdump -i tap0 host 10.0.0.4 -n -ttttt
00:00:00.000000 IP 10.0.0.4.41733 > 104.20.44.44.80: Flags [S], seq 3975419138, win 44477, options [mss 1460], length 0
00:00:00.004318 IP 104.20.44.44.80 > 10.0.0.4.41733: Flags [S.], seq 4164704437, ack 3975419139, win 29200, options [mss 1460], length 0
00:00:00.004534 IP 10.0.0.4.41733 > 104.20.44.44.80: Flags [.], ack 1, win 44477, length 0
00:00:00.011039 IP 10.0.0.4.41733 > 104.20.44.44.80: Flags [P.], seq 1:85, ack 1, win 44477, length 84: HTTP: GET / HTTP/1.1
00:00:01.094237 IP 104.20.44.44.80 > 10.0.0.4.41733: Flags [S.], seq 4164704437, ack 3975419139, win 29200, options [mss 1460], length 0
00:00:01.094479 IP 10.0.0.4.41733 > 104.20.44.44.80: Flags [.], ack 1, win 44477, length 0
00:00:01.210787 IP 10.0.0.4.41733 > 104.20.44.44.80: Flags [P.], seq 1:85, ack 1, win 44477, length 84: HTTP: GET / HTTP/1.1
00:00:03.607225 IP 10.0.0.4.41733 > 104.20.44.44.80: Flags [P.], seq 1:85, ack 1, win 44477, length 84: HTTP: GET / HTTP/1.1
00:00:08.399056 IP 10.0.0.4.41733 > 104.20.44.44.80: Flags [P.], seq 1:85, ack 1, win 44477, length 84: HTTP: GET / HTTP/1.1
00:00:18.002415 IP 10.0.0.4.41733 > 104.20.44.44.80: Flags [P.], seq 1:85, ack 1, win 44477, length 84: HTTP: GET / HTTP/1.1
00:00:37.289491 IP 10.0.0.4.41733 > 104.20.44.44.80: Flags [P.], seq 1:85, ack 1, win 44477, length 84: HTTP: GET / HTTP/1.1
00:01:15.656151 IP 10.0.0.4.41733 > 104.20.44.44.80: Flags [P.], seq 1:85, ack 1, win 44477, length 84: HTTP: GET / HTTP/1.1
00:02:32.590664 IP 10.0.0.4.41733 > 104.20.44.44.80: Flags [P.], seq 1:85, ack 1, win 44477, length 84: HTTP: GET / HTTP/1.1
```

驗證重傳回退和接收器靜默很容易，但是僅對某些段觸發重傳的情況又如何呢？為了獲得最佳效能，RTO 演算法需要在偵測到連接正常時「反彈」。

讓我們將防火牆規則設定為僅阻止第 6 個封包 6000 位元組：

```
$ iptables -I FORWARD --in-interface tap0 \
	-m connbytes --connbytes 6 \
	--connbytes-dir original --connbytes-mode packets \
	-m quota --quota 6000 -j DROP
```

現在，如果我們嘗試發送一些數據，我們的 TCP 必須識別通訊中斷及其結束時間。讓我們發送 6009 位元組：

```
$ ./tools/level-ip curl -X POST http://httpbin.org/post \
	-d "payload=$(perl -e "print 'lorem ipsum ' x500")"
```

讓我們逐步完成連線階段，看看何時觸發重傳以及 RTO 值如何變化。下面是修改後的tcpdump輸出，其中包含 TCP 套接字內部狀態的內聯註解：

```
00.000000 10.0.0.4.49951 > httpbin.org.80: [S], seq 1, options [mss 1460]
00.120709 httpbin.org.80 > 10.0.0.4.49951: [S.], seq 1, ack 1, options [mss 8961]
00.120951 10.0.0.4.49951 > httpbin.org.80: [.], ack 1

- Connection established, TCP RTO value of 10.0.0.4:49951 is 1000 milliseconds.

00.122686 10.0.0.4.49951 > httpbin.org.80: [P.], seq 1:174, ack 1: POST /post
00.242564 httpbin.org.80 > 10.0.0.4.49951: [.], ack 174
01.141287 10.0.0.4.49951 > httpbin.org.80: [.], seq 174:1634, ack 1: HTTP
01.141386 10.0.0.4.49951 > httpbin.org.80: [.], seq 1634:3094, ack 1: HTTP
01.141460 10.0.0.4.49951 > httpbin.org.80: [.], seq 3094:4554, ack 1: HTTP
01.263301 httpbin.org.80 > 10.0.0.4.49951: [.], ack 1634
01.265995 httpbin.org.80 > 10.0.0.4.49951: [.], ack 3094

- So far so good, the remote host has acked our HTTP POST 
  and the start of our payload. RTO value is 336ms.

01.526797 10.0.0.4.49951 > httpbin.org.80: [.], seq 3094:4554, ack 1: HTTP
02.259425 10.0.0.4.49951 > httpbin.org.80: [.], seq 3094:4554, ack 1: HTTP
03.735553 10.0.0.4.49951 > httpbin.org.80: [.], seq 3094:4554, ack 1: HTTP

- The communication blackout caused by our iptables rule has started.
  Our TCP has to retransmit the segment multiple times. 
  The RTO value of the socket keeps increasing:
    01.526797: 618ms
    02.259425: 1236ms
    03.735553: 2472ms

06.692867 10.0.0.4.49951 > httpbin.org.80: [.], seq 3094:4554, ack 1: HTTP
06.819115 httpbin.org.80 > 10.0.0.4.49951: [.], ack 4554

- Finally the remote host responds. Our RTO value has increased to 4944ms.
  Karn\'s Algorithm takes effect here: The new RTO value cannot be measured
  with the retransmitted segment, so we skip it.

06.819356 10.0.0.4.49951 > httpbin.org.80: [.], seq 4554:6014, ack 1: HTTP
06.819442 10.0.0.4.49951 > httpbin.org.80: [P.], seq 6014:6182, ack 1: HTTP
06.948678 httpbin.org.80 > 10.0.0.4.49951: [.], ack 6014
06.948917 httpbin.org.80 > 10.0.0.4.49951: [.], ack 6182

- Now we get ACKs on the first try and the network is relatively healthy again.
  Karn\'s Algorithm allows us to measure the new RTO:
    06.948678: 309ms
    06.948917: 309ms
  
06.948942 httpbin.org.80 > 10.0.0.4.49951: [P.], seq 1:26, ack 6182: HTTP 100 Continue
06.949014 httpbin.org.80 > 10.0.0.4.49951: [.], seq 26:1486, ack 6182: HTTP/1.1 200 OK
06.949145 10.0.0.4.49951 > httpbin.org.80: [.], ack 26
06.949816 httpbin.org.80 > 10.0.0.4.49951: [.], seq 1486:2946, ack 6182: HTTP
06.949894 httpbin.org.80 > 10.0.0.4.49951: [.], seq 2946:4406, ack 6182: HTTP
06.950029 10.0.0.4.49951 > httpbin.org.80: [.], ack 2946
06.950030 httpbin.org.80 > 10.0.0.4.49951: [.], seq 4406:5866, ack 6182: HTTP
06.950161 httpbin.org.80 > 10.0.0.4.49951: [P.], seq 5866:6829, ack 6182: HTTP
06.950287 10.0.0.4.49951 > httpbin.org.80: [.], ack 5866
06.950435 10.0.0.4.49951 > httpbin.org.80: [.], ack 6829
06.958155 10.0.0.4.49951 > httpbin.org.80: [F.], seq 6182, ack 6829
07.082998 httpbin.org.80 > 10.0.0.4.49951: [F.], seq 6829, ack 6183
07.083253 10.0.0.4.49951 > httpbin.org.80: [.], ack 6830

- The data communication and connection is finished.
  No significant changes to the RTO measurement occur.
```

## 結論
TCP 中的重傳是穩健實作的重要組成部分。 TCP 必須能夠在不斷變化的網路環境中生存並保持高效能，例如延遲可能突然激增或網路路徑暫時被阻塞。

下次，我們將了解 TCP 擁塞控制，以在不降低網路健康狀況的情況下實現最大效能。

如果您嘗試該項目並提供反饋，我會很高興。請參閱入門了解如何將其與 cURL 和 Firefox 一起使用！

如果您喜歡這篇文章，您可以 與您的追蹤者分享 並 在 Twitter 上關注我！

## 來源
* https://en.wikipedia.org/wiki/Automatic_repeat_request  ↩
* https://tools.ietf.org/html/rfc2018  ↩
* https://www.ietf.org/rfc/rfc793.txt  ↩
* https://en.wikipedia.org/wiki/TCP/IP_Illustration#Volume_1:_The_Protocols  ↩  ↩ 2
* http://ee.lbl.gov/papers/congavoid.pdf  ↩
* https://tools.ietf.org/html/rfc6298  ↩  ↩ 2  ↩ 3
* https://en.wikipedia.org/wiki/Karn%27s_Algorithm  ↩
