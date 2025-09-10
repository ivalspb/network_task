
# Network Monitoring System

A comprehensive client-server application for real-time network and device monitoring with threshold-based alerting.

## Overview

This system consists of:
- **Server**: Qt-based application that collects and displays metrics from multiple clients
- **Client**: Lightweight agent that gathers system metrics and sends them to the server

## Features

### Server
- Real-time monitoring of multiple clients
- Threshold-based alerting system
- Graphical user interface with data visualization
- Client connection management
- Configurable thresholds for:
  - CPU usage
  - Memory usage  
  - Network bandwidth
  - Network latency
  - Packet loss

### Client
- Automatic metric collection:
  - Network performance (bandwidth, latency, packet loss)
  - Device status (CPU, memory, uptime)
  - System logs with varying severity levels
- Automatic reconnection capabilities
- Heartbeat monitoring for connection health
- TCP keepalive optimization

## Build Requirements

### Server
- Qt 5.15+ or Qt 6.x
- C++17 compatible compiler
- Linux/Windows/macOS

### Client  
- Qt 5.15+ or Qt 6.x
- C++17 compatible compiler
- Linux (primary target), with Windows/macOS support

## Building

### Server
```bash
cd server
mkdir build && cd build
qmake ..
make -j4
```

### Client
```bash
cd client  
mkdir build && cd build
qmake ..
make -j4
```

## Usage

### Starting the Server
```bash
./Server
```
- Server listens on port 12345 by default
- GUI displays connected clients and their metrics
- Use Settings button to configure thresholds

### Starting a Client
```bash
./Client -a <server_address> -p <port>
```
Example:
```bash
./Client -a 192.168.1.100 -p 12345
```

### Command Line Options (Client)
- `-a, --address`: Server address (default: localhost)
- `-p, --port`: Server port (default: 12345)

## Protocol

### Message Types
- `ClientHandshake`: Initial connection handshake
- `NetworkMetrics`: Network performance data
- `DeviceStatus`: System resource usage
- `Log`: System log messages
- `Heartbeat`: Connection health monitoring
- `ControlCommand`: Start/Stop data exchange

### Data Format
JSON messages with newline termination:
```json
{
  "type": "NetworkMetrics",
  "metrics": {
    "bandwidth": 950.5,
    "latency": 45.2,
    "packet_loss": 0.1,
    "interface": "eth0"
  },
  "timestamp": "2024-01-15T10:30:45.123Z"
}
```

## File Structure

### Server
```
server/
├── Server/
│   ├── include/
│   │   ├── serverthread.h
│   │   └── serverworker.h
│   └── src/
│       ├── serverthread.cpp
│       └── serverworker.cpp
├── GUI/
│   ├── include/
│   │   ├── mainwindow.h
│   │   └── settingsdialog.h
│   └── src/
│       ├── mainwindow.cpp
│       └── settingsdialog.cpp
└── main.cpp
```

### Client
```
client/
├── include/
│   ├── client.h
│   ├── networkmetrics.h
│   ├── devicestatus.h
│   └── loggenerator.h
├── src/
│   ├── client.cpp
│   ├── networkmetrics.cpp
│   ├── devicestatus.cpp
│   └── loggenerator.cpp
└── main.cpp
```

## Key Components

### ServerThread
- Manages server worker in separate thread
- Handles thread-safe operations
- Forwards signals to GUI

### ServerWorker  
- TCP server implementation
- Client connection management
- Message processing and threshold checking

### NetworkMetrics (Client)
- Bandwidth calculation
- Latency measurement (TCP echo + ping)
- Packet loss detection
- Network interface identification

### DeviceStatus (Client)
- Uptime monitoring
- CPU usage calculation
- Memory usage tracking

### LogGenerator (Client)
- Realistic log message generation
- Multiple severity levels (INFO, WARNING, ERROR)
- Variable message lengths

## Monitoring Capabilities

### Network Metrics
- **Bandwidth**: Real-time network throughput
- **Latency**: Round-trip time measurements
- **Packet Loss**: Retransmission rate monitoring
- **Interface**: Active network interface detection

### Device Metrics  
- **CPU Usage**: Current processor utilization
- **Memory Usage**: RAM consumption percentage
- **Uptime**: System running time
- **System Logs**: Generated application logs

## Alerting System

The server provides configurable thresholds with visual indicators:
- Red background for exceeded thresholds
- Warning messages in threshold log
- Advice suggestions for common issues

Default thresholds:
- CPU: 80%
- Memory: 85% 
- Bandwidth: 800 Mbps
- Latency: 100 ms
- Packet Loss: 5%

## Connection Management

### Client Identification
- Unique process-based client IDs
- Automatic reconnection handling
- Duplicate connection prevention

### Health Monitoring
- Heartbeat mechanism (5-second intervals)
- Automatic reconnection on failure
- TCP keepalive optimization

## Performance Features

- Multi-threaded server architecture
- Non-blocking socket operations
- Efficient JSON message parsing
- Random delay injection for realistic load simulation
- Memory-efficient data structures

## Platform Support

### Primary
- Linux (tested on Ubuntu 20.04+)

### Secondary
- Windows (requires minor adjustments)
- macOS (requires minor adjustments)

## Dependencies

- Qt Core, Network, and GUI modules
- Standard C++17 library
- System-specific networking APIs

## License

Proprietary - See individual file headers for license information.

## Support

For issues and questions, please check:
1. Server/client connectivity (firewall, ports)
2. JSON message format compliance
3. System permission requirements
4. Network interface configuration
```
