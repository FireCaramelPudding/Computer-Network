#include <stdlib.h>
#include <time.h>
#include <WinSock2.h>
#include <fstream>
#include <iostream>
#include <cmath>
#include <error.h>
using namespace std;

#pragma comment(lib,"ws2_32.lib")

#define SERVER_PORT 12340       //端口号
#define SERVER_IP "0.0.0.0"     //IP 地址
const int BUFFER_LENGTH = 1026; //缓冲区大小，（以太网中 UDP 的数据 帧中包长度应小于 1480 字节）
const int SEND_WIND_SIZE = 10;  //发送窗口大小为 10，GBN 中应满足 W + 1 <= N（W 为发送窗口大小，N 为序列号个数）
//本例取序列号 0...19 共 20 个
//如果将窗口大小设为 1，则为停-等协议
const int SEQ_SIZE = 20; //序列号的个数，从 0~19 共计 20 个
//由于发送数据第一个字节如果值为 0，则数据会发送失败
//因此接收端序列号为 1~20，与发送端一一对应

BOOL ack[SEQ_SIZE]; // 收到 ack 情况，对应 0~19 的 ack
char dataBuffer[SEQ_SIZE][BUFFER_LENGTH];
int curSeq;         // 当前数据包的 seq
int curAck;         // 当前等待确认的 ack
int totalSeq;       // 收到的包的总数
int totalPacket;    // 需要发送的包总数
int totalAck;       // 确认收到（ack）的包的总数

//************************************
// Method: getCurTime
// FullName: getCurTime
// Access: public
// Returns: void
// Qualifier: 获取当前系统时间，结果存入 ptime 中
// Parameter: char * ptime
//************************************
void getCurTime(char *ptime)
{
    char buffer[128];
    memset(buffer, 0, sizeof(buffer));
    time_t c_time;
    struct tm *p;
    time(&c_time);
    p = localtime(&c_time);
    sprintf(buffer, "%d/%d/%d %d:%d:%d",
            p->tm_year + 1900,
            p->tm_mon + 1,
            p->tm_mday,
            p->tm_hour,
            p->tm_min,
            p->tm_sec);
    strcpy(ptime, buffer);
}

//************************************
// Method:    lossInLossRatio
// FullName:  lossInLossRatio
// Access:    public
// Returns:   BOOL
// Qualifier: 根据丢失率随机生成一个数字，判断是否丢失,丢失则返回 TRUE，否则返回 FALSE
// Parameter: float lossRatio [0,1]
//************************************
BOOL lossInLossRatio(float lossRatio)
{
    int lossBound = (int)(lossRatio * 100);
    int r = rand() % 101;
    if (r <= lossBound)
    {
        return TRUE;
    }
    return FALSE;
}

/*输入：接收到的序列号
输出：判断这个序列号是否是所需的，布尔变量
*/
BOOL seqRecvAvailable(int recvSeq)
{
	int step;
	int index;
	index = recvSeq - 1;
	step = index - curAck;
	step = step >= 0 ? step : step + SEQ_SIZE;/*为step循环取模*/
	//序列号是否在当前接收窗口之内
	if (step >= SEND_WIND_SIZE)
	{
		return FALSE;
	}
	return TRUE;
}
//************************************
// Method: seqIsAvailable
// FullName: seqIsAvailable
// Access: public
// Returns: unsigned int
// Qualifier: 当前序列号 curSeq 是否可用
//************************************
unsigned int seqIsAvailable()/*此时任意一个seq有三种状态：未发送-0；已发送未确认-1；已确认-2*/
{
    int step;
    step = curSeq - curAck;
    if(step < 0)
    {
        step += SEQ_SIZE;
    }
    //序列号是否在当前发送窗口之内
    if (step >= SEND_WIND_SIZE)
    {
        return 0;/*超出窗口，未发送*/
    }
    if (!ack[curSeq])
    {
        return 1;/*已发送还未确认*/
    }
    return 2;/*已确认*/
}

//************************************
// Method: timeoutHandler
// FullName: timeoutHandler
// Access: public
// Returns: void
// Qualifier: 超时重传处理函数，只重传未收到的包
//************************************
void timeoutHandler(SOCKET socketServer, SOCKADDR_IN addrClient)
{
    printf("******Time out\n");

    // // 遍历发送窗口内的所有数据包
    // for (int i = 0; i < SEND_WIND_SIZE; ++i)
    // {
        int index = curAck  % SEQ_SIZE;

        // 如果数据包未被确认，则重发
        if (ack[index] == false)
        {
            printf("*****Resend Packet %d\n\n", index + 1);

            // 重发数据包
            char resendBuffer[BUFFER_LENGTH];
            resendBuffer[0] = index + 1; // 序列号从1开始
            memcpy(&resendBuffer[1], dataBuffer[index], BUFFER_LENGTH - 1); // 复制数据包内容

            sendto(socketServer, resendBuffer, BUFFER_LENGTH, 0, (SOCKADDR *)&addrClient, sizeof(SOCKADDR));
        }
    // }
    curSeq = curAck;
}


