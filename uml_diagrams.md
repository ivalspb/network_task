## 1. Диаграмма классов (Class Diagram)

```
┌───────────────────────────────────────────────────────────────────────────────┐
│                                Server Package                                 │
├───────────────────────────────────────────────────────────────────────────────┤
│                                                                               │
│  ┌─────────────────────┐      ┌─────────────────────┐      ┌────────────────┐ │
│  │     ServerThread    │      │     ServerWorker    │      │   QTcpServer   │ │
│  ├─────────────────────┤      ├─────────────────────┤      ├────────────────┤ │
│  │ - m_worker          │◆---->│ - m_clients         │◆---->│ (inheritance)  │ │
│  │ - m_port            │      │ - m_socketMap       │      │                │ │
│  │ - m_startRequested  │      │ - m_buffers         │      │                │ │
│  │ - m_stopRequested   │      │ -m_processToClientId│      │                │ │
│  │ - m_thresholds      │      │ - m_thresholds      │      │                │ │
│  ├─────────────────────┤      │ - m_mutex           │      │                │ │
│  │ + startServer()     │      │ - m_running         │      │                │ │
│  │ + stopServer()      │      │ - m_heartbeatTimer  │      │                │ │
│  │ + updateThresholds()│      ├─────────────────────┤      │                │ │
│  │ + getClientSocket() │      │ + startServer()     │      │                │ │
│  │ + run()             │      │ + stopServer()      │      │                │ │
│  └─────────────────────┘      │ + updateThresholds()│      │                │ │
│                               │ + getClientSocket() │      │                │ │
│                               │ + incomingConnection│      │                │ │
│                               └─────────────────────┘      │                │ │
│                                                            │                │ │
│  ┌─────────────────────┐      ┌─────────────────────┐      │                │ │
│  │      MainWindow     │      │   SettingsDialog    │      │                │ │
│  ├─────────────────────┤      ├─────────────────────┤      │                │ │
│  │ - m_serverThread    │◆---->│ - m_cpuThreshold    │      │                │ │
│  │ - m_clientsTable    │      │ - m_memoryThreshold │      │                │ │
│  │ - m_dataTable       │      │ - m_bandwidthThresh │      │                │ │
│  │ - m_currentThreshold│      │ - m_latencyThreshold│      │                │ │
│  │ - m_clientDataMap   │      ├─────────────────────┤      │                │ │
│  ├─────────────────────┤      │ + thresholdsJson()  │      │                │ │
│  │ + onStartStopClicked│      └─────────────────────┘      │                │ │
│  │ + onClientConnected │                                   │                │ │
│  │ + onDataFromClient  │                                   │                │ │
│  │ + onSettingsRequested│                                  │                │ │
│  └─────────────────────┘                                   │                │ │
│                                                            │                │ │
└───────────────────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────────────────┐
│                                Client Package                                 │
├───────────────────────────────────────────────────────────────────────────────┤
│                                                                               │
│  ┌─────────────────────┐      ┌─────────────────────┐      ┌────────────────┐ │
│  │    NetworkClient    │      │     QTcpSocket      │      │   QTimer(s)    │ │
│  ├─────────────────────┤      ├─────────────────────┤      ├────────────────┤ │
│  │ - m_socket          │◆---->│                     │◆---->│ - m_reconnectTimer│
│  │ - m_reconnectTimer  │◆---->│                     │      │ - m_dataTimer  │ │
│  │ - m_dataTimer       │◆---->│                     │      │ - m_statsTimer │ │
│  │ - m_networkMetrics  │◆---->│                     │      │ - m_heartbeatTimer│
│  │ - m_deviceStatus    │◆---->│                     │      └────────────────┘ │
│  │ - m_logGenerator    │◆---->│                     │                         │
│  │ - m_serverAddress   │      │                     │                         │
│  │ - m_clientId        │      │                     │                         │
│  ├─────────────────────┤      │                     │                         │
│  │ + start()           │      │                     │                         │
│  │ + stop()            │      │                     │                         │
│  │ + onConnected()     │      │                     │                         │
│  │ + onReadyRead()     │      │                     │                         │
│  │ + sendData()        │      │                     │                         │
│  └─────────────────────┘      │                     │                         │
│                               │                     │                         │
│  ┌─────────────────────┐      ┌─────────────────────┐      ┌────────────────┐ │
│  │   NetworkMetrics    │      │    DeviceStatus     │      │  LogGenerator  │ │
│  ├─────────────────────┤      ├─────────────────────┤      ├────────────────┤ │
│  │ - m_bandwidth       │      │ - m_uptime          │      │ - m_random     │ │
│  │ - m_latency         │      │ - m_cpuUsage        │      │ - m_infoMessages│
│  │ - m_packetLoss      │      │ - m_memoryUsage     │      │ - m_warningMessages│
│  ├─────────────────────┤      ├─────────────────────┤      │ - m_errorMessages│
│  │ + updateMetrics()   │      │ + updateStatus()    │      ├────────────────┤ │
│  │ + toJson()          │      │ + toJson()          │      │ + generateLog()│ │
│  └─────────────────────┘      └─────────────────────┘      └────────────────┘ │
│                                                                               │
└───────────────────────────────────────────────────────────────────────────────┘
```

