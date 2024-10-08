#include "http.h"

#include <thread>
#include <iostream>
#include <sstream>
#include <thread>

HTTP::HTTP(int port) :_port(port)
{
    Init();
}

void HTTP::BeginAccepting()
{
    if (listen(_listen_socket_descriptor, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(_listen_socket_descriptor);
        WSACleanup();
        throw std::exception();
    }
    _acceptingEnable = true;

    std::thread loop(&HTTP::BeginAcceptLoop, this);
    loop.detach();
}

void HTTP::StopAndClean()
{
    _acceptingEnable = false;
    closesocket(_listen_socket_descriptor);
    WSACleanup();
}

void HTTP::RegisterHandler(Methods method, std::string path, HTTPHandler handler)
{
    _handlers.push_back(_HandlerData{ method, path, handler });
}

void HTTP::UnregisterHandler(HTTPHandler handler)
{
    for (std::vector<_HandlerData>::const_iterator It = _handlers.cbegin(); It != _handlers.cend(); ++It)
    {
        if (It->handler == handler)
        {
            _handlers.erase(It);
            return;
        }
    }
}

void HTTP::WaitServerToClose()
{
    while (_acceptingEnable)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void HTTP::Init()
{
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        throw std::exception();
    }

    _listen_socket_descriptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_listen_socket_descriptor == INVALID_SOCKET) {
        WSACleanup();
        throw std::exception();
    }

    sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(_port);

    if (bind(_listen_socket_descriptor, (sockaddr*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        closesocket(_listen_socket_descriptor);
        WSACleanup();
        throw std::exception();
    }
}

void HTTP::BeginAcceptLoop()
{
    while (_acceptingEnable) {
        SOCKET client_socket = accept(_listen_socket_descriptor, NULL, NULL);
        if (client_socket == INVALID_SOCKET) {
            continue;
        }
        std::thread client_handler(&HTTP::RequestHandler, this, static_cast<uintptr_t>(client_socket));
        client_handler.detach();
    }
}

void HTTP::RequestHandler(uintptr_t client_socket)
{
    RequestData requestData;
    ResponseData responseData;

    std::stringstream requestBuffer;
    ReadBuffer(client_socket, requestBuffer);
    ParseBuffer(requestBuffer, requestData);

    HTTPHandler handler = FindHandler(requestData);
    if (handler != nullptr)
        handler(requestData, responseData);
    else
        responseData.code = HTTPCode::NOT_FOUND;

    std::stringstream response;
    BuildResponse(response, responseData);

    std::string data = response.str();
    std::lock_guard<std::mutex> lock(_send_mutex);
    send(client_socket, data.c_str(), data.size(), 0);

    closesocket(client_socket);
}

void HTTP::ReadBuffer(uintptr_t client_socket, std::stringstream& ss)
{
    char buffer[RECV_BUFFER_SIZE];
    int received_bytes;

    std::lock_guard<std::mutex> lock(_recv_mutex);
    do
    {
        received_bytes = recv(client_socket, buffer, RECV_BUFFER_SIZE, 0);
        ss.write(buffer, received_bytes);
    }
    while (received_bytes == RECV_BUFFER_SIZE);
}

void HTTP::ParseBuffer(std::stringstream& ss, RequestData& requestData)
{
    std::string line;

    std::getline(ss, line, ' ');
    requestData.method = GetMethodFromString(line);

    std::getline(ss, line, ' ');
    ParseUrl(line, requestData);

    std::getline(ss, line);

    while (std::getline(ss, line))
    {
        if (line.compare("\r") == 0)
            break;

        size_t pos = line.find(':');
        if (pos != std::string::npos)
        {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);

            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \r\n\t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \r\n\t") + 1);

            requestData.header[key] = value;
        }
    }

    std::ostringstream bodyStream;
    while (std::getline(ss, line))
    {
        bodyStream << line << '\n';
    }
    requestData.body = bodyStream.str();
}

void HTTP::ParseUrl(const std::string& url, RequestData& requestData)
{
    size_t questionPos = url.find('?');
    if (questionPos != std::string::npos)
    {
        requestData.path = url.substr(0, questionPos);
        std::string params = url.substr(questionPos + 1);

        std::stringstream ss(params);
        std::string keyValue;

        while (std::getline(ss, keyValue, '&'))
        {
            size_t equalPos = keyValue.find('=');
            if (equalPos != std::string::npos)
            {
                std::string key = keyValue.substr(0, equalPos);
                std::string value = keyValue.substr(equalPos + 1);
                requestData.urlParams[key] = value;
            }
        }
    }
    else
    {
        requestData.path = url;
    }
}

void HTTP::BuildResponse(std::stringstream& ss, const ResponseData& responseData)
{
    ss << "HTTP/1.1 " << GetHTTPCodeName(responseData.code) << CRLF;
    for (std::map<std::string, std::string>::const_iterator It = responseData.header.cbegin(); It != responseData.header.cend(); ++It)
    {
        ss << It->first << ": " << It->second << CRLF;
    }
    ss << CRLF;
    ss << responseData.bodyContent;
}

HTTPHandler HTTP::FindHandler(const RequestData& requestData)
{
    if (_handlers.empty())
        return nullptr;

    for (std::vector<_HandlerData>::const_iterator It = _handlers.cbegin(); It != _handlers.cend(); ++It)
    {
        if (It->method != requestData.method)
            continue;

        if (requestData.path.compare(It->path) == 0)
            return It->handler;
    }

    return nullptr;
}

std::string HTTP::GetHTTPCodeName(HTTPCode code)
{
    switch (code)
    {
    case SWITCHING_PROTOCOLS:
        return std::string("101 Switching Protocols");
    case OK:
        return std::string("200 OK");
    case NOT_FOUND:
        return std::string("404 Not Found");
    }
}

std::string HTTP::GetMethodName(Methods method)
{
    switch (method)
    {
    case Methods::GET:
        return std::string("GET");
    case Methods::POST:
        return std::string("POST");
    case Methods::PUT:
        return std::string("PUT");
    case Methods::DELETE_METHOD:
        return std::string("DELETE");
    }
}

Methods HTTP::GetMethodFromString(std::string line)
{
    if (line.find("GET")    != std::string::npos) return Methods::GET;
    if (line.find("POST")   != std::string::npos) return Methods::POST;
    if (line.find("PUT")    != std::string::npos) return Methods::PUT;
    if (line.find("DELETE") != std::string::npos) return Methods::DELETE_METHOD;
}