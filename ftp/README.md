user1
pass1

admin / adminpass

root
12345

* **命令支持：** 实现了与服务器对应的客户端命令：
    * `connect <host> <port>`：连接到服务器
    * `login <username> <password>`：登录
    * `ls` / `dir`：列出当前目录内容
    * `cd <directory>`：切换目录
    * `get <filename>`：从服务器下载文件
    * `put <filename>`：上传文件到服务器
    * `passive`：设置客户端为被动模式（默认）
    * `active`：设置客户端为主动模式
    * `quit`：断开连接并退出

# 2025JW_Network_Projects: 简易FTP服务器与客户端

本项目是2025年计算机网络课程设计，旨在实现一个遵循FTP（文件传输协议）基本规范的服务器和客户端程序。

## 核心功能

* **FTP 服务器：** 支持多客户端连接、用户认证、文件上传/下载、目录列表、目录切换，并支持主动/被动数据传输模式。
* **FTP 客户端：** 提供命令行界面，可连接服务器、登录、执行文件/目录操作（上传、下载、列表、切换），并可在主动/被动模式间切换。

## 如何运行

1.  **克隆项目：**
    ```bash
    git clone [https://github.com/FishCoderovo/2025JW_Network_Projects.git](https://github.com/FishCoderovo/2025JW_Network_Projects.git)
    cd 2025JW_Network_Projects
    ```
2.  **编译：**
    ```bash
    cd ftp_server && make && cd ..
    cd ftp_client && make && cd ..
    ```
3.  **运行服务器：**
    ```bash
    cd ftp_server && ./server
    ```
    （默认监听 21 端口，用户配置在 `ftp_server/users.conf`）
4.  **运行客户端：**
    ```bash
    cd ftp_client && ./client
    ```

## 客户端指令

在 `ftp>` 提示符下输入以下命令：

* `connect <IP> <PORT>`：连接到服务器（如 `connect 127.0.0.1 21`）
* `login <username> <password>`：登录（如 `login user 123`）
* `ls` / `dir`：列出当前目录内容
* `cd <directory>`：切换目录
* `get <filename>`：从服务器下载文件
* `put <filename>`：上传文件到服务器
* `passive`：切换到被动模式（默认）
* `active`：切换到主动模式
* `quit`：断开连接并退出

## 课程设计参考

本项目已实现《网络实习》指导书中要求的核心FTP功能，包括文件上传/下载、目录操作、主动/被动模式、登录验证等。

---