## 2. Диаграмма последовательности запуска сервера

```
MainWindow          ServerThread         ServerWorker         QTcpServer
    |                    |                    |                    |
    |onStartStopClicked()|                    |                    |
    |------------------->|                    |                    |
    |                    |startServer(port)   |                    |
    |                    |------------------->|                    |
    |                    |   (if not running) |                    |
    |                    |       start()      |                    |
    |                    |<-------------------|                    |
    |                    |                    |                    |
    |                    |---(in new thread)-->run()              |
    |                    |                    |------------------->|
    |                    |                    |   create worker    |
    |                    |                    |<-------------------|
    |                    |                    |                    |
    |                    |                    |startServer(port)   |
    |                    |                    |------------------->|
    |                    |                    |   listen(port)     |
    |                    |                    |<-------------------|
    |                    |                    |                    |
    |                    |   serverStatus()   |                    |
    |<----------------------------------------|                    |
```

## 3. Диаграмма последовательности подключения клиента

```
NetworkClient       ServerWorker         MainWindow
    |                    |                    |
    |connectToHost()     |                    |
    |------------------->|                    |
    |                    | incomingConnection()|
    |                    |------------------->|
    |                    |   create socket    |
    |                    |<-------------------|
    |                    |                    |
    |ClientHandshake     |                    |
    |------------------->|                    |
    |                    |handleClientHandshake|
    |                    |------------------->|
    |                    |   register client  |
    |                    |<-------------------|
    |                    |                    |
    |ConnectionAck       |   clientConnected()|
    |<-------------------|------------------->|
    |                    |                    |
    |StartCommand        |                    |
    |<-------------------|                    |
    |                    |                    |
    |start data exchange |                    |
    |------------------->|                    |
```

## 4. Диаграмма последовательности отправки данных

```
NetworkClient       ServerWorker         MainWindow
    |                    |                    |
    |m_dataTimer timeout |                    |
    |------------------->|                    |
    |                    |                    |
    |sendNetworkMetrics()|                    |
    |------------------->|                    |
    |                    |processClientData() |
    |                    |------------------->|
    |                    |   checkThresholds()|
    |                    |<-------------------|
    |                    |                    |
    |                    |   dataFromClient() |
    |                    |------------------->|
    |                    |                    |
    |sendDeviceStatus()  |                    |
    |------------------->|                    |
    |                    |processClientData() |
    |                    |------------------->|
    |                    |   checkThresholds()|
    |                    |<-------------------|
    |                    |                    |
    |                    |   dataFromClient() |
    |                    |------------------->|
```

## 5. Диаграмма последовательности проверки соединений

```
ServerWorker         QTcpSocket(s)        MainWindow
    |                    |                    |
    |m_heartbeatTimer    |                    |
    | timeout            |                    |
    |------------------->|                    |
    |                    |                    |
    |checkClientConnections()                 |
    |------------------->|                    |
    |   check each socket|                    |
    |<-------------------|                    |
    |                    |                    |
    |for inactive clients:                    |
    |   disconnectFromHost()                  |
    |------------------->|                    |
    |                    |                    |
    |onClientDisconnectedForced()             |
    |------------------->|                    |
    |   cleanup          |                    |
    |<-------------------|                    |
    |                    |                    |
    |   clientDisconnected()                  |
    |---------------------------------------->|
```

## 6. Диаграмма состояний NetworkClient

