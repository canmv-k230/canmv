#配置 tcp/udp socket调试工具
import socket
import time
import network


def udpserver():
    #获取lan接口
    a=network.LAN()
    if(a.active()):
        a.active(0)
    a.active(1)
    a.ifconfig("dhcp")
    ip = a.ifconfig()[0]
    print(a.ifconfig())
        
    #获取地址及端口号对应地址
    ai = socket.getaddrinfo(ip, 8080)
    #ai = socket.getaddrinfo("10.10.1.94", 60000)
    print("Address infos:", ai)
    addr = ai[0][-1]

    print("udp server %s port:%d\n" % ((network.LAN().ifconfig()[0]),8080))
    #建立socket
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    #设置属性
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    #绑定
    s.bind(addr)
    print("a")
    #延时
    time.sleep(1)

    for j in range(10):
        try:
            #接受内容
            data, addr = s.recvfrom(800)
            print("b")
        except:
            continue
        #打印内容
        print("recv %d" % j,data,addr)
        #回复内容
        s.sendto(b"%s have recv count=%d " % (data,j), addr)
    #关闭
    s.close()
    print("udp server exit!!")




#main()
udpserver()
