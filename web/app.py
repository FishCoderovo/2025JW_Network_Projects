import os
from flask import Flask, request, redirect, url_for, render_template, send_from_directory, flash
from werkzeug.utils import secure_filename
from urllib.parse import unquote

app = Flask(__name__)

# 配置上传文件的总根目录，所有文件都将保存在这个目录下
# 确保这个目录存在，如果不存在Flask会报错
UPLOAD_FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'uploads')
ALLOWED_EXTENSIONS = {'txt', 'pdf', 'png', 'jpg', 'jpeg', 'gif', 'zip', 'rar', '7z', 'doc', 'docx', 'xls', 'xlsx', 'ppt', 'pptx', 'mp4', 'mp3', 'json', 'xml', 'html', 'css', 'js', 'py'} # 增加更多常见文件类型

app.config['UPLOAD_FOLDER'] = UPLOAD_FOLDER
app.config['MAX_CONTENT_LENGTH'] = 16 * 1024 * 1024  # 限制文件大小为16MB (可根据需求调整)
app.secret_key = 'your_super_secret_key_here' # 非常重要！用于flash消息，生产环境应使用更长、更安全的密钥

# 辅助函数：检查文件扩展名是否允许
def allowed_file(filename):
    return '.' in filename and \
           filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS

# 辅助函数：确保路径在允许的上传目录下且安全
def safe_path_join(base_path, *paths):
    # 构建目标路径的绝对路径
    target_path = os.path.join(base_path, *paths)
    abs_base_path = os.path.abspath(base_path)
    abs_target_path = os.path.abspath(target_path)

    # 检查目标路径是否以基准路径开头，防止跳出限定目录
    # os.path.commonpath 返回两个或多个路径的共同最长路径前缀
    # 如果共同前缀不是abs_base_path本身，说明abs_target_path在abs_base_path之外
    common_prefix = os.path.commonpath([abs_base_path, abs_target_path])
    if common_prefix != abs_base_path:
        print(f"SECURITY ALERT: Attempted to access path outside UPLOAD_FOLDER. Base: {abs_base_path}, Target: {abs_target_path}, Common: {common_prefix}")
        return None # 路径不安全

    return abs_target_path

@app.route('/', defaults={'path': ''})
@app.route('/<path:path>')
def index(path):
    # 解码URL路径，因为path参数可能包含URL编码（如空格被编码为%20）
    current_path = unquote(path).strip('/') # 移除路径开头的斜杠，防止与后续join冲突
    
    full_current_path = safe_path_join(app.config['UPLOAD_FOLDER'], current_path)

    if full_current_path is None or not os.path.exists(full_current_path):
        flash('Requested path does not exist or is invalid.', 'error')
        return redirect(url_for('index')) # 重定向到根目录

    if not os.path.isdir(full_current_path):
        # 如果请求的路径不是一个目录（而是一个文件），则重定向到其父目录
        flash('Requested path is a file, not a directory. Redirecting to parent.', 'info')
        # os.path.dirname(current_path) 获取当前路径的父目录部分
        return redirect(url_for('index', path=os.path.dirname(current_path).strip('/')))

    files = []
    directories = []

    try:
        # 列出当前目录下的所有文件和目录
        with os.scandir(full_current_path) as entries:
            for entry in entries:
                if entry.name.startswith('.'): # 忽略隐藏文件/目录
                    continue
                if entry.is_dir():
                    directories.append(entry.name)
                else:
                    files.append(entry.name)
    except Exception as e:
        flash(f'Error listing directory: {e}', 'error')
        return redirect(url_for('index'))

    # 对文件和目录进行排序
    directories.sort()
    files.sort()

    # 构建返回上一级目录的路径
    parent_path = os.path.dirname(current_path).strip('/') if current_path else None
    
    # 传递给模板的数据
    return render_template('index.html',
                           current_path=current_path,
                           files=files,
                           directories=directories,
                           parent_path=parent_path)

@app.route('/upload', methods=['POST'])
def upload_file():
    # 获取当前路径（从隐藏字段传递过来）
    current_path = request.form.get('current_path', '').strip('/')
    target_dir = safe_path_join(app.config['UPLOAD_FOLDER'], current_path)

    if target_dir is None:
        flash('Invalid upload path.', 'error')
        return redirect(url_for('index', path=current_path))

    # 确保目标上传目录存在，如果不存在则创建
    if not os.path.exists(target_dir):
        try:
            os.makedirs(target_dir)
        except OSError as e:
            flash(f"Error creating directory {target_dir}: {e}", 'error')
            return redirect(url_for('index', path=current_path))

    if 'file' not in request.files:
        flash('No file part', 'error')
        return redirect(url_for('index', path=current_path))
    
    file = request.files['file']
    if file.filename == '':
        flash('No selected file', 'error')
        return redirect(url_for('index', path=current_path))

    if file and allowed_file(file.filename):
        filename = secure_filename(file.filename)
        # 检查文件名是否已存在，如果存在可以考虑重命名或提示用户
        file_path = os.path.join(target_dir, filename)
        if os.path.exists(file_path):
            flash(f'File "{filename}" already exists. Overwriting.', 'warning') # 提示用户
        try:
            file.save(file_path)
            flash(f'File "{filename}" uploaded successfully to "/uploads/{current_path}"', 'success')
        except Exception as e:
            flash(f'Error saving file: {e}', 'error')
    else:
        flash('File type not allowed or no file selected.', 'error')
    
    return redirect(url_for('index', path=current_path))