```
┌─────────────────┐
│    Stopped      │
└─────────────────┘
        |
        | start()
        v
┌─────────────────┐   connect()   ┌─────────────────┐
│  Connecting     │──────────────>│   Connected     │
└─────────────────┘               └─────────────────┘
        |                               |
        | error/timeout                 | disconnect()
        v                               v
┌─────────────────┐   tryReconnect() ┌─────────────────┐
│  Reconnecting   │<─────────────────│  Disconnected   │
└─────────────────┘                  └─────────────────┘
        |                               |
        | success                       | stop()
        v                               v
┌─────────────────┐                  ┌─────────────────┐
│   Connected     │                  │    Stopped      │
└─────────────────┘                  └─────────────────┘
```


## 7. Диаграмма компонентов (Component Diagram)

```
┌────────────────────────────────────────────────────────────┐
│                    Monitoring System                       │
├────────────────────────────────────────────────────────────┤
│                                                            │
│  ┌─────────────────┐        ┌─────────────────┐            │
│  │   Server GUI    │        │   Server Core   │            │
│  │◄────────────────┤        │────────────────►│            │
│  │ - MainWindow    │        │ - ServerThread  │            │
│  │ - SettingsDialog│        │ - ServerWorker  │            │
│  └─────────────────┘        └─────────────────┘            │
│          ▲                           ▲                     │
│          │                           │                     │
│          ▼                           ▼                     │
│  ┌─────────────────┐        ┌─────────────────┐            │
│  │    Qt GUI       │        │   Qt Network    │            │
│  │   Framework     │        │   Framework     │            │
│  └─────────────────┘        └─────────────────┘            │
│                                                            │
│  ┌─────────────────┐        ┌─────────────────┐            │
│  │   Client App    │        │  System Metrics │            │
│  │◄────────────────┤        │   Collection    │            │
│  │ - NetworkClient │        │────────────────►│            │
│  │ - main.cpp      │        │ - NetworkMetrics│            │
│  └─────────────────┘        │ - DeviceStatus  │            │
│          ▲                  │ - LogGenerator  │            │
│          │                  └─────────────────┘            │
│          ▼                           ▲                     │
│  ┌─────────────────┐        ┌─────────────────┐            │
│  │    Qt Core      │        │  System APIs    │            │
│  │   Framework     │        │   (Linux/OS)    │            │
│  └─────────────────┘        └─────────────────┘            │
│                                                            │
└────────────────────────────────────────────────────────────┘
```

## 8. Диаграмма развертывания (Deployment Diagram)

```
┌────────────────────────────────────────────────────────────┐
│                    Server Machine                          │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                   Server Process                    │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  │   │
│  │  │ GUI Thread  │  │ ServerThread│  │ Qt Network  │  │   │
│  │  │             │  │   Thread    │  │   Module    │  │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  │   │
│  └─────────────────────────────────────────────────────┘   │
│                     ▲                                      │
│                     │ TCP/IP (port 12345)                  │
│                     ▼                                      │
└────────────────────────────────────────────────────────────┘
                             │
                             │
                             ▼
┌────────────────────────────────────────────────────────────┐
│                    Client Machine 1                        │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                   Client Process                    │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  │   │
│  │  │NetworkClient│  │ System Stats│  │ Qt Network  │  │   │
│  │  │             │  │  Collection │  │   Module    │  │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  │   │
│  └─────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────┘
                             │
                             │
                             ▼
┌────────────────────────────────────────────────────────────┐
│                    Client Machine 2                        │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                   Client Process                    │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  │   │
│  │  │NetworkClient│  │ System Stats│  │ Qt Network  │  │   │
│  │  │             │  │  Collection │  │   Module    │  │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  │   │
│  └─────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────┘
```

## 9. Диаграмма активности обработки данных (Activity Diagram)

```
┌─────────────────┐
│  Data Received  │
└─────────────────┘
        |
        v
┌─────────────────────────────────┐
│ Parse JSON Message              │
│ - Validate structure            │
│ - Extract type and data         │
└─────────────────────────────────┘
        |
        v
┌─────────────────────────────────┐
│ Check Message Type              │
│ - Heartbeat? ──────┐            │
│ - NetworkMetrics?  │            │
│ - DeviceStatus?    │            │
│ - Log?             │            │
│ - Control?         │            │
└─────────────────────────────────┘
        |              |           |
        |              |           |
        v              v           v
┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│ Send        │  │ Process     │  │ Execute     │
│ Response    │  │ Metrics &   │  │ Control     │
│             │  │ Check       │  │ Command     │
│             │  │ Thresholds  │  │             │
└─────────────┘  └─────────────┘  └─────────────┘
        |              |                   |
        |              v                   |
        |      ┌─────────────┐             |
        |      │ Update GUI  │             |
        |      │ if needed   │             |
        |      └─────────────┘             |
        |              |                   |
        └──────────────┴───────────────────┘
                         |
                         v
                 ┌─────────────┐
                 │   Complete  │
                 └─────────────┘
```

