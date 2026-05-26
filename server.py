import tornado
import tornado.web
import tornado.websocket
import tornado.httpserver
import tornado.ioloop
import pymysql
import hashlib
import threading
import json
import datetime
from concurrent.futures import ThreadPoolExecutor

# MySQL 数据库配置
DB_CONFIG = {
    'host': 'localhost',
    'user': 'root',
    'password': 'Zhangli1124...',
    'database': 'wechat_db',
    'charset': 'utf8mb4'
}

# 在线用户映射: user_id -> WebSocketHandler
online_users = {}
# 保护在线用户映射的锁
online_users_lock = threading.Lock()

# 已处理的消息 ID（用于去重，防止重发导致重复保存）
processed_msg_ids = {}

# 数据库线程池：避免同步数据库操作阻塞 Tornado 事件循环
db_executor = ThreadPoolExecutor(max_workers=10)

# === 数据库操作函数 ===

def get_db_connection():
    """获取数据库连接"""
    return pymysql.connect(**DB_CONFIG) # 根据固定的结构来连接数据库

def init_db():
    """初始化数据库表（如果不存在）"""
    conn = get_db_connection()
    cursor = conn.cursor()
    
    # 用户表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS users (
            id INT AUTO_INCREMENT PRIMARY KEY,
            username VARCHAR(50) UNIQUE NOT NULL,
            password VARCHAR(255) NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    ''')
    
    # 好友关系表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS friends (
            id INT AUTO_INCREMENT PRIMARY KEY,
            user_id INT NOT NULL,
            friend_id INT NOT NULL,
            status ENUM('pending', 'accepted') DEFAULT 'pending',
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            UNIQUE KEY (user_id, friend_id)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    ''')
    
    # 消息表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS messages (
            id INT AUTO_INCREMENT PRIMARY KEY,
            from_user_id INT NOT NULL,
            to_user_id INT NOT NULL,
            content TEXT NOT NULL,
            is_read BOOLEAN DEFAULT FALSE,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX (from_user_id, to_user_id),
            INDEX (to_user_id, from_user_id)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    ''')
    
    # 离线消息表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS offline_messages (
            id INT AUTO_INCREMENT PRIMARY KEY,
            from_user_id INT NOT NULL,
            to_user_id INT NOT NULL,
            content TEXT NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX (to_user_id)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    ''')
    
    conn.commit()
    cursor.close()
    conn.close()
    print("数据库初始化成功")

def hash_password(password):
    """密码哈希"""
    return hashlib.sha256(password.encode()).hexdigest()

def check_user_exists(username):
    """检查用户是否存在"""
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT id FROM users WHERE username = %s', (username,))
    result = cursor.fetchone()
    cursor.close()
    conn.close()
    return result is not None

def get_user_by_username(username):
    """根据用户名获取用户"""
    conn = get_db_connection()
    cursor = conn.cursor(pymysql.cursors.DictCursor)
    cursor.execute('SELECT id, username FROM users WHERE username = %s', (username,))
    result = cursor.fetchone()
    cursor.close()
    conn.close()
    return result

def get_user_by_id(user_id):
    """根据ID获取用户"""
    conn = get_db_connection()
    cursor = conn.cursor(pymysql.cursors.DictCursor)
    cursor.execute('SELECT id, username FROM users WHERE id = %s', (user_id,))
    result = cursor.fetchone()
    cursor.close()
    conn.close()
    return result

def create_user(username, password):
    """创建新用户"""
    conn = get_db_connection()
    cursor = conn.cursor()
    try:
        cursor.execute('INSERT INTO users (username, password) VALUES (%s, %s)', 
                      (username, hash_password(password)))
        conn.commit()
        user_id = cursor.lastrowid
        cursor.close()
        conn.close()
        return user_id
    except pymysql.IntegrityError:
        cursor.close()
        conn.close()
        return None

def verify_user(username, password):
    """验证用户登录"""
    conn = get_db_connection()
    cursor = conn.cursor(pymysql.cursors.DictCursor)
    cursor.execute('SELECT id, username FROM users WHERE username = %s AND password = %s',
                  (username, hash_password(password)))
    result = cursor.fetchone()
    cursor.close()
    conn.close()
    return result

