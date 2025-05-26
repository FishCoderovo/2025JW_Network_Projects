import socket
import re
import os
import sys
from urllib.parse import urlparse
from bs4 import BeautifulSoup

# 定义下载保存目录
DOWNLOADS_DIR = 'downloads'

def create_downloads_dir():
    """创建下载目录"""
    if not os.path.exists(DOWNLOADS_DIR):
        os.makedirs(DOWNLOADS_DIR)
        print(f"Created download directory: {DOWNLOADS_DIR}")

# 简单的文件名清理函数，避免使用werkzeug.utils.secure_filename
def simple_secure_filename(filename):
    """
    简单的文件名清理，替换掉可能导致问题的字符，
    并确保只保留字母、数字、点、下划线和连字符。
    """
    # 替换非法字符为下划线
    filename = re.sub(r'[^a-zA-Z0-9_.-]', '_', filename)
    # 替换连续下划线为单个
    filename = re.sub(r'__+', '_', filename)
    # 移除开头和结尾的特殊字符
    filename = filename.strip('._-')
    if not filename:
        filename = "untitled" # 避免空文件名
    return filename

def get_http_response_via_socket(url):
    """
    通过Socket发送HTTP GET请求并接收响应。
    处理HTTP头和内容分离，并尝试处理Content-Length。
    """
    parsed_url = urlparse(url)
    host = parsed_url.netloc
    path = parsed_url.path if parsed_url.path else '/'
    # 默认使用80端口，如果协议是http
    port = 80

    # 对于HTTPS，此简单示例不直接支持
    if parsed_url.scheme == 'https':
        print("HTTPS not directly supported by raw socket in this example. Please use HTTP URLs.")
        return None, None

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(10) # 设置超时
        s.connect((host, port))

        request_line = f"GET {path} HTTP/1.1\r\n"
        headers = f"Host: {host}\r\nConnection: close\r\nUser-Agent: SimpleSocketScraper/1.0\r\n\r\n"
        request = (request_line + headers).encode('utf-8')
        s.sendall(request)

        # 接收响应
        response_bytes = b''
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            response_bytes += chunk
        s.close()

        # 分离头部和正文
        header_end = response_bytes.find(b'\r\n\r\n')
        if header_end == -1:
            print(f"Error: Could not find end of headers for {url}")
            return None, None

        headers_raw = response_bytes[:header_end].decode('latin-1') # 头部通常是latin-1编码
        body_raw = response_bytes[header_end + 4:] # +4 for '\r\n\r\n'

        headers_dict = {}
        for line in headers_raw.split('\r\n'):
            if ':' in line:
                key, value = line.split(':', 1)
                headers_dict[key.strip().lower()] = value.strip()
        
        # 检查Content-Length，并尝试确保接收完整内容
        content_length_str = headers_dict.get('content-length')
        if content_length_str:
            content_length = int(content_length_str)
            if len(body_raw) < content_length:
                print(f"Warning: Body received ({len(body_raw)} bytes) is less than Content-Length ({content_length} bytes) for {url}")
        
        return headers_dict, body_raw

    except socket.error as e:
        print(f"Socket error for {url}: {e}")
        return None, None
    except Exception as e:
        print(f"An unexpected error occurred for {url}: {e}")
        return None, None

def save_text_to_file(text_content, filename):
    """将文本内容保存到文件"""
    filepath = os.path.join(DOWNLOADS_DIR, filename)
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(text_content)
    print(f"Text content saved to: {filepath}")