## 10. Диаграмма состояний ServerWorker

```
┌─────────────────┐
│    Stopped      │
└─────────────────┘
        |
        | startServer()
        v
┌─────────────────┐   start success  ┌─────────────────┐
│   Starting      │─────────────────>│   Listening     │
└─────────────────┘                  └─────────────────┘
        |                                     |
        | start fail                          | stopServer()
        v                                     v
┌─────────────────┐                  ┌─────────────────┐
│    Error        │                  │   Stopping      │
└─────────────────┘                  └─────────────────┘
        |                                     |
        | retry                               | cleanup complete
        v                                     v
┌─────────────────┐                  ┌─────────────────┐
│   Starting      │                  │    Stopped      │
└─────────────────┘                  └─────────────────┘

From Listening state:
┌─────────────────┐   client connects  ┌─────────────────┐
│   Listening     │───────────────────>│ Client Handling │
└─────────────────┘                    └─────────────────┘
        ʌ                                     |
        | client disconnects                  | process data
        |                                     v
        └─────────────────────────────────────┘
```

## 11. Диаграмма пакетов (Package Diagram)

```
┌────────────────────────────────────────────────────────────┐
│                 Network Monitoring System                  │
├────────────────────────────────────────────────────────────┤
│                                                            │
│  ┌─────────────────┐        ┌─────────────────┐            │
│  │    Server       │        │    Client       │            │
│  │   Package       │        │   Package       │            │
│  ├─────────────────┤        ├─────────────────┤            │
│  │ ◄─dependency──► │        │ ◄─dependency──► │            │
│  │                 │        │                 │            │
│  │ ┌─────────────┐ │        │ ┌─────────────┐ │            │
│  │ │  GUI        │ │        │ │ Network     │ │            │
│  │ │  Module     │ │        │ │ Client      │ │            │
│  │ └─────────────┘ │        │ └─────────────┘ │            │
│  │                 │        │                 │            │
│  │ ┌─────────────┐ │        │ ┌─────────────┐ │            │
│  │ │  Server     │ │        │ │ Metrics     │ │            │
│  │ │  Core       │ │        │ │ Collection  │ │            │
│  │ │  Module     │ │        │ │ Module      │ │            │
│  │ └─────────────┘ │        │ └─────────────┘ │            │
│  └─────────────────┘        └─────────────────┘            │
│                                                            │
│  ┌─────────────────┐        ┌─────────────────┐            │
│  │   Shared        │        │    Qt           │            │
│  │   Protocol      │        │   Framework     │            │
│  │   Package       │        │   Package       │            │
│  ├─────────────────┤        ├─────────────────┤            │
│  │ ◄─dependency─── │        │ ◄─dependency─── │            │
│  │                 │        │                 │            │
│  │ JSON Message    │        │ QtCore, QtGui,  │            │
│  │ Formats         │        │ QtNetwork, etc. │            │
│  │ Data Structures │        │                 │            │
│  └─────────────────┘        └─────────────────┘            │
│                                                            │
└────────────────────────────────────────────────────────────┘
```

## 12. Диаграмма коммуникации для heartbeat

```
NetworkClient       ServerWorker
    |                    |
    |Heartbeat Timer     |
    | timeout            |
    |------------------->|
    |                    |
    |sendHeartbeat()     |
    |------------------->|
    |                    |
    |                    | process heartbeat
    |                    |-------------------->|
    |                    | update last activity|
    |                    |<--------------------|
    |                    |                     |
    |HeartbeatResponse   |                     |
    |<-------------------|                     |
    |                    |                     |
    |update last response|                     |
    |time                |                     |
    |------------------->|                     |
```


## 13. Детальная диаграмма классов Server Package