def add_friend(user_id, friend_username):
    """添加好友请求"""
    friend = get_user_by_username(friend_username)
    if not friend:
        return False, "用户不存在"
    
    if user_id == friend['id']:
        return False, "不能添加自己为好友"
    
    conn = get_db_connection()
    cursor = conn.cursor()
    try:
        cursor.execute('INSERT INTO friends (user_id, friend_id, status) VALUES (%s, %s, %s)',
                      (user_id, friend['id'], 'pending'))
        conn.commit()
        cursor.close()
        conn.close()
        return True, "好友请求已发送"
    except pymysql.IntegrityError:
        cursor.close()
        conn.close()
        return False, "好友关系已存在"

def handle_friend_request(user_id, friend_id, accept):
    """处理好友请求"""
    conn = get_db_connection()
    cursor = conn.cursor()
    
    if accept:
        # 接受请求，双向添加好友
        cursor.execute('UPDATE friends SET status = %s WHERE user_id = %s AND friend_id = %s',
                      ('accepted', friend_id, user_id))
        cursor.execute('INSERT INTO friends (user_id, friend_id, status) VALUES (%s, %s, %s)',
                      (user_id, friend_id, 'accepted'))
    else:
        # 拒绝请求
        cursor.execute('DELETE FROM friends WHERE user_id = %s AND friend_id = %s',
                      (friend_id, user_id))
    
    conn.commit()
    cursor.close()
    conn.close()
    return True, "操作成功"

def get_friends(user_id):
    """获取好友列表"""
    conn = get_db_connection()
    cursor = conn.cursor(pymysql.cursors.DictCursor)
    cursor.execute('''
        SELECT u.id, u.username 
        FROM friends f 
        JOIN users u ON f.friend_id = u.id 
        WHERE f.user_id = %s AND f.status = 'accepted'
    ''', (user_id,))
    result = cursor.fetchall()
    cursor.close()
    conn.close()
    return result

def get_friend_requests(user_id):
    """获取好友请求列表"""
    conn = get_db_connection()
    cursor = conn.cursor(pymysql.cursors.DictCursor)
    cursor.execute('''
        SELECT u.id, u.username 
        FROM friends f 
        JOIN users u ON f.user_id = u.id 
        WHERE f.friend_id = %s AND f.status = 'pending'
    ''', (user_id,))
    result = cursor.fetchall()
    cursor.close()
    conn.close()
    return result

def remove_friend(user_id, friend_id):
    """删除好友"""
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('DELETE FROM friends WHERE (user_id = %s AND friend_id = %s) OR (user_id = %s AND friend_id = %s)',
                  (user_id, friend_id, friend_id, user_id))
    conn.commit()
    cursor.close()
    conn.close()
    return True

def get_friend_request_by_id(request_id):
    """根据请求ID获取好友请求信息"""
    conn = get_db_connection()
    cursor = conn.cursor(pymysql.cursors.DictCursor)
    cursor.execute('''
        SELECT user_id, friend_id 
        FROM friends 
        WHERE id = %s AND status = 'pending'
    ''', (request_id,))
    result = cursor.fetchone()
    cursor.close()
    conn.close()
    return result

def save_message(from_user_id, to_user_id, content):
    """保存消息到数据库"""
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('INSERT INTO messages (from_user_id, to_user_id, content) VALUES (%s, %s, %s)',
                  (from_user_id, to_user_id, content))
    conn.commit()
    message_id = cursor.lastrowid
    cursor.close()
    conn.close()
    return message_id

def get_messages(user_id1, user_id2, limit=20, offset=0):
    """获取两个用户之间的消息
    
    Args:
        user_id1: 当前用户ID
        user_id2: 好友ID
        limit: 每次返回的消息数量
        offset: 已加载的消息数量
    
    使用降序查询，返回最新消息在前，客户端需要反转以显示正确顺序
    """
    conn = get_db_connection()
    cursor = conn.cursor(pymysql.cursors.DictCursor)
    cursor.execute('''
        SELECT m.id, m.from_user_id, m.to_user_id, m.content, m.created_at, u.username as from_username
        FROM messages m
        JOIN users u ON m.from_user_id = u.id
        WHERE (m.from_user_id = %s AND m.to_user_id = %s) OR (m.from_user_id = %s AND m.to_user_id = %s)
        ORDER BY m.created_at DESC
        LIMIT {} OFFSET {}
    '''.format(int(limit), int(offset)), (user_id1, user_id2, user_id2, user_id1))
    result = cursor.fetchall()
    cursor.close()
    conn.close()
    
    # 将 datetime 对象转换为字符串
    for msg in result:
        if isinstance(msg.get('created_at'), datetime.datetime):
            msg['created_at'] = msg['created_at'].isoformat()
    
    return result

