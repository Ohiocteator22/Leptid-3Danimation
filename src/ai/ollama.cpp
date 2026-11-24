// Leptid — Ollama client implementation (minimal HTTP/1.1 over raw TCP).
// POSIX sockets everywhere; Winsock2 on Windows (same API surface, small shim).
#include "ai/ollama.h"
#include "formats/json.h"
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace leptid {

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
const SocketHandle kInvalidSock = INVALID_SOCKET;
static bool winsockReady() { // once per process; WSACleanup not needed for our lifetime
    static bool ok = [] {
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    return ok;
}
static void sockClose(SocketHandle s) { closesocket(s); }
static long long sockSend(SocketHandle s, const char* b, size_t n) {
    return (long long)send(s, b, (int)n, 0);
}
static long long sockRecv(SocketHandle s, char* b, size_t n) {
    return (long long)recv(s, b, (int)n, 0);
}
#else
using SocketHandle = int;
const SocketHandle kInvalidSock = -1;
static void sockClose(SocketHandle s) { close(s); }
static long long sockSend(SocketHandle s, const char* b, size_t n) {
    return (long long)write(s, b, n);
}
static long long sockRecv(SocketHandle s, char* b, size_t n) {
    return (long long)read(s, b, n);
}
#endif

// Very small HTTP over TCP: enough for Ollama's local API.
SocketHandle tcpConnect(const std::string& host, int port) {
#ifdef _WIN32
    if (!winsockReady()) return kInvalidSock;
#endif
    char portStr[16];
    snprintf(portStr, sizeof portStr, "%d", port);
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    std::string hostname = host;
    // strip http:// prefix if present
    if (hostname.rfind("http://", 0) == 0) hostname = hostname.substr(7);
    if (getaddrinfo(hostname.c_str(), portStr, &hints, &res) != 0 || !res) return kInvalidSock;
    SocketHandle fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd == kInvalidSock) { freeaddrinfo(res); return kInvalidSock; }
#ifdef _WIN32
    DWORD tv = 3000; // Windows SO_*TIMEO takes milliseconds
#else
    timeval tv{};
    tv.tv_sec = 3;
    tv.tv_usec = 0;
#endif
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
    int rc = connect(fd, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0) { sockClose(fd); return kInvalidSock; }
    return fd;
}

std::string httpPost(const std::string& host, int port, const std::string& path,
                     const std::string& body, std::string* error) {
    SocketHandle fd = tcpConnect(host, port);
    if (fd == kInvalidSock) {
        if (error) *error = "cannot connect to " + host + ":" + std::to_string(port);
        return "";
    }
    std::string req = "POST " + path + " HTTP/1.1\r\n"
                      "Host: " + host + "\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " + std::to_string(body.size()) + "\r\n"
                      "Connection: close\r\n\r\n" + body;
    if (sockSend(fd, req.data(), req.size()) < 0) {
        sockClose(fd);
        if (error) *error = "send failed";
        return "";
    }
    std::string resp;
    char buf[8192];
    long long n;
    while ((n = sockRecv(fd, buf, sizeof buf)) > 0) resp.append(buf, (size_t)n);
    sockClose(fd);
    // split headers/body
    size_t p = resp.find("\r\n\r\n");
    if (p == std::string::npos) { if (error) *error = "malformed HTTP reply"; return ""; }
    return resp.substr(p + 4);
}

} // namespace

bool ollamaAvailable(const OllamaConfig& cfg) {
    SocketHandle fd = tcpConnect(cfg.host, cfg.port);
    if (fd == kInvalidSock) return false;
    std::string req = "GET / HTTP/1.1\r\nHost: " + cfg.host + "\r\nConnection: close\r\n\r\n";
    bool ok = sockSend(fd, req.data(), req.size()) > 0;
    if (ok) {
        char buf[256] = {0};
        long long n = sockRecv(fd, buf, sizeof buf - 1);
        ok = n > 0 && strstr(buf, "200") != nullptr;
    }
    sockClose(fd);
    return ok;
}

bool ollamaGenerate(const OllamaConfig& cfg, const std::string& system,
                    const std::string& prompt, std::string& reply, std::string* error) {
    Json j = Json::object();
    j.set("model", Json::of(cfg.model));
    j.set("system", Json::of(system));
    j.set("prompt", Json::of(prompt));
    j.set("stream", Json::of(false));
    std::string body = httpPost(cfg.host, cfg.port, "/api/generate", j.dump(), error);
    if (body.empty()) return false;
    Json resp;
    if (!Json::parse(body, resp)) { if (error) *error = "bad JSON from Ollama"; return false; }
    reply = resp.str("response");
    if (reply.empty() && error) *error = "empty response";
    return !reply.empty();
}

std::string leptidKnowledgeSummary() {
    return
        "Leptid is a free/open-source 2D animation app (C++17, zero dependencies). "
        "Two modes: DA (drawing animation: raster layers, frames, fps timeline, onion skinning "
        "prev/next tinted ~30%, stabilizer smoothing strokes) and OOA (object-oriented animation: "
        "articulated cut characters with bones, FK rotate via Bone tool K, stable IK via IK tool I "
        "- analytic two-bone + damped FABRIK, per-joint angle limits, max 24 deg/solve so limbs "
        "never fling). Character cutting: tool P drags rectangles over a T-pose layer using slot "
        "names (torso, head, armL_upper, armL_lower, armR_upper, armR_lower, legL_upper, legL_lower, "
        "legR_upper, legR_lower, or single armL/armR/legL/legR), then Build Character. Pose keys: "
        "whole-pose keyframes with tweening (Linear/EaseIn/EaseOut/EaseInOut/Step), end FRAME not "
        "key count drives the timeline. Formats: .lep v3 = self-contained ZIP (project.json + "
        "frame PNGs + embedded character JSON + limb PNGs); .Lchar = character asset. Export: PNG "
        "sequence, animated GIF (own encoder), MP4 via ffmpeg. Timeline shows waveforms for "
        "imported audio (wav direct, mp3/ogg via ffmpeg). Cameras: project camera frames the "
        "800x600 output (tool C); editor view zoom/pan with wheel/middle-drag. Optional AI: "
        "local Ollama server, disabled unless enabled by the user.";
}

} // namespace leptid