```plaintext
┌───────────────────────────────────────────────────────────────────────────────┐
│                                SERVER PACKAGE                                 │
├───────────────────────────────────────────────────────────────────────────────┤
│                                                                               │
│  ┌─────────────────────────────────────────────────────────────────────────┐  │
│  │                             ServerThread                                │  │
│  ├─────────────────────────────────────────────────────────────────────────┤  │
│  │  - m_worker: ServerWorker*                                              │  │
│  │  - m_port: int                                                          │  │
│  │  - m_startRequested: bool                                               │  │
│  │  - m_stopRequested: bool                                                │  │
│  │  - m_thresholds: QJsonObject                                            │  │
│  ├─────────────────────────────────────────────────────────────────────────┤  │
│  │  + startServer(port: int): void                                         │  │
│  │  + stopServer(): void                                                   │  │
│  │  + updateThresholds(thresholds: QJsonObject): void                      │  │
│  │  + getClientSocket(clientId: int): QTcpSocket*                          │  │
│  │  + run(): void                                                          │  │
│  │  # clientConnected(id: int, ip: QString)                                │  │
│  │  # clientDisconnected(id: int)                                          │  │
│  │  # dataFromClient(id: int, type: QString, content: QString, timestamp: QString)│
│  │  # logMessage(msg: QString)                                             │  │
│  │  # serverStatus(status: QString)                                        │  │
│  └─────────────────────────────────────────────────────────────────────────┘  │
│                                  ◆                                            │
│                                  |                                            │
│                                  | composition                                │
│                                  ▼                                            │
│  ┌─────────────────────────────────────────────────────────────────────────┐  │
│  │                             ServerWorker                                │  │
│  ├─────────────────────────────────────────────────────────────────────────┤  │
│  │  - m_clients: QMap<QTcpSocket*, int>                                    │  │
│  │  - m_socketMap: QMap<int, QTcpSocket*>                                  │  │
│  │  - m_buffers: QMap<QTcpSocket*, QByteArray>                             │  │
│  │  - m_processToClientId: QMap<QString, int>                              │  │
│  │  - m_clientToProcessId: QMap<int, QString>                              │  │
│  │  - m_nextClientId: int                                                  │  │
│  │  - m_thresholds: QJsonObject                                            │  │
│  │  - m_mutex: QMutex                                                      │  │
│  │  - m_running: bool                                                      │  │
│  │  - m_clientLastActivity: QMap<int, QDateTime>                           │  │
│  │  - m_heartbeatTimer: QTimer*                                            │  │
│  │  - m_heartbeatInterval: int                                             │  │
│  ├─────────────────────────────────────────────────────────────────────────┤  │
│  │  + startServer(port: int): void                                         │  │
│  │  + stopServer(): void                                                   │  │
│  │  + updateThresholds(thresholds: QJsonObject): void                      │  │
│  │  + getClientSocket(clientId: int): QTcpSocket*                          │  │
│  │  # incomingConnection(socketDescriptor: qintptr): void                  │  │
│  │  # onClientReadyRead(): void                                            │  │
│  │  # onClientDisconnected(): void                                         │  │
│  │  # processControlCommand(socket: QTcpSocket*, json: QJsonObject): void  │  │
│  │  - checkThresholds(clientId: int, type: QString, data: QJsonObject): void│
│  │  - processClientData(socket: QTcpSocket*, json: QJsonObject): void      │  │
│  │  - sendAck(socket: QTcpSocket*, clientId: int): void                    │  │
│  │  - sendStart(socket: QTcpSocket*, clientId: int): void                  │  │
│  │  - handleClientHandshake(socket: QTcpSocket*, handshake: QJsonObject): void│
│  │  - getTimestamp(): QString                                              │  │
│  │  - getClientInfo(socket: QTcpSocket*): QString                          │  │
│  │  - checkClientConnections(): void                                       │  │
│  │  - onClientDisconnectedForced(socket: QTcpSocket*): void                │  │
│  └─────────────────────────────────────────────────────────────────────────┘  │
│                                  △                                            │
│                                  |                                            │
│                                  | inheritance                                │
│                                  |                                            │
│  ┌─────────────────────────────────────────────────────────────────────────┐  │
│  │                               QTcpServer                                │  │
│  └─────────────────────────────────────────────────────────────────────────┘  │
│                                                                               │
└───────────────────────────────────────────────────────────────────────────────┘
```

## 14. Детальная диаграмма классов Client Package

