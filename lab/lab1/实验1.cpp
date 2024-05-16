//#include "stdafx.h"//注释掉否则报错
#include <winsock2.h>//winsock2.h要在windows.h之前包含
#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <ws2tcpip.h>
#include <stdbool.h>
#pragma comment(lib,"Ws2_32.lib")
#define MAXSIZE 65507 //发送数据报文的最大长度
#define HTTP_PORT 80 //http 服务器端口
//宏定义
#define my_ip "127.0.0.1"
#define my_port 7890
/*钓鱼网站的源网站*/
#define fish_web "http://today.hit.edu.cn/"
/*钓鱼网站的目的网站*/
#define target_web "jwts.hit.edu.cn"
/*钓鱼网站的目的网站的url*/
#define target_url "http://jwts.hit.edu.cn/"
/*禁止访问的网站*/
#define block_web "http://mail.hit.edu.cn/"
/*是否允许my_ip访问，0代表不允许，1代表允许*/
#define is_permit 1
//Http 重要头部数据
struct HttpHeader{
    char method[4]; // POST 或者 GET，注意有些为 CONNECT，本实验暂不考虑
    char url[1024]; // 请求的 url
    char host[1024]; // 目标主机
    char cookie[1024 * 10]; //cookie
    HttpHeader(){
        ZeroMemory(this,sizeof(HttpHeader));
    }
};
//使用命名空间
using namespace std;
BOOL InitSocket();
void ParseHttpHead(char *buffer,HttpHeader * httpHeader);
BOOL ConnectToServer(SOCKET *serverSocket,char *host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);
//代理相关参数
SOCKET ProxyServer;
SOCKADDR_IN ProxyServerAddr;
const int ProxyPort = my_port;
bool have_cache = false;
bool need_cache = true;
//由于新的连接都使用新线程进行处理，对线程的频繁的创建和销毁特别浪费资源
//可以使用线程池技术提高服务器效率
//const int ProxyThreadMaxNum = 20;
//HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
//DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};
struct ProxyParam{
    SOCKET clientSocket;
    SOCKET serverSocket;
};

/*函数声明*/
BOOL InitSocket();
void ParseHttpHead(char *buffer,HttpHeader * httpHeader);
BOOL ConnectToServer(SOCKET *serverSocket,char *host);
void file_name(char* url, char* filename);
void buide_cache(char* buffer, char* url);
void read_cache(char* buffer, char* filename);
void modify_cache(char* buffer, char* date);
bool get_date(char* buffer, char* field, char* date);