def save_offline_message(from_user_id, to_user_id, content):
    """保存离线消息"""
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('INSERT INTO offline_messages (from_user_id, to_user_id, content) VALUES (%s, %s, %s)',
                  (from_user_id, to_user_id, content))
    conn.commit()
    cursor.close()
    conn.close()

def get_offline_messages(user_id):
    """获取离线消息"""
    conn = get_db_connection()
    cursor = conn.cursor(pymysql.cursors.DictCursor)
    cursor.execute('''
        SELECT om.id, om.from_user_id, om.to_user_id, om.content, om.created_at, u.username as from_username
        FROM offline_messages om
        JOIN users u ON om.from_user_id = u.id
        WHERE om.to_user_id = %s
        ORDER BY om.created_at ASC
    ''', (user_id,))
    result = cursor.fetchall()
    
    # 删除已获取的离线消息
    cursor.execute('DELETE FROM offline_messages WHERE to_user_id = %s', (user_id,))
    conn.commit()
    
    cursor.close()
    conn.close()
    
    # 将 datetime 对象转换为字符串
    for msg in result:
        if isinstance(msg.get('created_at'), datetime.datetime):
            msg['created_at'] = msg['created_at'].isoformat()
    
    return result

# === Tornado 请求处理器 === HTTP 处理类 ===

class BaseHandler(tornado.web.RequestHandler):
    """基础处理器，提供 JSON 响应方法"""
    
    # 在跨域的时候需要设置跨域头
    def set_default_headers(self):
        """设置跨域头"""
        self.set_header("Access-Control-Allow-Origin", "*")
        self.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
        self.set_header("Access-Control-Allow-Headers", "Content-Type")
    
    def options(self):
        """处理 OPTIONS 请求"""
        self.set_status(200)
    
    def write_json(self, data):
        """发送 JSON 响应"""
        self.set_header("Content-Type", "application/json")
        self.write(json.dumps(data))

class LoginHandler(BaseHandler):
    """登录接口"""
    
    async def post(self):
        try:
            data = json.loads(self.request.body.decode('utf-8'))
            username = data.get('username')
            password = data.get('password')
            
            if not username or not password:
                self.write_json({'success': False, 'message': '用户名和密码不能为空'})
                return
            
            # 把verify_user函数放到线程池中执行，不加await返回的是一个Future对象，而不是执行完后的结果
            user = await tornado.ioloop.IOLoop.current().run_in_executor(
                db_executor, verify_user, username, password)   # 线程池、函数、参数1、参数2
            if user:
                self.write_json({'success': True, 'data': user})
            else:
                self.write_json({'success': False, 'message': '用户名或密码错误'})
        except Exception as e:
            self.write_json({'success': False, 'message': str(e)})

class RegisterHandler(BaseHandler):
    """注册接口"""
    
    async def post(self):
        try:
            data = json.loads(self.request.body.decode('utf-8'))
            username = data.get('username')
            password = data.get('password')
            
            if not username or not password:
                self.write_json({'success': False, 'message': '用户名和密码不能为空'})
                return
            
            exists = await tornado.ioloop.IOLoop.current().run_in_executor(
                db_executor, check_user_exists, username)
            if exists:
                self.write_json({'success': False, 'message': '用户名已存在'})
                return
            
            user_id = await tornado.ioloop.IOLoop.current().run_in_executor(
                db_executor, create_user, username, password)
            if user_id:
                self.write_json({'success': True, 'data': {'id': user_id, 'username': username}})
            else:
                self.write_json({'success': False, 'message': '注册失败'})
        except Exception as e:
            self.write_json({'success': False, 'message': str(e)})

class AddFriendHandler(BaseHandler):
    """添加好友接口"""
    
    async def post(self):
        try:
            data = json.loads(self.request.body.decode('utf-8'))
            user_id = data.get('user_id')
            friend_username = data.get('friend_username')
            
            if not user_id or not friend_username:
                self.write_json({'success': False, 'message': '参数错误'})
                return
            
            success, message = await tornado.ioloop.IOLoop.current().run_in_executor(
                db_executor, add_friend, user_id, friend_username)
            self.write_json({'success': success, 'message': message})
        except Exception as e:
            self.write_json({'success': False, 'message': str(e)})

