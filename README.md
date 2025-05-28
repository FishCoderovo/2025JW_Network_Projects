# 2025JW_Network_Projects: 2025 计算机网络课程设计

## 项目描述

此存储库包含2025年计算机网络课程设计中的三个主要实践项目，旨在深入理解网络协议和应用开发：

1.  **传统 FTP 服务器与客户端 (C 语言)：** 实现了一个符合 FTP 协议基本功能（如文件传输、目录操作、登录验证）的服务器和客户端，并尝试实现主动/被动两种数据传输模式。
2.  **基于 Web 方式的文件传输系统 (Python Flask)：** 一个通过 Web 界面提供文件上传、下载和目录管理功能的简易系统，模拟了部分 FTP 功能，但提供了更友好的用户体验。
3.  **网页抓取技术 (Python Socket + BeautifulSoup)：** 一个基于底层 Socket 编程实现的 HTTP 网页爬虫，用于理解 HTTP 协议的底层交互，能够抓取指定 HTTP 网站的文本内容和图片。

---

## 项目目录结构
2025JW/
├── .gitignore               # Git 忽略文件配置，不上传虚拟环境、编译文件和运行时数据
├── README.md                # 本说明文件
│
├── ftp/                     # 传统C语言FTP项目目录
│   ├── ftp.h                # 头文件：定义常量和结构
│   ├── ftp_client.c         # FTP 客户端源代码
│   ├── ftp_server.c         # FTP 服务器源代码
│   └── Makefile             # 用于编译C语言项目的Makefile
│   └── server_files/        # (此目录需手动创建) 服务器文件存储根目录
│       └── .gitkeep         # 标记空目录以便Git跟踪
│
├── web/                     # Web FTP项目目录 (Python Flask)
│   ├── app.py               # Flask 主应用文件，包含路由和逻辑
│   ├── templates/           # HTML 模板目录
│   │   └── index.html       # Web界面模板
│   ├── static/              # 静态文件目录 (CSS, JS, 图片等)
│   │   └── css/
│   │       └── style.css
│   ├── uploads/             # 用户上传文件的存储目录
│   │   └── .gitkeep         # 标记此空目录以便Git跟踪
│   └── requirements.txt     # Python依赖列表 (Web FTP项目所需库)
│
└── crawl/                   # 网页抓取项目目录 (Python Scraper)
├── scraper.py           # 网页抓取主程序
├── downloads/           # 存放抓取到的文本和图片的目录
│   └── .gitkeep         # 标记此空目录以便Git跟踪
└── requirements.txt     # Python依赖列表 (网页抓取项目所需库)