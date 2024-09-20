
#include <WinSock2.h>
#include <string>
#include <map>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

#define CRLF "\r\n"
#define RECV_BUFFER_SIZE 1024

enum HTTPCode
{
    SWITCHING_PROTOCOLS = 101,
    OK = 200,
    NOT_FOUND = 404
};

enum Methods
{
    GET,
    POST,
    PUT,
    DELETE_METHOD
};

struct RequestData
{
    Methods method;
    std::string path;
    std::map<std::string, std::string> urlParams;
    std::map<std::string, std::string> header;
    std::string body;
};

struct ResponseData
{
    HTTPCode code;
    std::map<std::string, std::string> header;
    std::string bodyContent;
};

using HTTPHandler = void(*)(const RequestData&, ResponseData&);

class HTTP
{
private:
    struct _HandlerData
    {
        Methods method;
        std::string path;
        HTTPHandler handler;
    };

    std::vector<_HandlerData> _handlers;
    SOCKET _listen_socket_descriptor;
    int _port;
    bool _acceptingEnable;
public:
    HTTP(int port);

    void BeginAccepting();
    void StopAndClean();

    void RegisterHandler(Methods method, std::string path, HTTPHandler handler);
    void UnregisterHandler(HTTPHandler handler);

    void WaitServerToClose();

private:
    void Init();
    void BeginAcceptLoop();

    void RequestHandler(uintptr_t client_socket);
    void ReadBuffer(uintptr_t client_socket, std::stringstream& ss);
    void ParseBuffer(std::stringstream& ss, RequestData& requestData);
    void ParseUrl(const std::string& url, RequestData& requestData);
    void BuildResponse(std::stringstream& ss, const ResponseData& responseData);

    HTTPHandler FindHandler(const RequestData& requestData);
    std::string GetHTTPCodeName(HTTPCode code);
    std::string GetMethodName(Methods method);
    Methods GetMethodFromString(std::string method);
};