class HandleFriendRequestHandler(BaseHandler):
    """处理好友请求接口"""
    
    async def post(self):
        try:
            data = json.loads(self.request.body.decode('utf-8'))
            
            request_id = data.get('request_id')
            action = data.get('action')
            user_id = data.get('user_id')
            friend_id = data.get('friend_id')
            accept = data.get('accept', False)
            
            # 如果是格式1（request_id + action），先在线程池中查询请求信息
            if request_id and action:
                result = await tornado.ioloop.IOLoop.current().run_in_executor(
                    db_executor, get_friend_request_by_id, request_id)
                
                if not result:
                    self.write_json({'success': False, 'message': '请求不存在'})
                    return
                
                user_id = result['friend_id']
                friend_id = result['user_id']
                accept = (action == 'accept')
            
            if not user_id or not friend_id:
                self.write_json({'success': False, 'message': '参数错误'})
                return
            
            success, message = await tornado.ioloop.IOLoop.current().run_in_executor(
                db_executor, handle_friend_request, user_id, friend_id, accept)
            self.write_json({'success': success, 'message': message})
        except Exception as e:
            self.write_json({'success': False, 'message': str(e)})

class FriendsHandler(BaseHandler):
    """获取好友列表接口"""
    
    async def get(self):
        try:
            user_id = self.get_argument('user_id', None)
            
            if not user_id:
                self.write_json({'success': False, 'message': '参数错误'})
                return
            
            user_id_int = int(user_id)
            print(f"获取好友列表 - user_id: {user_id_int}")
            
            friends = await tornado.ioloop.IOLoop.current().run_in_executor(
                db_executor, get_friends, user_id_int)
            print(f"找到 {len(friends)} 个好友")
            
            self.write_json({'success': True, 'data': friends})
        except Exception as e:
            print(f"获取好友列表错误: {e}")
            self.write_json({'success': False, 'message': str(e)})

class FriendRequestsHandler(BaseHandler):
    """获取好友请求接口"""
    
    async def get(self):
        try:
            user_id = self.get_argument('user_id', None)
            
            if not user_id:
                self.write_json({'success': False, 'message': '参数错误'})
                return
            
            requests = await tornado.ioloop.IOLoop.current().run_in_executor(
                db_executor, get_friend_requests, int(user_id))
            self.write_json({'success': True, 'data': requests})
        except Exception as e:
            self.write_json({'success': False, 'message': str(e)})

class RemoveFriendHandler(BaseHandler):
    """删除好友接口"""
    
    async def post(self):
        try:
            data = json.loads(self.request.body.decode('utf-8'))
            user_id = data.get('user_id')
            friend_id = data.get('friend_id')
            
            if not user_id or not friend_id:
                self.write_json({'success': False, 'message': '参数错误'})
                return
            
            success = await tornado.ioloop.IOLoop.current().run_in_executor(
                db_executor, remove_friend, user_id, friend_id)
            self.write_json({'success': success})
        except Exception as e:
            self.write_json({'success': False, 'message': str(e)})

class GetMessagesHandler(BaseHandler):
    """获取消息列表接口"""
    
    async def get(self):
        try:
            user_id = self.get_argument('user_id', None)
            friend_id = self.get_argument('friend_id', None)
            limit = int(self.get_argument('limit', 20))
            offset = int(self.get_argument('offset', 0))
            
            if not user_id or not friend_id:
                self.write_json({'success': False, 'message': '参数错误'})
                return
            
            messages = await tornado.ioloop.IOLoop.current().run_in_executor(
                db_executor, get_messages, int(user_id), int(friend_id), limit, offset)
            self.write_json({'success': True, 'data': messages})
        except Exception as e:
            self.write_json({'success': False, 'message': str(e)})

# === WebSocket 处理类 ===