```plaintext
┌───────────────────────────────────────────────────────────────────────────────┐
│                                CLIENT PACKAGE                                 │
├───────────────────────────────────────────────────────────────────────────────┤
│                                                                               │
│  ┌─────────────────────────────────────────────────────────────────────────┐  │
│  │                            NetworkClient                                │  │
│  ├─────────────────────────────────────────────────────────────────────────┤  │
│  │  - m_socket: QTcpSocket*                                                │  │
│  │  - m_reconnectTimer: QTimer*                                            │  │
│  │  - m_dataTimer: QTimer*                                                 │  │
│  │  - m_statsTimer: QTimer*                                                │  │
│  │  - m_heartbeatTimer: QTimer*                                            │  │
│  │  - m_networkMetrics: NetworkMetrics*                                    │  │
│  │  - m_deviceStatus: DeviceStatus*                                        │  │
│  │  - m_logGenerator: LogGenerator*                                        │  │
│  │  - m_serverAddress: QHostAddress                                        │  │
│  │  - m_serverPort: quint16                                                │  │
│  │  - m_clientId: int                                                      │  │
│  │  - m_heartbeatTimeout: int                                              │  │
│  │  - m_connectionAcknowledged: bool                                       │  │
│  │  - m_dataExchangeEnabled: bool                                          │  │
│  │  - m_processId: QString                                                 │  │
│  │  - m_lastHeartbeatResponse: QDateTime                                   │  │
│  ├─────────────────────────────────────────────────────────────────────────┤  │
│  │  + start(address: QString = "localhost", port: quint16 = 12345): void   │  │
│  │  + stop(): void                                                         │  │
│  │  # onConnected(): void                                                  │  │
│  │  # onDisconnected(): void                                               │  │
│  │  # onReadyRead(): void                                                  │  │
│  │  # processControlCommand(message: QJsonObject): void                    │  │
│  │  # onErrorOccurred(error: QAbstractSocket::SocketError): void           │  │
│  │  # sendData(): void                                                     │  │
│  │  # tryReconnect(): void                                                 │  │
│  │  # updateRealTimeData(): void                                           │  │
│  │  # checkConnectionHealth(): void                                        │  │
│  │  # sendHeartbeat(): void                                                │  │
│  │  - sendNetworkMetrics(): void                                           │  │
│  │  - sendDeviceStatus(): void                                             │  │
│  │  - sendLog(): void                                                      │  │
│  │  - createJsonMessage(obj: QJsonObject): QByteArray                      │  │
│  │  - parseServerMessage(message: QJsonObject): void                       │  │
│  │  - logMessage(message: QString, toConsole: bool = false): void          │  │
│  │  - generateProcessId(): QString                                         │  │
│  │  - sendInitialHandshake(): void                                         │  │
│  │  - startHeartbeatMonitoring(): void                                     │  │
│  │  - stopHeartbeatMonitoring(): void                                      │  │
│  └─────────────────────────────────────────────────────────────────────────┘  │
│                                  ◆                                            │
│                                  | composition                                │
│                                  ▼                                            │
│  ┌─────────────────────────────────────────────────────────────────────────┐  │
│  │                           NetworkMetrics                                │  │
│  ├─────────────────────────────────────────────────────────────────────────┤  │
│  │  - m_bandwidth: double                                                  │  │
│  │  - m_latency: double                                                    │  │
│  │  - m_packetLoss: double                                                 │  │
│  │  - m_interfaceName: QString                                             │  │
│  │  - m_lastTotalBytes: qint64                                             │  │
│  │  - m_lastUpdateTime: QDateTime                                          │  │
│  ├─────────────────────────────────────────────────────────────────────────┤  │
│  │  + updateMetrics(targetAddress: QHostAddress): void                     │  │
│  │  + toJson(): QJsonObject                                                │  │
│  │  - getNetworkBandwidth(): double                                        │  │
│  │  - getNetworkLatency(targetAddress: QHostAddress): double               │  │
│  │  - getPacketLoss(targetAddress: QHostAddress): double                   │  │
│  │  - getNetworkInterfaceName(targetAddress: QHostAddress): QString        │  │
│  │  - getTotalBytes(): qint64                                              │  │
│  │  - measureTcpRttWithEcho(address: QHostAddress, port: quint16): double  │  │
│  │  - measurePingLatency(address: QHostAddress): double                    │  │
│  └─────────────────────────────────────────────────────────────────────────┘  │
│                                                                               │
│  ┌─────────────────────────────────────────────────────────────────────────┐  │
│  │                            DeviceStatus                                 │  │
│  ├─────────────────────────────────────────────────────────────────────────┤  │
│  │  - m_uptime: qint64                                                     │  │
│  │  - m_cpuUsage: double                                                   │  │
│  │  - m_memoryUsage: double                                                │  │
│  │  - m_lastCpuTotal: qint64                                               │  │
│  │  - m_lastCpuIdle: qint64                                                │  │
│  │  - m_lastCpuUpdate: QDateTime                                           │  │
│  ├─────────────────────────────────────────────────────────────────────────┤  │
│  │  + updateStatus(): void                                                 │  │
│  │  + toJson(): QJsonObject                                                │  │
│  │  - getUptime(): qint64                                                  │  │
│  │  - getCpuUsage(): double                                                │  │
│  │  - getMemoryUsage(): double                                             │  │
│  │  - getLinuxCpuUsage(total: qint64&, idle: qint64&): void                │  │
│  └─────────────────────────────────────────────────────────────────────────┘  │
│                                                                               │
│  ┌─────────────────────────────────────────────────────────────────────────┐  │
│  │                            LogGenerator                                 │  │
│  ├─────────────────────────────────────────────────────────────────────────┤  │
│  │  - m_random: QRandomGenerator                                           │  │
│  │  - m_infoMessages: QStringList                                          │  │
│  │  - m_warningMessages: QStringList                                       │  │
│  │  - m_errorMessages: QStringList                                         │  │
│  ├─────────────────────────────────────────────────────────────────────────┤  │
│  │  + generateLog(): QJsonObject                                           │  │
│  │  - generateInfoMessage(): QString                                       │  │
│  │  - generateWarningMessage(): QString                                    │  │
│  │  - generateErrorMessage(): QString                                      │  │
│  │  - generateLongInfoMessage(): QString                                   │  │
│  │  - generateLongWarningMessage(): QString                                │  │
│  │  - generateLongErrorMessage(): QString                                  │  │
│  └─────────────────────────────────────────────────────────────────────────┘  │
│                                                                               │
└───────────────────────────────────────────────────────────────────────────────┘
```

