# 🚀 PennCloud: A Distributed Cloud Storage & Email Platform

[![C++](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![Distributed Systems](https://img.shields.io/badge/Architecture-Distributed-green.svg)](https://en.wikipedia.org/wiki/Distributed_computing)
[![BigTable](https://img.shields.io/badge/Storage-BigTable-orange.svg)](https://en.wikipedia.org/wiki/Bigtable)

> **A high-performance, fault-tolerant distributed system implementing Google's BigTable architecture with email services, file storage, and real-time replication.**

## 🌟 Overview

PennCloud is a comprehensive distributed cloud platform that combines the power of Google's BigTable architecture with modern web services. Built from scratch in C++, it provides enterprise-grade email services, file storage, and administrative capabilities across multiple replication groups with automatic failover and load balancing.

### 🎯 Key Features

- **📧 Complete Email System**: Full SMTP/POP3 implementation with inbox management, email composition, and forwarding
- **💾 Distributed File Storage**: High-performance file upload/download with chunked storage for large files (up to 1TB+)
- **🔄 Automatic Replication**: Multi-master replication across server groups with automatic failover
- **⚡ Load Balancing**: Intelligent request distribution across frontend servers
- **🛠️ Admin Console**: Real-time monitoring and management of the entire system
- **🔐 User Authentication**: Secure login system with session management
- **📊 BigTable Storage**: Custom implementation of Google's BigTable for scalable data storage

## 🏗️ Architecture

### System Components

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Load Balancer │    │  Frontend Server│    │  Admin Console  │
│   (Port 8880)   │◄──►│   (Port 8000)   │◄──►│   (Port 8080)   │
└─────────────────┘    └─────────────────┘    └─────────────────┘
                                │
                                ▼
                    ┌─────────────────────────┐
                    │     Coordinator         │
                    │   (Heartbeat Monitor)   │
                    └─────────────────────────┘
                                │
                ┌───────────────┼───────────────┐
                ▼               ▼               ▼
    ┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
    │  Replication    │ │  Replication    │ │  Replication    │
    │   Group 1       │ │   Group 2       │ │   Group N       │
    │                 │ │                 │ │                 │
    │ ┌─────────────┐ │ │ ┌─────────────┐ │ │ ┌─────────────┐ │
    │ │ KV Storage  │ │ │ │ KV Storage  │ │ │ │ KV Storage  │ │
    │ │ (Primary)   │ │ │ │ (Primary)   │ │ │ │ (Primary)   │ │
    │ └─────────────┘ │ │ └─────────────┘ │ │ └─────────────┘ │
    │ ┌─────────────┐ │ │ ┌─────────────┐ │ │ ┌─────────────┐ │
    │ │ KV Storage  │ │ │ │ KV Storage  │ │ │ │ KV Storage  │ │
    │ │ (Replica)   │ │ │ │ (Replica)   │ │ │ │ (Replica)   │ │
    │ └─────────────┘ │ │ └─────────────┘ │ │ └─────────────┘ │
    │ ┌─────────────┐ │ │ ┌─────────────┐ │ │ ┌─────────────┐ │
    │ │ KV Storage  │ │ │ │ KV Storage  │ │ │ │ KV Storage  │ │
    │ │ (Replica)   │ │ │ │ (Replica)   │ │ │ │ (Replica)   │ │
    │ └─────────────┘ │ │ └─────────────┘ │ │ └─────────────┘ │
    └─────────────────┘ └─────────────────┘ └─────────────────┘
              
```

### 🗄️ BigTable Implementation

Our custom BigTable implementation features:

- **Tablet-based Storage**: Data is partitioned into tablets for horizontal scaling
- **Automatic Splitting**: Tablets automatically split when they exceed size limits
- **Memory Management**: Intelligent caching with disk persistence
- **Row-level Locking**: Fine-grained concurrency control
- **WAL (Write-Ahead Logging)**: Crash recovery and data consistency
- **Checkpointing**: Periodic snapshots for fast recovery

### 📧 Email System

- **SMTP Server**: Handles outgoing emails with local and remote delivery
- **POP3 Server**: Manages email retrieval and inbox operations
- **Email Storage**: Messages stored in BigTable with unique UUIDs
- **Remote Relay**: Automatic forwarding to external domains
- **User Authentication**: Secure login with password verification

### 💾 File Storage System

- **Chunked Storage**: Large files split into 25MB chunks for efficient storage
- **Directory Structure**: Hierarchical file organization
- **Upload/Download**: RESTful API for file operations
- **File Management**: Rename, delete, and view operations
- **Metadata Storage**: File information stored in BigTable

## 🚀 Quick Start

### Prerequisites

- **macOS** or **Linux** system
- **C++17** compatible compiler (GCC 7+ or Clang 5+)
- **Make** build system
- **pthread** library

### Installation & Deployment

1. **Clone the repository**
   ```bash
   git clone <repository-url>
   cd sp25-cis5050-T10-main
   ```

2. **Deploy the entire system**
   
   **For macOS:**
   ```bash
   ./makeProject_mac.sh
   ```
   
   **For Linux:**
   ```bash
   ./makeProject.sh
   ```

3. **Access the services**
   - **Main Application**: http://127.0.0.1:8880
   - **Admin Console**: http://127.0.0.1:8080/admin
   - **Load Balancer**: http://127.0.0.1:8880

### 🔧 Individual Component Deployment

**Backend Services:**
```bash
cd Backend
./make_all.sh
cd ../Deployment
./dev_backend.sh
```

**Frontend Server:**
```bash
cd Frontend/frontend-server
make
./fe_deploy.sh
```

**Admin Console:**
```bash
cd Frontend/admin
make
./admin-server
```

## 📖 API Documentation

### Email Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/email/inbox` | Retrieve user's inbox |
| `GET` | `/email/view/{id}` | View specific email |
| `POST` | `/email/send` | Send new email |
| `POST` | `/email/delete/{id}` | Delete email |
| `POST` | `/email/forward/{id}` | Forward email |

### File Storage Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/drive/view` | List files in directory |
| `POST` | `/drive/upload` | Upload file |
| `GET` | `/drive/download?file={name}` | Download file |
| `POST` | `/drive/delete` | Delete file |
| `POST` | `/drive/rename` | Rename file |

### User Management

| Method | Endpoint | Description |
|--------|----------|-------------|
| `POST` | `/user/signup` | Create new account |
| `POST` | `/user/login` | User authentication |
| `POST` | `/user/logout` | End user session |

## 🛠️ Configuration

### Server Configuration (`servers.cfg`)
```
# Coordinator host and port (first line)
127.0.0.1:4444
# KV Storage servers (subsequent lines)
127.0.0.1:8080
127.0.0.1:8082
127.0.0.1:8084
127.0.0.1:8090
127.0.0.1:8092
127.0.0.1:8094
```

### Frontend Configuration (`fe_config.cfg`)
```
127.0.0.1 8000
127.0.0.1 8001
127.0.0.1 8002
127.0.0.1 8003
127.0.0.1 8004
```

## 🔍 Monitoring & Administration

### Admin Console Features

- **Real-time Server Status**: Monitor all backend servers and their health
- **BigTable Data Viewer**: Inspect stored data across all tablets
- **Replication Group Management**: View and manage replication groups
- **Performance Metrics**: Monitor system performance and resource usage
- **Server Control**: Enable/disable servers and manage failover

### Health Monitoring

The system includes comprehensive health monitoring:

- **Heartbeat Service**: Continuous monitoring of all servers
- **Automatic Failover**: Primary server failure triggers replica promotion
- **Load Balancing**: Intelligent request distribution
- **Error Recovery**: Automatic recovery from transient failures

## 🏆 Performance Features

### Scalability
- **Horizontal Scaling**: Add more servers to increase capacity
- **Load Distribution**: Requests distributed across multiple frontend servers
- **Data Partitioning**: Tablets automatically split as data grows

### Reliability
- **Fault Tolerance**: System continues operating with server failures
- **Data Replication**: Multiple copies of data across different servers
- **Consistent Hashing**: Efficient data distribution and rebalancing

### Performance
- **Large File Support**: Efficient handling of files up to 150MB+
- **Memory Management**: Intelligent caching with disk persistence
- **Concurrent Processing**: Multi-threaded request handling

## 🧪 Testing & Development

### Running Individual Components

**KV Storage Server:**
```bash
./kvstorage -p <port> -c <server-config> [-i <init-dir>] [-w <work-dir>] [-v] [-r]
```

**SMTP Server:**
```bash
./smtp -p <port> -c <server-config>
```

**POP3 Server:**
```bash
./pop3 -p <port> -c <server-config>
```

### Debug Mode

Enable verbose logging by setting the `DEBUG` environment variable:
```bash
export DEBUG=1
```

## 🚨 Important Notes

### Cache Management
**⚠️ CRITICAL**: Before each run, delete the contents of the `cache_2` directory to ensure clean state:
```bash
rm -rf Data/cache_2/*
```

### Port Configuration
- Ensure no port conflicts between services
- Internal ports are typically `external_port + 1`
- Check `servers.cfg` and `fe_config.cfg` for port assignments


## 🎓 Academic Context

This project was developed as part of **CIS 5050 - Software Systems** at the University of Pennsylvania, implementing advanced distributed systems concepts including:

- Distributed consensus algorithms
- Replication and consistency models
- Load balancing and fault tolerance
- BigTable-style storage systems
- Network protocol implementation

## 📄 License

This project is developed for educational purposes as part of the CIS 5050 course at the University of Pennsylvania.

---

**Ready to experience the power of distributed systems?** 🚀

Start the system with `./makeProject_mac.sh` (macOS) or `./makeProject.sh` (Linux) and explore the admin console at http://127.0.0.1:8080/admin!
