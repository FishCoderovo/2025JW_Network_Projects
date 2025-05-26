激活虚拟环境：
source venv/bin/activate

运行抓取程序：
python3 scraper.py http://example.com   # 替换为要抓取的HTTP网址

示例网站
http://httpbin.org/image/jpeg
解释： 执行抓取脚本，结果将保存在 downloads/ 目录下。


如果报错
删除旧的虚拟环境文件夹：
rm -rf venv
这会彻底删除所有与旧虚拟环境相关的文件。

重新创建虚拟环境：
python3 -m venv venv

重新激活新的虚拟环境：
source venv/bin/activate
您会再次看到 (venv) 提示符。

在新的虚拟环境中重新安装 beautifulsoup4 和 lxml：
pip install beautifulsoup4 lxml

安装完成后，再次尝试运行网页抓取程序：
python3 scraper.py http://example.com
