import os
from flask import Flask, request, redirect, url_for, render_template, send_from_directory, flash
from werkzeug.utils import secure_filename

# 配置上传文件保存的目录
UPLOAD_FOLDER = 'uploads'
ALLOWED_EXTENSIONS = {'txt', 'pdf', 'png', 'jpg', 'jpeg', 'gif', 'zip', 'tar', 'gz'} # 允许上传的文件类型

app = Flask(__name__)
app.config['UPLOAD_FOLDER'] = UPLOAD_FOLDER
app.config['MAX_CONTENT_LENGTH'] = 16 * 1024 * 1024  # 16 MB 最大上传大小
app.secret_key = 'your_super_secret_key_here' # 替换为随机生成的密钥，生产环境请从环境变量加载

# 确保上传目录存在
if not os.path.exists(UPLOAD_FOLDER):
    os.makedirs(UPLOAD_FOLDER)
    print(f"Created upload directory: {UPLOAD_FOLDER}")

def allowed_file(filename):
    """检查文件扩展名是否在允许的列表中"""
    return '.' in filename and \
           filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS

@app.route('/')
def index():
    """显示文件列表和上传界面"""
    files = []
    for root, dirs, filenames in os.walk(app.config['UPLOAD_FOLDER']):
        # 构建相对路径，确保在页面上显示正确
        relative_path = os.path.relpath(root, app.config['UPLOAD_FOLDER'])
        for name in filenames:
            files.append(os.path.join(relative_path, name).replace('\\', '/')) # 统一路径分隔符
    # 获取目录列表
    directories = [d for d in os.listdir(app.config['UPLOAD_FOLDER']) if os.path.isdir(os.path.join(app.config['UPLOAD_FOLDER'], d))]
    return render_template('index.html', files=files, current_dir='/', directories=directories)

@app.route('/upload', methods=['POST'])
def upload_file():
    """处理文件上传（单文件和多文件）"""
    if 'file' not in request.files:
        flash('No file part')
        return redirect(request.url)

    files = request.files.getlist('file') # 获取所有文件对象
    uploaded_count = 0

    for file in files:
        if file.filename == '':
            flash('No selected file')
            continue

        if file and allowed_file(file.filename):
            filename = secure_filename(file.filename) # 安全文件名
            filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)
            
            # 检查文件是否已存在，如果需要可添加重命名逻辑
            if os.path.exists(filepath):
                flash(f'File "{filename}" already exists. Skipping or rename it.')
                continue

            file.save(filepath)
            uploaded_count += 1
            flash(f'File "{filename}" uploaded successfully.')
        else:
            flash(f'File "{file.filename}" not allowed or invalid.')

    if uploaded_count > 0:
        flash(f'Total {uploaded_count} file(s) uploaded.')
    return redirect(url_for('index'))

@app.route('/download/<path:filename>')
def download_file(filename):
    """处理文件下载"""
    # 确保文件存在于 uploads 目录或其子目录中
    return send_from_directory(app.config['UPLOAD_FOLDER'], filename, as_attachment=True)

@app.route('/create_dir', methods=['POST'])
def create_directory():
    """处理目录创建"""
    dirname = request.form.get('dirname')
    if not dirname:
        flash('Directory name cannot be empty.')
        return redirect(url_for('index'))

    # 使用 secure_filename 确保目录名安全，防止路径遍历攻击
    secure_dirname = secure_filename(dirname)
    dir_path = os.path.join(app.config['UPLOAD_FOLDER'], secure_dirname)

    if os.path.exists(dir_path):
        flash(f'Directory "{secure_dirname}" already exists.')
    else:
        try:
            os.makedirs(dir_path)
            flash(f'Directory "{secure_dirname}" created successfully.')
        except OSError as e:
            flash(f'Error creating directory: {e}')
    return redirect(url_for('index'))

if __name__ == '__main__':
    # 启动Flask应用，调试模式开启 (生产环境请关闭)
    app.run(debug=True, host='0.0.0.0', port=5000)