@app.route('/download/<path:filepath>')
def download_file(filepath):
    # 解码URL路径
    decoded_filepath = unquote(filepath)
    
    # 确保文件路径在UPLOAD_FOLDER内部
    full_file_path = safe_path_join(app.config['UPLOAD_FOLDER'], decoded_filepath)
    
    if full_file_path is None: # safe_path_join返回None表示路径不安全
        flash('Invalid download path.', 'error')
        return redirect(url_for('index', path=os.path.dirname(decoded_filepath).strip('/')))
    
    if not os.path.exists(full_file_path):
        flash('File not found.', 'error')
        return redirect(url_for('index', path=os.path.dirname(decoded_filepath).strip('/')))
    
    if os.path.isdir(full_file_path): # 防止下载目录
        flash('Cannot download directories.', 'error')
        return redirect(url_for('index', path=decoded_filepath.strip('/')))

    # 从父目录发送文件
    directory = os.path.dirname(full_file_path)
    filename = os.path.basename(full_file_path)
    try:
        # as_attachment=True 强制浏览器下载而不是显示
        return send_from_directory(directory, filename, as_attachment=True)
    except Exception as e:
        flash(f'Error downloading file: {e}', 'error')
        return redirect(url_for('index', path=os.path.dirname(decoded_filepath).strip('/')))

@app.route('/create_dir', methods=['POST'])
def create_dir():
    current_path = request.form.get('current_path', '').strip('/')
    dir_name = request.form.get('dir_name')

    if not dir_name:
        flash('Directory name cannot be empty.', 'error')
        return redirect(url_for('index', path=current_path))
    
    # 清理目录名，防止特殊字符或路径遍历
    secure_dir_name = secure_filename(dir_name)
    if not secure_dir_name:
        flash('Invalid directory name after sanitization. Avoid special characters.', 'error')
        return redirect(url_for('index', path=current_path))

    full_path_to_create = safe_path_join(app.config['UPLOAD_FOLDER'], current_path, secure_dir_name)

    if full_path_to_create is None:
        flash('Invalid path for directory creation.', 'error')
        return redirect(url_for('index', path=current_path))

    try:
        os.makedirs(full_path_to_create, exist_ok=True) # exist_ok=True 避免目录已存在时报错
        flash(f'Directory "{secure_dir_name}" created successfully in "/uploads/{current_path}"', 'success')
    except OSError as e:
        flash(f'Error creating directory: {e}', 'error')
    
    return redirect(url_for('index', path=current_path))

@app.route('/delete_item/<path:item_path>', methods=['POST'])
def delete_item(item_path):
    decoded_item_path = unquote(item_path)
    full_item_path = safe_path_join(app.config['UPLOAD_FOLDER'], decoded_item_path)
    
    if full_item_path is None: # safe_path_join返回None表示路径不安全
        flash('Invalid deletion path.', 'error')
        return redirect(url_for('index', path=os.path.dirname(decoded_item_path).strip('/')))

    if not os.path.exists(full_item_path):
        flash('Item not found.', 'error')
        return redirect(url_for('index', path=os.path.dirname(decoded_item_path).strip('/')))

    try:
        if os.path.isdir(full_item_path):
            if not os.listdir(full_item_path): # 检查目录是否为空
                os.rmdir(full_item_path) # rmdir 只能删除空目录
                flash(f'Directory "{os.path.basename(decoded_item_path)}" deleted successfully.', 'success')
            else:
                flash(f'Error deleting directory "{os.path.basename(decoded_item_path)}": Directory is not empty. Only empty directories can be deleted.', 'error')
        else:
            os.remove(full_item_path)
            flash(f'File "{os.path.basename(decoded_item_path)}" deleted successfully.', 'success')
    except OSError as e:
        flash(f'Error deleting item "{os.path.basename(decoded_item_path)}": {e}', 'error')
    
    return redirect(url_for('index', path=os.path.dirname(decoded_item_path).strip('/')))


if __name__ == '__main__':
    # 确保 uploads 目录存在
    if not os.path.exists(app.config['UPLOAD_FOLDER']):
        os.makedirs(app.config['UPLOAD_FOLDER'])
    app.run(debug=True, host='0.0.0.0') # 允许从虚拟机外部访问