int main(int argc, char* argv[])
{
    printf("代理服务器正在启动\n");
    printf("初始化...\n");
    if(!InitSocket()){
        printf("socket 初始化失败\n");
        return -1;
    }
    printf("代理服务器正在运行，监听端口 %d\n",ProxyPort);
    SOCKET acceptSocket = INVALID_SOCKET;//调用 InitSocket 函数来初始化套接字库，创建代理服务器的套接字，并绑定端口。
    ProxyParam* lpProxyParam;
    HANDLE hThread;
    DWORD dwThreadID;
    //代理服务器不断监听
    while(true){
start:
        acceptSocket = accept(ProxyServer,NULL,NULL);//调用 accept 函数接收客户端的连接请求。接收到的连接请求对应的客户端套接字为 acceptSocket
        lpProxyParam = new ProxyParam;//c++下使用new申请空间
        if(lpProxyParam == NULL){
            continue;
        }
        SOCKADDR_IN acceptAddr;
        int addrlen = sizeof(SOCKADDR);
        acceptSocket = accept(ProxyServer, (SOCKADDR*)&acceptAddr, &addrlen);//再次调用 accept 函数接收客户端的连接请求，这次会获取客户端的地址信息。
        printf("\n用户IP地址为%s\n", inet_ntoa(acceptAddr.sin_addr));
        //用户过滤
        switch (is_permit)
        {
        case 0:
            if(strcmp(inet_ntoa(acceptAddr.sin_addr), my_ip) == 0){
              printf("您的主机已被屏蔽！\n");
              goto start;
            }
            break;
        case 1:
            break;
        }  
        lpProxyParam->clientSocket = acceptSocket;//将接收到的客户端套接字 acceptSocket 存储到 lpProxyParam 结构体中。
        hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread,(LPVOID)lpProxyParam, 0, 0);//创建一个新的线程来处理这个客户端的连接请求，线程的主体函数为 ProxyThread，并将 lpProxyParam 作为参数传递给 ProxyThread 函数。
        CloseHandle(hThread);
        Sleep(200);//让当前线程暂停 200 毫秒，为其他线程提供执行的机会。
    }
    closesocket(ProxyServer);
    WSACleanup();//当代理服务器需要关闭时，关闭代理服务器的套接字，并清理套接字库。
    return 0;
}
//************************************
// Method: InitSocket
// FullName: InitSocket
// Access: public 
// Returns: BOOL
// Qualifier: 初始化套接字
//************************************
BOOL InitSocket(){
    //加载套接字库（必须）
    WORD wVersionRequested;
    WSADATA wsaData;
    //套接字加载时错误提示
    int err;
    //版本 2.2
    wVersionRequested = MAKEWORD(2, 2);
    //加载 dll 文件 Scoket 库
    err = WSAStartup(wVersionRequested, &wsaData);
    if(err != 0){
    //找不到 winsock.dll
        printf("加载 winsock 失败，错误代码为: %d\n", WSAGetLastError());
        return FALSE;
    }
    if(LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) !=2) 
    {
        printf("不能找到正确的 winsock 版本\n");
        WSACleanup();
        return FALSE;
    }
    ProxyServer= socket(AF_INET, SOCK_STREAM, 0);//调用 socket 函数创建一个 TCP 套接字，用于监听客户端的连接请求。
    if(INVALID_SOCKET == ProxyServer){
        printf("创建套接字失败，错误代码为：%d\n",WSAGetLastError());
        return FALSE;
    }
    ProxyServerAddr.sin_family = AF_INET;//设置代理服务器的地址族为 AF_INET，表示使用 IPv4 地址。
    ProxyServerAddr.sin_port = htons(ProxyPort);//指向网络字节顺序的端口号
    ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;//设置代理服务器的 IP 地址为 INADDR_ANY，表示接收所有的客户端连接请求。
    if(bind(ProxyServer,(SOCKADDR*)&ProxyServerAddr,sizeof(SOCKADDR)) == SOCKET_ERROR){
        printf("绑定套接字失败\n");
        return FALSE;
    }
    if(listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR){
        printf("监听端口%d 失败",ProxyPort);
        return FALSE;
    }
        return TRUE;
}
//************************************
// Method: ProxyThread
// FullName: ProxyThread
// Access: public 
// Returns: unsigned int __stdcall
// Qualifier: 线程执行函数
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter){
    char Buffer[MAXSIZE];//存储报文
    char fill_buffer[MAXSIZE];//存储缓存数据
    char* CacheBuffer;//存储cache缓存
    char* DataBuffer;//为构建date字段创建缓存
    ZeroMemory(Buffer,MAXSIZE);//初始化
    SOCKADDR_IN clientAddr;//存储客户端地址
    int length = sizeof(SOCKADDR_IN);
    int recvSize;
    int ret;
    FILE* file;//
    char field[] = "Date";//下一步捕捉date字段使用
    char data_save[30];//保存字段date的值
    ZeroMemory(data_save, 30);
    ZeroMemory(fill_buffer, MAXSIZE);//初始化
    char filename[100];//存储缓存文件名字
    ZeroMemory(filename, 100);

    recvSize = recv(((ProxyParam *)lpParameter)->clientSocket,Buffer,MAXSIZE,0);//从网页截获报文存储至buffer中
    HttpHeader* httpHeader = new HttpHeader;//创建一个 HttpHeader 结构体，用于存储 HTTP 头部信息。
    if(recvSize <= 0){
        goto error;
    }//goto语句后不能再声明变量
    //printf("\n======截获的报文数据是======\n%s\n",Buffer);

    CacheBuffer = new char[recvSize + 1];
    ZeroMemory(CacheBuffer,recvSize + 1);
    memcpy(CacheBuffer,Buffer,recvSize);//将从报文读到的内容写入我自己的缓存中
    ParseHttpHead(CacheBuffer,httpHeader);//解析报文
    // printf("\n======httpHeader是======\n%s\n",httpHeader);
    // printf("\n======CacheBuffer是======\n%s\n",CacheBuffer);
    DataBuffer = new char[recvSize + 1];
    ZeroMemory(DataBuffer,recvSize + 1);

    file_name(httpHeader->url, filename);//构造文件名

    //更新缓存
    if((file = fopen(filename, "rb") )!= NULL){
        printf("\n代理服务器在该url下有相应缓存！\n");
		fread(fill_buffer, sizeof(char), MAXSIZE, file);
		fclose(file);
		get_date(fill_buffer, field, data_save);
		printf("date_str: %s\n", data_save);
		modify_cache(Buffer, data_save);
		printf("\n======改造后的请求报文======\n%s\n", Buffer);
		have_cache = true;
    }
    //禁止访问的网站
    if(strstr(httpHeader->url, block_web) != NULL)
    {
        printf("禁止访问\n");
        goto error;
    }

    //钓鱼网站
    if(strstr(httpHeader->url, fish_web) != NULL)
    {
        printf("钓鱼网站\n");
        printf("钓鱼成功：您所前往的%s已被引导至%s\n",fish_web,target_web);
		memcpy(httpHeader->host, target_web, strlen(target_web)+1);
		printf("host: %s\n", httpHeader->host);
		memcpy(httpHeader->url, target_url, strlen(target_url)+1);
		printf("url: %s\n", httpHeader->url);
        have_cache = false;
    }
    
    delete CacheBuffer;
    if(!ConnectToServer(&((ProxyParam *)lpParameter)->serverSocket,httpHeader->host)) {
        goto error;
    }
    printf("代理连接主机 %s 成功\n",httpHeader->host);
    //将客户端发送的 HTTP 数据报文直接转发给目标服务器
    ret = send(((ProxyParam *)lpParameter)->serverSocket,Buffer,strlen(Buffer) + 1,0);
    // printf("\n======发送的请求报文是======\n%s\n",Buffer);

    //等待目标服务器返回数据
    recvSize = recv(((ProxyParam *)lpParameter)->serverSocket,Buffer,MAXSIZE,0);
    // printf("\n======返回的请求报文是======\n%s\n",Buffer);

    if(recvSize <= 0){
        goto error;
    }
    printf("have_cache: %d\n", have_cache);
    printf("need_cache: %d\n", need_cache);
    //有缓存时，判断返回的状态码是否为304，是则将缓存数据发送给客户端
    // printf("\n======代理服务器接收到数据======\n%s\n",Buffer);
    printf("网站url是：%s\n", httpHeader->url);
    if(have_cache == true){
        // printf("\n===============缓存读取成功===============\n");
        read_cache(Buffer, httpHeader->url);//读取缓存数据
        // goto send;
        // need_cache = false;
    }
    if(need_cache == true){
        buide_cache(Buffer, httpHeader->url);//将数据写入缓存
    }
    //将目标服务器返回的数据直接转发给客户端
    ret = send(((ProxyParam *)lpParameter)->clientSocket,Buffer,sizeof(Buffer),0);
    // printf("\n======客户端接收到数据======\n%s\n",Buffer);
    //错误处理
    error:
        printf("关闭套接字\n");
        Sleep(200);
        closesocket(((ProxyParam*)lpParameter)->clientSocket);
        closesocket(((ProxyParam*)lpParameter)->serverSocket);
        free(lpParameter);
        _endthreadex(0);
        return 0;
}
//************************************
// Method: ParseHttpHead
// FullName: ParseHttpHead
// Access: public 
// Returns: void
// Qualifier: 解析 TCP 报文中的 HTTP 头部
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char *buffer,HttpHeader * httpHeader){
    char *p;
    char *ptr;
    const char * delim = "\r\n";//分隔符，处理是跳过
    p = strtok_s(buffer,delim,&ptr);//提取第一行
    printf("%s\n",p);
    if(p[0] == 'G'){//GET 方式
        memcpy(httpHeader->method,"GET",3);
        memcpy(httpHeader->url,&p[4],strlen(p) -13);//填充httpHeader结构体
    }
    else if(p[0] == 'P'){//POST 方式
        memcpy(httpHeader->method,"POST",4);
        memcpy(httpHeader->url,&p[5],strlen(p) - 14);
    }
    printf("%s\n",httpHeader->url);
    p = strtok_s(NULL,delim,&ptr);//提取第二行
    while(p){
        switch(p[0]){
            case 'H'://Host
                memcpy(httpHeader->host,&p[6],strlen(p) - 6);//主机名
                break;
            case 'C'://Cookie
                if(strlen(p) > 8){
                    char header[8];
                    ZeroMemory(header,sizeof(header));
                    memcpy(header,p,6);//提取cookie
                    if(!strcmp(header,"Cookie")){
                        memcpy(httpHeader->cookie,&p[8],strlen(p) -8);
                    }
                }
                break;
            default:
                break;
        }
        p = strtok_s(NULL,delim,&ptr);//继续提取
    }
}
//************************************
// Method: ConnectToServer
// FullName: ConnectToServer
// Access: public 
// Returns: BOOL
// Qualifier: 根据主机创建目标服务器套接字，并连接
// Parameter: SOCKET * serverSocket
// Parameter: char * host
//************************************
BOOL ConnectToServer(SOCKET *serverSocket,char *host){
    sockaddr_in serverAddr;//存储目标服务器的地址信息。
    serverAddr.sin_family = AF_INET;//设置地址族为 AF_INET，表示使用 IPv4 地址。
    serverAddr.sin_port = htons(HTTP_PORT);//设置端口号为 HTTP 服务的端口，htons 函数用于将主机字节序转换为网络字节序。
    HOSTENT *hostent = gethostbyname(host);//调用 gethostbyname 函数获取主机的信息，返回一个 HOSTENT 结构体指针。
    if(!hostent){
        return FALSE;
    }
    in_addr Inaddr=*( (in_addr*) *hostent->h_addr_list);//获取主机的 IP 地址。
    serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));//将 IP 地址的格式从点分十进制格式转换为网络字节序格式，并设置为目标服务器的 IP 地址。
    *serverSocket = socket(AF_INET,SOCK_STREAM,0);//调用 socket 函数创建一个套接字。
    if(*serverSocket == INVALID_SOCKET){
        return FALSE;
    }
    if(connect(*serverSocket,(SOCKADDR *)&serverAddr,sizeof(serverAddr)) == SOCKET_ERROR){//调用 connect 函数连接到目标服务器
        closesocket(*serverSocket);
        return FALSE;
    }
    return TRUE;
}
/*
函数名：file_name
输入：网站url，文件指针
输出：空
功能：根据url编辑保存文件的文件名
*/
void file_name(char* url, char* filename){
	int count = 0;//计算已经提取的字符数量。
	while (*url != '\0') {
		if ((*url >= 'a' && *url <= 'z') || (*url >= 'A' && *url <= 'Z') || (*url >= '0' && *url <= '9')) {
			*filename++ = *url;
			count++;//如果当前字符是字母或数字，将其添加到文件名中，并增加计数器
		}
		if (count >= 95)//文件名长度限制为95个字符
			break;
		url++;
	}
    *filename = '\0';  // 添加字符串结束符
}