def download_image_via_socket(image_url, image_filename):
    """
    通过Socket下载图片并保存。
    """
    parsed_url = urlparse(image_url)
    host = parsed_url.netloc
    path = parsed_url.path if parsed_url.path else '/'
    port = 80

    # 对于HTTPS，此简单示例不直接支持
    if parsed_url.scheme == 'https':
        print(f"Skipping HTTPS image: {image_url}")
        return

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(10)
        s.connect((host, port))

        request_line = f"GET {path} HTTP/1.1\r\n"
        headers = f"Host: {host}\r\nConnection: close\r\nUser-Agent: SimpleSocketScraper/1.0\r\n\r\n"
        request = (request_line + headers).encode('utf-8')
        s.sendall(request)

        # 接收响应
        response_bytes = b''
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            response_bytes += chunk
        s.close()

        # 分离头部和正文
        header_end = response_bytes.find(b'\r\n\r\n')
        if header_end == -1:
            print(f"Error: Could not find end of headers for image {image_url}")
            return

        headers_raw = response_bytes[:header_end].decode('latin-1')
        body_raw = response_bytes[header_end + 4:]

        # 可以解析headers_raw来获取Content-Length等信息，但此处直接保存整个body
        filepath = os.path.join(DOWNLOADS_DIR, image_filename)
        with open(filepath, 'wb') as f:
            f.write(body_raw)
        print(f"Image downloaded and saved to: {filepath}")

    except socket.error as e:
        print(f"Socket error downloading image {image_url}: {e}")
    except Exception as e:
        print(f"An unexpected error occurred downloading image {image_url}: {e}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python scraper.py <URL>")
        sys.exit(1)

    target_url = sys.argv[1]
    create_downloads_dir()

    print(f"Grabbing data from: {target_url}")
    headers, body_bytes = get_http_response_via_socket(target_url)

    if headers is None or body_bytes is None:
        print("Failed to get HTTP response.")
        return

    content_type = headers.get('content-type', '').lower()

    if 'text/html' in content_type:
        # 尝试解码HTML内容
        charset = 'utf-8' # 默认UTF-8
        match = re.search(r'charset=([\w-]+)', content_type)
        if match:
            charset = match.group(1)
        
        try:
            html_content = body_bytes.decode(charset)
        except UnicodeDecodeError:
            print(f"Could not decode with {charset}, trying utf-8...")
            html_content = body_bytes.decode('utf-8', errors='ignore')
        except Exception as e:
            print(f"Error decoding HTML: {e}, trying utf-8 with ignore errors...")
            html_content = body_bytes.decode('utf-8', errors='ignore')


        soup = BeautifulSoup(html_content, 'lxml')

        # 抓取文本信息
        page_title = soup.title.string if soup.title else 'No Title'
        text_filename = simple_secure_filename(f"{page_title[:50]}_text.txt") # 限制文件名长度
        save_text_to_file(soup.get_text(), text_filename)

        # 抓取图片信息并下载
        image_tags = soup.find_all('img')
        print(f"Found {len(image_tags)} image tags.")
        for i, img in enumerate(image_tags):
            img_src = img.get('src')
            if img_src:
                # 完善图片URL（处理相对路径）
                if not img_src.startswith(('http://', 'https://', '//')):
                    # 简单拼接，更复杂的应使用urljoin
                    if img_src.startswith('/'): # 根相对路径
                        img_url = urlparse(target_url).scheme + "://" + urlparse(target_url).netloc + img_src
                    else: # 相对路径
                        # 获取基础URL的目录部分
                        base_url_parsed = urlparse(target_url)
                        base_path = os.path.dirname(base_url_parsed.path)
                        # 确保路径以斜杠结尾，以便正确拼接
                        if base_path and not base_path.endswith('/'):
                            base_path += '/'
                        elif not base_path: # 如果是根路径
                            base_path = '/'
                        
                        img_url = base_url_parsed.scheme + "://" + base_url_parsed.netloc + base_path + img_src
                elif img_src.startswith('//'): # 协议相对路径
                    img_url = urlparse(target_url).scheme + ":" + img_src
                else:
                    img_url = img_src
                
                # 提取文件名
                img_name = os.path.basename(urlparse(img_url).path)
                if not img_name or '.' not in img_name: # 如果路径末尾没有文件名或没有扩展名，给一个默认名
                    # 尝试从Content-Type获取扩展名，或者使用通用jpg
                    content_type_img = headers.get('content-type', '').lower()
                    if 'image/' in content_type_img:
                        ext = content_type_img.split('/')[-1]
                        img_name = f"image_{i}.{ext}"
                    else:
                        img_name = f"image_{i}.jpg" # 默认jpg
                        
                img_filename = simple_secure_filename(f"img_{i}_{img_name}") # 加入索引防止重名

                print(f"Attempting to download image: {img_url}")
                download_image_via_socket(img_url, img_filename)
    else:
        print(f"Content-Type is {content_type}, not HTML. Only text/html is processed for text/image extraction.")
        # 如果是其他类型，可以直接保存原始文件
        file_extension = content_type.split('/')[-1] if '/' in content_type else 'bin'
        filename = simple_secure_filename(f"downloaded_content.{file_extension}")
        filepath = os.path.join(DOWNLOADS_DIR, filename)
        with open(filepath, 'wb') as f:
            f.write(body_bytes)
        print(f"Non-HTML content saved to: {filepath}")

if __name__ == "__main__":
    main()