## 15. Детальная диаграмма последовательности обработки сообщения

```plaintext
NetworkClient       ServerWorker         MainWindow          Описание
    |                    |                    |                    |
    |NetworkMetrics      |                    |                    |
    |JSON message        |                    |                    |
    |───────────────────>|                    |                    | 1. Клиент отправляет данные
    |                    |                    |                    |
    |                    |onClientReadyRead() |                    | 2. Сервер получает данные
    |                    |───────────────────>|                    |
    |                    | parse JSON         |                    | 3. Парсинг JSON
    |                    |<───────────────────|                    |
    |                    |                    |                    |
    |                    |processClientData() |                    | 4. Обработка данных клиента
    |                    |───────────────────>|                    |
    |                    | extract metrics    |                    | 5. Извлечение метрик
    |                    |<───────────────────|                    |
    |                    |                    |                    |
    |                    |checkThresholds()   |                    | 6. Проверка порогов
    |                    |───────────────────>|                    |
    |                    | compare values     |                    | 7. Сравнение с порогами
    |                    |<───────────────────|                    |
    |                    |                    |                    |
    |                    |if threshold exceeded                    | 8. Если порог превышен
    |                    |───────────────────>|                    |
    |                    |   thresholdWarning()|                   | 9. Отправка предупреждения
    |                    |────────────────────────────────────────>|10. В GUI
    |                    |                    |                    |
    |                    |dataFromClient()    |                    |11. Отправка данных в GUI
    |                    |────────────────────────────────────────>|12. для отображения
    |                    |                    |                    |
    |                    |                    |onDataFromClient()  |13. Обработка в GUI
    |                    |                    |───────────────────>|14. Обновление таблицы
    |                    |                    | update UI          |15. Обновление интерфейса
    |                    |                    |<───────────────────|16. 
    |                    |                    |                    |
```

## 16. Диаграмма состояний с детализацией Client

```plaintext
┌─────────────────┐
│    Stopped      │
└─────────────────┘
        |
        | start()
        v
┌─────────────────┐   connect()   ┌─────────────────┐
│  Connecting     │──────────────>│   Connected     │
└─────────────────┘               └─────────────────┘
        |            ▲                    |  |
        | error      | handshake success  |  | sendInitialHandshake()
        | timeout    |                    |  v
        v            |                    |┌─────────────────┐
┌─────────────────┐  |                    ││ Handshake Sent  │
│  Reconnecting   │  |                    └─────────────────┘
└─────────────────┘  |                    |  |
        |            |                    |  | receive ConnectionAck
        | success    |                    |  v
        └────────────┴─────────────────────┐┌─────────────────┐
                         |                 ││ Ack Received    │
                         |                 └─────────────────┘
                         |                 |  |
                         |                 |  | receive StartCommand
                         |                 |  v
                         |                 │┌─────────────────┐
                         |                 ││ Data Exchange   │
                         |                 ││   Enabled       │
                         |                 └─────────────────┘
                         |                 |  |
                         |                 |  | receive StopDataExchange
                         |                 |  v
                         |                 │┌─────────────────┐
                         |                 ││ Data Exchange   │
                         |                 ││   Disabled      │
                         |                 └─────────────────┘
                         |                 |  |
                         |                 |  | disconnect/timeout
                         |                 |  v
                         |                 │┌─────────────────┐
                         └─────────────────┴│ Disconnected   │
                                            └─────────────────┘
                                                    |
                                                    | stop()
                                                    v
                                            ┌─────────────────┐
                                            │    Stopped      │
                                            └─────────────────┘
```