/*
    函数名：build_cache
    输入：缓冲区，网站url
    输出：空
    功能：构造cache,即读取报文并写入文件
*/
void buide_cache(char* buffer, char* url){
    char* p;
    char* ptr;
    char* temp_buffer;
    char num[10];
    temp_buffer = (char*)malloc(MAXSIZE*sizeof(char));
    ZeroMemory(temp_buffer, MAXSIZE);
    ZeroMemory(num, 10);
    memcpy(temp_buffer, buffer, strlen(buffer));
    p = strtok_s(temp_buffer, "\r\n", &ptr);
    if(strstr(p, "200") != NULL){//状态码为200时需要进行缓存/更新
        have_cache = true;
        char filename[100];
        file_name(url, filename);
        FILE* file;
        errno_t err = fopen_s(&file, filename, "wb");//打开文件,记录错误信息
        // if (err || file == NULL) {
        //     printf("\nCannot open file %s. Error code: %d\n", filename, err);
        //     return;
        // }
        fwrite(buffer, sizeof(char), strlen(buffer), file);
        fclose(file);
    }
    printf("\n===============缓存构建成功===============\n");
}

/*
    函数名：read_cache
    输入：缓冲区，文件名
    输出:空
    功能：从cache中获得报文，即从文件中读数据
*/
void read_cache(char* buffer, char* filename){
    char* p;
    char* ptr;
    char* temp_buffer;
    char num[10];
    temp_buffer = (char*)malloc(MAXSIZE*sizeof(char));
    ZeroMemory(temp_buffer, MAXSIZE);
    ZeroMemory(num, 10);
    memcpy(temp_buffer, buffer, strlen(buffer));
    p = strtok_s(temp_buffer, "\r\n", &ptr);
    if(strstr(p, "304")!= NULL){//状态码为304时可以直接读取缓存数据而不必建立网络连接
        FILE* file ;
        fopen_s(&file, filename, "rb");
        // if (file == NULL) {
        //     perror("文件打开失败\n");
        //     return;
        // }
        fread(buffer, sizeof(char), MAXSIZE, file);
        fclose(file);
        need_cache = false;//从缓存中读取数据，不需要再次缓存
        printf("\n===============缓存读取成功===============\n");
    }
}