class ChatWebSocketHandler(tornado.websocket.WebSocketHandler):
    """WebSocket 聊天处理器"""
    
    def open(self):
        """新连接建立"""
        print(f"WebSocket 客户端已连接: {self.request.remote_ip}")
        self.user_id = None
    
    def on_message(self, message):
        """处理收到的消息"""
        try:
            data = json.loads(message)
            msg_type = data.get('type')
            msg_data = data.get('data', {})
            
            if msg_type == 'login':
                self.handle_login(msg_data)
            elif msg_type == 'send_message':
                self.handle_send_message(msg_data)
            elif msg_type == 'heartbeat':
                self.write_message(json.dumps({'type': 'heartbeat_response'}))
            else:
                print(f"未知消息类型: {msg_type}")
        except json.JSONDecodeError:
            print(f"无效的 JSON 消息: {message[:100]}")
        except Exception as e:
            print(f"消息处理错误: {e}")
    
    def handle_login(self, data):
        """处理 WebSocket 登录"""
        self.user_id = data.get('user_id')
        if self.user_id:
            with online_users_lock:
                online_users[self.user_id] = self
            print(f"用户 {self.user_id} 已在线")
            
            # 发送离线消息
            offline_messages = get_offline_messages(self.user_id)
            if offline_messages:
                self.write_message(json.dumps({
                    'type': 'offline_messages',
                    'messages': offline_messages
                }))
    
    def handle_send_message(self, data):
        """处理发送消息"""
        from_user_id = data.get('from_user_id')
        to_user_id = data.get('to_user_id')
        content = data.get('content')
        client_msg_id = data.get('client_msg_id', '')
        
        if not from_user_id or not to_user_id or not content:
            self.write_message(json.dumps({
                'type': 'message_send_failed',
                'client_msg_id': client_msg_id,
                'message': '参数错误'
            }))
            return
        
        # 去重检查：如果 client_msg_id 已处理过，直接返回 ACK 不重复保存
        if client_msg_id and client_msg_id in processed_msg_ids:
            self.write_message(json.dumps({
                'type': 'message_sent',
                'client_msg_id': client_msg_id,
                'data': processed_msg_ids[client_msg_id]
            }))
            return
        
        # 保存消息到数据库
        message_id = save_message(from_user_id, to_user_id, content)
        
        # 获取发送者信息
        from_user = get_user_by_id(from_user_id)
        from_username = from_user['username'] if from_user else 'unknown'
        
        message_data = {
            'id': message_id,
            'from_user_id': from_user_id,
            'to_user_id': to_user_id,
            'content': content,
            'from_username': from_username,
            'created_at': 'now'
        }
        
        # 缓存已处理的消息 ID（用于去重）
        if client_msg_id:
            processed_msg_ids[client_msg_id] = message_data
        
        # 通知发送者消息已发送（包含 client_msg_id 用于客户端确认）
        self.write_message(json.dumps({
            'type': 'message_sent',
            'client_msg_id': client_msg_id,
            'data': message_data
        }))
        
        # 尝试直接发送给在线用户
        with online_users_lock:
            if to_user_id in online_users:
                target_ws = online_users[to_user_id]
                target_ws.write_message(json.dumps({
                    'type': 'new_message',
                    'data': message_data
                }))
                print(f"消息已推送给用户 {to_user_id}")
            else:
                # 保存离线消息
                save_offline_message(from_user_id, to_user_id, content)
                print(f"用户 {to_user_id} 离线，消息已存储到离线消息表")
    
    def on_close(self):
        """连接关闭"""
        print(f"WebSocket 客户端已断开连接: {self.request.remote_ip}")
        if self.user_id:
            with online_users_lock:
                if self.user_id in online_users and online_users[self.user_id] == self:
                    del online_users[self.user_id]
                    print(f"用户 {self.user_id} 已离线")
    
    def check_origin(self, origin):
        """允许所有来源"""
        return True

# === 主应用 ===

def main():
    """启动服务器"""
    init_db()
    
    app = tornado.web.Application([
        # HTTP API
        (r'/api/login', LoginHandler),
        (r'/api/register', RegisterHandler),
        (r'/api/add_friend', AddFriendHandler),
        (r'/api/handle_friend_request', HandleFriendRequestHandler),
        (r'/api/friends', FriendsHandler),
        (r'/api/friend_requests', FriendRequestsHandler),
        (r'/api/remove_friend', RemoveFriendHandler),
        (r'/api/get_messages', GetMessagesHandler),
        
        # WebSocket
        (r'/ws', ChatWebSocketHandler),
    ])
    
    http_server = tornado.httpserver.HTTPServer(app)
    http_server.listen(8080, address='0.0.0.0')
    print("服务器已启动，监听端口 8080")
    print("HTTP API: http://0.0.0.0:8080/api/")
    print("WebSocket: ws://0.0.0.0:8080/ws")
    
    # 开始事件循环等待请求
    tornado.ioloop.IOLoop.current().start()

if __name__ == '__main__':
    main()