//************************************
// Method: ackHandler
// FullName: ackHandler
// Access: public
// Returns: void
// Qualifier: 收到 ack，累积确认，取数据帧的第一个字节
//由于发送数据时，第一个字节（序列号）为 0（ASCII）时发送失败，因此加一了，此处需要减一还原
//************************************
void ackHandler(char c)
{
	unsigned char index = (unsigned char)c - 1;	 //序列号减一
	printf("Recv a ack of seq %d\n", index + 1); //从接收方收到的确认收到的序列号
	int next;

	if (curAck == index)
	{
		totalAck += 1;
		ack[index] = FALSE;
		curAck = (index + 1) % SEQ_SIZE;
        printf("\n###########Windows move to %d##############\n", curAck + 1);
		for (int i = 1; i < SEQ_SIZE; i++)
		{
			next = (i + index) % SEQ_SIZE;
			if (ack[next] == TRUE)
			{
				ack[next] = FALSE;
				curAck = (next + 1) % SEQ_SIZE;
				totalSeq++;
				curSeq++;
				curSeq %= SEQ_SIZE;
			}
			else
			{
				break;
			}
		}
	}
	else if (curAck < index && index - curAck + 1 <= SEND_WIND_SIZE)
	{ //要保证是要接受的消息（在滑动窗口内）
		if (!ack[index])
		{
			totalAck += 1;
			ack[index] = TRUE;
		}
	}
}