/*
    函数名：modify_cache
    输入：字段名，保存字段值的指针
    输出：空
    功能：用于第二次访问网站且无需修改时，这时直接从缓存中读取文件，需要向其插入If-Modified-Since字段
*/
void modify_cache(char* buffer, char* date){
    const char* field = "Host";
    const char* newfield = "If-Modified-Since: ";
    //const char *delim = "\r\n";
    char temp[MAXSIZE];
    ZeroMemory(temp, MAXSIZE);
    char* pos = strstr(buffer, field);//获取请求报文段中Host后的部分信息
    int i = 0;
    for (i = 0; i < strlen(pos); i++) {
        temp[i] = pos[i];//将pos复制给temp
    }
    *pos = '\0';
    while (*newfield != '\0') {  //插入If-Modified-Since字段
        *pos++ = *newfield++;
    }
    while (*date != '\0') {//插入对象文件的最新被修改时间
        *pos++ = *date++;
    }
    *pos++ = '\r';
    *pos++ = '\n';
    for (i = 0; i < strlen(temp); i++) {
        *pos++ = temp[i];
    }
}

/*
    函数名：get_date
    输入：缓冲区，字段名，保存字段值的指针
    输出：布尔值，成功获得则返回true
    功能：分析http报文头部的field字段，获取时间
*/
bool get_date(char* buffer, char* field, char* date){
    char* p;
    char* ptr;
    char* temp_buffer;
    char* temp;
    temp_buffer = (char*)malloc(MAXSIZE*sizeof(char));//为临时缓冲区分配内存
    ZeroMemory(temp_buffer, MAXSIZE);
    ZeroMemory(date, 30);//初始化
    memcpy(temp_buffer, buffer, strlen(buffer));//将buffer中的内容复制到temp_buffer中
    p = strtok_s(temp_buffer, "\r\n", &ptr);//分割报文,获取第一行
    while(p != NULL){
        if(strstr(p, field) != NULL){//使用strstr函数查找field字段，即查找date字段
            memcpy(date, &p[strlen(field) + 2], strlen(p) - strlen(field) + 2);//获取时间
            return true;
        }
        p = strtok_s(NULL, "\r\n", &ptr);//继续分割
    }
    return false;
}