## 17. Диаграмма коммуникации для управления клиентом

```plaintext
MainWindow          ServerThread         ServerWorker         NetworkClient
    |                    |                    |                    |
    |onClientControlClicked()                 |                    |
    |------------------->|                    |                    | 1. Пользователь нажимает кнопку
    |                    |                    |                    |
    |                    |getClientSocket()   |                    | 2. Получение сокета клиента
    |                    |------------------->|                    |
    |                    |                    |                    |
    |                    |       socket       |                    | 3. Возврат сокета
    |                    |<-------------------|                    |
    |                    |                    |                    |
    |                    |                    |Control Command     | 4. Отправка команды управления
    |                    |                    |───────────────────>| 5. (Start/StopDataExchange)
    |                    |                    |                    |
    |                    |                    |ControlResponse     | 6. Клиент подтверждает
    |                    |                    |<───────────────────| 7. 
    |                    |                    |                    |
    |                    |clientControlStatusChanged()             | 8. Обновление статуса
    |                    |<-------------------|                    |
    |clientControlStatusChanged()             |                    | 9. Обновление GUI
    |<-------------------|                    |                    |
    |                    |                    |                    |
    |update UI           |                    |                    |10. Обновление интерфейса
    |------------------->|                    |                    |
```

## 18. Диаграмма компонентов с интерфейсами

```plaintext
┌──────────────────────────────────────────────────────────────────────────────┐
│                        COMPONENT ARCHITECTURE                                │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌─────────────────┐        ┌─────────────────┐        ┌─────────────────┐   │
│  │   Server GUI    │        │   Server Core   │        │   Qt Network    │   │
│  │   Component     │        │   Component     │        │   Component     │   │
│  ├─────────────────┤        ├─────────────────┤        ├─────────────────┤   │
│  │ Provides:       │        │ Provides:       │        │ Provides:       │   │
│  │ - GUI Interface │        │ - TCP Server    │        │ - Socket API    │   │
│  │ - User Controls │        │ - Client Mgmt   │        │ - Networking    │   │
│  │ - Data Display  │        │ - Data Processing│       │ - SSL Support   │   │
│  │ Requires:       │        │ Requires:       │        │ Requires:       │   │
│  │ - Server API    │        │ - Qt Network    │        │ - OS Socket API │   │
│  │ - Qt GUI        │        │ - Qt Core       │        │                 │   │
│  └─────────────────┘        └─────────────────┘        └─────────────────┘   │
│          |                           |                           |           │
│          |                           |                           |           │
│          ▼                           ▼                           ▼           │
│  ┌─────────────────┐        ┌─────────────────┐        ┌─────────────────┐   │
│  │    Qt GUI       │        │    Qt Core      │        │  Operating      │   │
│  │   Component     │        │   Component     │        │  System         │   │
│  └─────────────────┘        └─────────────────┘        └─────────────────┘   │
│                                                                              │
│  ┌─────────────────┐        ┌─────────────────┐        ┌─────────────────┐   │
│  │   Client App    │        │  Metrics Collector│      │   Logging       │   │
│  │   Component     │        │   Component     │        │   Component     │   │
│  ├─────────────────┤        ├─────────────────┤        ├─────────────────┤   │
│  │ Provides:       │        │ Provides:       │        │ Provides:       │   │
│  │ - Client Logic  │        │ - System Metrics│        │ - Log Generation│   │
│  │ - Communication │        │ - Network Stats │        │ - Message Types │   │
│  │ - Reconnection  │        │ - Device Status │        │ - Severity Levels│
│  │ Requires:       │        │ Requires:       │        │ Requires:       │   │
│  │ - Qt Network    │        │ - System APIs   │        │ - Randomization │   │
│  │ - Metrics       │        │ - /proc files   │        │                 │   │
│  └─────────────────┘        └─────────────────┘        └─────────────────┘   │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