//主函数
int main(int argc, char *argv[])
{
    //加载套接字库（必须）
    WORD wVersionRequested;
    WSADATA wsaData;
    //套接字加载时错误提示
    int err;
    //版本 2.2
    wVersionRequested = MAKEWORD(2, 2);
    //加载 dll 文件 Scoket 库
    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0)
    {
        //找不到 winsock.dll
        printf("WSAStartup failed with error: %d\n", err);
        return -1;
    }
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
    {
        printf("Could not find a usable version of Winsock.dll\n");
        WSACleanup();
    }
    else
    {
        printf("The Winsock 2.2 dll was found okay\n");
    }
    SOCKET socketServer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    //设置套接字为非阻塞模式
    int iMode = 1;                                          //1：非阻塞，0：阻塞
    ioctlsocket(socketServer, FIONBIO, (u_long FAR *)&iMode); //非阻塞设置
    SOCKADDR_IN addrServer;                                 //服务器地址
    //addrServer.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);
    addrServer.sin_addr.S_un.S_addr = htonl(INADDR_ANY); //两者均可
    addrServer.sin_family = AF_INET;
    addrServer.sin_port = htons(SERVER_PORT);
    err = bind(socketServer, (SOCKADDR *)&addrServer, sizeof(SOCKADDR));
    if (err)
    {
        err = GetLastError();
        printf("Could not bind the port %d for socket.Error code is %d\n", SERVER_PORT, err);
        WSACleanup();
        return -1;
    }

    /*双向传输的丢包率*/
    float packetLossRatio = 0.2; //默认包丢失率 0.2
    float ackLossRatio = 0.2;    //默认 ACK 丢失率 0.2
    //用时间作为随机种子，放在循环的最外面
    srand((unsigned)time(NULL));

    SOCKADDR_IN addrClient; //客户端地址
    int length = sizeof(SOCKADDR);
    char buffer[BUFFER_LENGTH]; //数据发送接收缓冲区
    ZeroMemory(buffer, sizeof(buffer));
    //将测试数据读入内存
    std::ifstream icin;
    icin.open("test2.txt");
    icin.seekg(0, ios::end);
    int fileSize = (int)icin.tellg();
    icin.seekg(0, ios::beg);
    char data[fileSize + 1];
    icin.read(data, fileSize);
    data[fileSize] = 0;
    icin.close();
    totalPacket = ceil(sizeof(data) / 1024.0);
    printf("totalPacket is: %d\n\n", totalPacket);

    int recvSize;

    //初始化ack
    for (int i = 0; i < SEQ_SIZE; i++)
    {
        ack[i] = FALSE;
    }
    while (true)
    {
        //非阻塞接收，若没有收到数据，返回值为-1
        recvSize = recvfrom(socketServer, buffer, BUFFER_LENGTH, 0, ((SOCKADDR *)&addrClient), &length);
        if (recvSize < 0)
        {
            Sleep(200);
            continue;
        }
        else if (strcmp(buffer, "Data Transfer Is Complete\n") == 0)
        {
            printf("Data Transfer Is Complete!\n");
            ZeroMemory(buffer, sizeof(buffer));
            continue;
        }
        printf("recv from client: %s\n", buffer);

        char cmd[128];
        sscanf(buffer, "%s%f%f", &cmd, &packetLossRatio, &ackLossRatio);

        if (!strcmp(buffer, "-time") )
        {
            getCurTime(buffer);
        }
        else if (!strcmp(buffer, "-quit") )
        {
            strcpy(buffer, "Good bye!");
        }
        else if (!strcmp(buffer, "-testsr") )
        {
            //进入 sr 测试阶段
            //首先 server（server 处于 0 状态）向 client 发送 205 状态码（server 进入 1 状态）
            //server 等待 client 回复 200 状态码，如果收到（server 进入 2 状态）， 则开始传输文件，否则延时等待直至超时
            //在文件传输阶段，server 发送窗口大小设为
            for (int i = 0; i < SEQ_SIZE; ++i)
			{
				ack[i] = FALSE;
			}
            ZeroMemory(buffer, sizeof(buffer));
            int recvSize;
            int waitCount[20] = {0};
            printf("Begin to test SR protocol, please don't abort the process\n");
            //加入了一个握手阶段
            //首先服务器向客户端发送一个 205 大小的状态码（我自己定义的） 表示服务器准备好了，可以发送数据
            //客户端收到 205 之后回复一个 200 大小的状态码，表示客户端准备好了，可以接收数据了
            //服务器收到 200 状态码之后，就开始使用 SR 发送数据了
            printf("Shake hands stage\n");
            int stage = 0;
            bool runFlag = true;
            while (runFlag)
            {
                switch (stage)
                {
                case 0: //发送 205 阶段
                    buffer[0] = 205;
                    sendto(socketServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR *)&addrClient, sizeof(SOCKADDR));
                    Sleep(100);
                    stage = 1;
                    break;
                case 1: //等待接收 200 阶段，没有收到则计数器+1，超时则放弃此次“连接”，等待从第一步开始
                    recvSize = recvfrom(socketServer, buffer, BUFFER_LENGTH, 0, ((SOCKADDR *)&addrClient), &length);
                    if (recvSize < 0)
                    {
                        waitCount[0]++;
                        if (waitCount[0] > 20)
                        {
                            runFlag = false;
                            printf("200 Timeout error\n");
                            break;
                        }
                        Sleep(500);
                        continue;
                    }
                    else
                    {
                        
                        if ((unsigned char)buffer[0] == 200)
                        {
                            printf("Begin a file transfer\n");
                            printf("File size is %dB, each packet is 1024B and packet total num is %d...\n\n", sizeof(data), totalPacket);
                            curSeq = 0;
                            curAck = 0;
                            totalSeq = 0;
                            waitCount[0] = 0;
                            totalAck = 0;
                            stage = 2;
                        }
                    }
                    break;
                case 2: //数据传输阶段

                    /*为判断数据传输是否完成添加或修改的语句*/
                    if (seqIsAvailable()==1 && totalSeq <= (totalPacket - 1))//totalSeq<=(totalPacket-1)：未传到最后一个数据包
                    {
                        //发送给客户端的序列号从 1 开始
                        buffer[0] = curSeq+1;
                        ack[curSeq] = FALSE;
                        //数据发送的过程中应该判断是否传输完成
                        memcpy(&buffer[1], data + 1024 * totalSeq, 1024);
                        printf("send a packet with a seq of %d\n", curSeq+1);
                        sendto(socketServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR *)&addrClient, sizeof(SOCKADDR));
                        curSeq++;
                        curSeq %= SEQ_SIZE;
                        totalSeq++;
                        printf("totalSeq now is: %d\n", totalSeq);
                        Sleep(500);
                    }
                    else if (seqIsAvailable() == 2 && totalSeq <= (totalPacket - 1))
					{
						curSeq++;
						curSeq %= SEQ_SIZE;
						totalSeq++;
						break;
					}
                    // 等待 Ack，若没有收到，则返回值为-1，计数器+1
                    // 无论有没有收到ACK，都会继续传数据
                    recvSize = recvfrom(socketServer, buffer, BUFFER_LENGTH, 0, ((SOCKADDR *)&addrClient), &length);
                    if (recvSize < 0)
                    {
                        waitCount[curSeq]++;
                        // 20 次等待 ack 则超时重传
                        if (waitCount[curSeq] > 20)
                        {
                            timeoutHandler(socketServer,addrClient);
                            waitCount[curSeq] = 0;
                           
                        }
                    }
                    else
                    {
                        //收到 ack
                        ackHandler(buffer[0]);
                        waitCount[curAck] = 0;
                        if (totalAck == totalPacket)
						{ //数据传输完成
                            sendto(socketServer, "Data Transfer Is Complete\n", 27, 0, (SOCKADDR *)&addrClient, sizeof(SOCKADDR));
							runFlag = false;
							break;
						}
                    }
                    Sleep(500);
                    break;
                }
            }
        }
        /* 双向数据传输 */
        else if (!strcmp(buffer, "-test2sr"))
        {
            ZeroMemory(dataBuffer, sizeof(dataBuffer));
           //TODO:初始化接收窗口
            for (int i = 0; i < SEQ_SIZE; ++i)
			{
				ack[i] = FALSE;
			}
            printf("Begin to test SR protocol, please don't abort the process\n");
            printf("The loss ratio of packet is %.2f, the loss ratio of ack is %.2f\n", packetLossRatio, ackLossRatio);
            int stage = 0;
            BOOL bao;
            curAck = 0;
            unsigned short seq;     //包的序列号
            unsigned short recvSeq; //接收窗口大小为 1，已确认的序列号
            unsigned short waitSeq; //等待的序列号
            int next;
            sendto(socketServer, "-test2sr", strlen("-test2sr") + 1, 0, (SOCKADDR *)&addrClient, sizeof(SOCKADDR));
            while (true)
            {
                int ret =0;
                //等待 server 回复设置 UDP 为阻塞模式
                ret = recvfrom(socketServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR *)&addrClient, &length);
                // if (ret < 0)
                // {
                //     printf("ret is minus!\n");
                //     Sleep(200);
                //     continue;
                // }
                if (strcmp(buffer, "Data Transfer Is Complete\n") == 0)
                {
                    printf("Data Transfer Is Complete!\n");
                    break;
                }
                
                switch (stage)
                {
                case 0: //等待握手阶段
                    if ((unsigned char)buffer[0] == 205)
                    {
                        printf("Ready for file transmission\n");
                        buffer[0] = 200;
                        buffer[1] = '\0';
                        sendto(socketServer, buffer, 2, 0, (SOCKADDR *)&addrClient, sizeof(SOCKADDR));
                        stage = 1;
                        recvSeq = 0;
                    }
                    break;
                case 1: //等待接收数据阶段
                    // printf("buffer[0] is %d\n", (unsigned char)buffer[0]);
                    // printf("buffer[1] is %s\n", buffer[1]);
                    seq = (unsigned short)buffer[0];
                    //随机法模拟包是否丢失
                    bao = lossInLossRatio(packetLossRatio);
                    // 包丢失
                    if (bao == 1)
                    {
                        printf("The packet with a seq of %d loss\n", seq );
                        break;
                    }
                    // 包没有丢失
                    printf("recv a packet with a seq of %d\n", seq );
                    // 打印接收到的数据
                

                    //TODO:接收到的序列号是否在接收窗口内
                    if (seqRecvAvailable(seq))
					{
						recvSeq = seq;
						ack[seq - 1] = TRUE;
                        strcpy(dataBuffer[seq - 1], &buffer[1]);
                        printf("************Received data**********\n");
                        printf("%s\n", dataBuffer[seq - 1]);
                        printf("***********************************\n");
						buffer[0] = recvSeq;
						buffer[1] = '\0';
						int tempt = curAck;
						if (seq - 1 == curAck)
						{
							for (int i = 0; i < SEQ_SIZE; i++)
							{
								next = (tempt + i) % SEQ_SIZE;
								if (ack[next])
								{
									curAck = (next + 1) % SEQ_SIZE;
									ack[next] = FALSE;

								}
								else
								{
									break;
								}
							}
						}

					}
                    else
                    {
                        recvSeq = seq;
						buffer[0] = recvSeq;
						buffer[1] = '\0';
                    }
                    bao = lossInLossRatio(ackLossRatio);
                    if (bao == 1)
                    {
                        printf("The ack of %d loss\n", (unsigned char)buffer[0] );
                        continue;
                    }
                    sendto(socketServer, buffer, 2, 0, (SOCKADDR *)&addrClient, sizeof(SOCKADDR));
                    printf("send a ack of %d\n", (unsigned char)buffer[0]);
                    break;
                }
                Sleep(500);
            }
            iMode = 1;                                              //1：非阻塞，0：阻塞
            ioctlsocket(socketServer, FIONBIO, (u_long FAR *)&iMode); //非阻塞设置
        }

        sendto(socketServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR *)&addrClient, sizeof(SOCKADDR));
        Sleep(500);
    }
    //关闭套接字，卸载库
    closesocket(socketServer);
    WSACleanup();
    return 0;
}