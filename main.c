//Zmniejszenie rozmiaru plików nagłówkowych Win32 poprzez wykluczenie niektórych rzadziej używanych interfejsów API
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <errno.h>

//Korzystając z winsock2.h trzeba dodatkowo skorzystać z ws2_32.lib (umieszczone w CMAKE)
#pragma comment(lib, "Ws2_32.lib")

#define BUFLEN 128
#define PORT 444
#define ADDRESS "127.0.0.1" // aka "localhost"
#define MAX_CLIENTS 1

// global running variable
_Atomic char running = 0; // default false
DWORD WINAPI sendThreadFunc(LPVOID lpParam);

int main() {
    int res, sendRes;
    WSADATA wsaData;
    //Inicjalizacja
    res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (res) {
        printf("Startup failed: %d\n", res);
        return 1;
    }
    SOCKET listener;
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        return ECONNREFUSED;
    }

    //Bindowanie
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(ADDRESS);
    address.sin_port = htons(PORT);
    res = bind(listener, (struct sockaddr *) &address, sizeof(address));
    if (res == SOCKET_ERROR) {
        printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(listener);
        WSACleanup();
        return 1;
    }

    res = listen(listener, SOMAXCONN);
    if (res == SOCKET_ERROR) {
        printf("Listen failed: %d\n", WSAGetLastError());
        closesocket(listener);
        WSACleanup();
        return 1;
    }
    printf("Oczekiwanie polaczenia na (IP:PORT) %s:%d\n", ADDRESS, PORT);

    fd_set socketSet;            // klienci
    SOCKET clients[MAX_CLIENTS]; // macierz klientów
    int curNoClients = 0;
    SOCKET sd, max_sd;
    struct sockaddr_in clientAddr;
    int clientAddrlen;
    char running = !0;
    char recvbuf[BUFLEN];
    char *binMessage = "00001001000000000000000000000000000000011110001100000000000000000000000011111111000111110101000010110011";
    int binMessageLen = strlen(binMessage);
    char *full = "Wyczerpano liczbę wolnych slotow. Polaczenie nie jest mozliwe\n";
    int fullLength = strlen(full);
    char *goodbye = "Serwer odlaczony\n";
    int goodbyeLength = strlen(goodbye);

    // wyczyszczenie setu klientów
    memset(clients, 0, MAX_CLIENTS * sizeof(SOCKET));

    while (running) {
        FD_ZERO(&socketSet);
        FD_SET(listener, &socketSet);

        for (int i = 0; i < MAX_CLIENTS; i++) {
            sd = clients[i];
            if (sd > 0) {
                FD_SET(sd, &socketSet);
            }
            if (sd > max_sd) {
                max_sd = sd;
            }
        }

        int activity = select(max_sd + 1, &socketSet, NULL, NULL, NULL);
        if (activity < 0) {
            continue;
        }

        if (FD_ISSET(listener, &socketSet)) {
            //Akceptacja połączenia
            sd = accept(listener, NULL, NULL);
            if (sd == INVALID_SOCKET) {
                return ENOTCONN;
            }
            //Nasłuchiwanie
            getpeername(sd, (struct sockaddr *) &clientAddr, &clientAddrlen);
            printf("Podlaczono klienta: %s:%d\n",
                   inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

            //Dodanie klienta do macierzy, jeśli są wolne miejsca
            if (curNoClients >= MAX_CLIENTS) {
                printf("Serwer przepelniony\n");
                sendRes = send(sd, full, fullLength, 0);
                if (sendRes != fullLength) {
                    return EHOSTUNREACH;
                }
                shutdown(sd, SD_BOTH);
                closesocket(sd);
            } else {
                int i;
                for (i = 0; i < MAX_CLIENTS; i++) {
                    if (!clients[i]) {
                        clients[i] = sd;
                        printf("Dodano do listy ID = %d\n", i);
                        curNoClients++;
                        break;
                    }
                }
                //Przesłanie wartości binarnej ramki
                sendRes = send(sd, binMessage, binMessageLen, 0);
                if (sendRes != binMessageLen) {
                    printf("Wystapil blad: %d\n", WSAGetLastError());
                    shutdown(sd, SD_BOTH);
                    closesocket(sd);
                    clients[i] = 0;
                    curNoClients--;
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i]) {
                continue;
            }

            sd = clients[i];
            if (FD_ISSET(sd, &socketSet)) {
                res = recv(sd, recvbuf, BUFLEN, 0);
                if (res > 0) {
                    recvbuf[res] = '\0';
                    printf("Received (%d): %s\n", res, recvbuf);
                    if (!memcmp(recvbuf, "/quit", 5 * sizeof(char))) {
                        running = 0; // false
                        break;
                    }
                    sendRes = send(sd, recvbuf, res, 0);
                    if (sendRes == SOCKET_ERROR) {
                        printf("Echo failed: %d\n", WSAGetLastError());
                        shutdown(sd, SD_BOTH);
                        closesocket(sd);
                        clients[i] = 0;
                        curNoClients--;
                    }
                } else {
                    getpeername(sd, (struct sockaddr *) &clientAddr, &clientAddrlen);
                    printf("Rozlaczono klienta %s:%d\n",
                           inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

                    shutdown(sd, SD_BOTH);
                    closesocket(sd);
                    clients[i] = 0;
                    curNoClients--;
                }
            }
        }
    }

    //Rozlaczenie klientow
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] > 0) {
            sendRes = send(clients[i], goodbye, goodbyeLength, 0);
            shutdown(clients[i], SD_BOTH);
            closesocket(clients[i]);
            clients[i] = 0;
        }
    }
    closesocket(listener);
    res = WSACleanup();
    if (res) {
        return 1;
    }
    return 0;
}