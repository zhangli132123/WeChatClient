"""
服务器压测脚本：测试多线程改造后的性能提升

使用方式：
    pip install requests
    python benchmark.py

压测前请先创建测试用户：
    python benchmark.py --setup
"""
import requests
import threading
import time
import json
import argparse
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

# 服务器地址（根据实际情况修改）
SERVER_URL = "http://127.0.0.1:8080"


def create_test_user(username, password="123456"):
    """创建测试用户"""
    resp = requests.post(f"{SERVER_URL}/api/register", json={
        "username": username,
        "password": password
    })
    return resp.json()


def login_request(username, password="123456"):
    """发送登录请求，返回 (耗时_秒, 成功/失败)"""
    start = time.time()
    try:
        resp = requests.post(f"{SERVER_URL}/api/login", json={
            "username": username,
            "password": password
        }, timeout=10)
        elapsed = time.time() - start
        return elapsed, resp.json().get("success", False)
    except Exception as e:
        elapsed = time.time() - start
        return elapsed, False


def get_friends_request(user_id):
    """获取好友列表请求"""
    start = time.time()
    try:
        resp = requests.get(f"{SERVER_URL}/api/friends", params={"user_id": user_id}, timeout=10)
        elapsed = time.time() - start
        return elapsed, resp.json().get("success", False)
    except Exception as e:
        elapsed = time.time() - start
        return elapsed, False


def setup_test_users(count=10):
    """创建压测用的测试用户"""
    print(f"[setup] 正在创建 {count} 个测试用户...")
    for i in range(count):
        username = f"bench_user_{i}"
        result = create_test_user(username)
        if result.get("success"):
            print(f"  OK: {username}")
        else:
            print(f"  跳过: {username} - {result.get('message', '')}")
    print("[setup] 测试用户创建完成\n")


def benchmark_login(concurrency, total_requests):
    """压测登录接口"""
    print(f"\n{'='*60}")
    print(f"压测: POST /api/login")
    print(f"并发数: {concurrency}, 总请求数: {total_requests}")
    print(f"{'='*60}\n")

    latencies = []
    success_count = 0
    fail_count = 0
    lock = threading.Lock()
    start_time = time.time()

    def worker():
        nonlocal success_count, fail_count
        elapsed, success = login_request("bench_user_0", "123456")
        with lock:
            latencies.append(elapsed)
            if success:
                success_count += 1
            else:
                fail_count += 1

    with ThreadPoolExecutor(max_workers=concurrency) as executor:
        futures = [executor.submit(worker) for _ in range(total_requests)]
        for f in as_completed(futures):
            f.result()

    total_time = time.time() - start_time

    # 统计结果
    latencies.sort()
    print(f"总耗时:      {total_time:.2f} 秒")
    print(f"QPS:         {total_requests / total_time:.2f} 请求/秒")
    print(f"成功:        {success_count}")
    print(f"失败:        {fail_count}")
    print(f"平均延迟:    {sum(latencies)/len(latencies)*1000:.2f} ms")
    print(f"中位数延迟:  {latencies[len(latencies)//2]*1000:.2f} ms")
    print(f"P95 延迟:    {latencies[int(len(latencies)*0.95)]*1000:.2f} ms")
    print(f"P99 延迟:    {latencies[int(len(latencies)*0.99)]*1000:.2f} ms")
    print(f"最小延迟:    {latencies[0]*1000:.2f} ms")
    print(f"最大延迟:    {latencies[-1]*1000:.2f} ms")


def benchmark_friends(concurrency, total_requests, user_id=1):
    """压测好友列表接口"""
    print(f"\n{'='*60}")
    print(f"压测: GET /api/friends")
    print(f"并发数: {concurrency}, 总请求数: {total_requests}, 用户ID: {user_id}")
    print(f"{'='*60}\n")

    latencies = []
    success_count = 0
    fail_count = 0
    lock = threading.Lock()
    start_time = time.time()

    def worker():
        nonlocal success_count, fail_count
        elapsed, success = get_friends_request(user_id)
        with lock:
            latencies.append(elapsed)
            if success:
                success_count += 1
            else:
                fail_count += 1

    with ThreadPoolExecutor(max_workers=concurrency) as executor:
        futures = [executor.submit(worker) for _ in range(total_requests)]
        for f in as_completed(futures):
            f.result()

    total_time = time.time() - start_time

    latencies.sort()
    print(f"总耗时:      {total_time:.2f} 秒")
    print(f"QPS:         {total_requests / total_time:.2f} 请求/秒")
    print(f"成功:        {success_count}")
    print(f"失败:        {fail_count}")
    print(f"平均延迟:    {sum(latencies)/len(latencies)*1000:.2f} ms")
    print(f"中位数延迟:  {latencies[len(latencies)//2]*1000:.2f} ms")
    print(f"P95 延迟:    {latencies[int(len(latencies)*0.95)]*1000:.2f} ms")
    print(f"最小/最大:   {latencies[0]*1000:.2f} / {latencies[-1]*1000:.2f} ms")


def main():
    parser = argparse.ArgumentParser(description="WeChat 服务器压测工具")
    parser.add_argument("--url", default=SERVER_URL, help=f"服务器地址（默认: {SERVER_URL}）")
    parser.add_argument("--setup", action="store_true", help="创建测试用户")
    parser.add_argument("--concurrency", type=int, default=10, help="并发数（默认: 10）")
    parser.add_argument("--requests", type=int, default=100, help="总请求数（默认: 100）")
    parser.add_argument("--user-id", type=int, default=1, help="好友列表压测使用的用户ID（默认: 1）")
    parser.add_argument("--api", choices=["login", "friends", "all"], default="all", help="压测接口（默认: all）")

    args = parser.parse_args()

    global SERVER_URL
    SERVER_URL = args.url

    # 创建测试用户
    if args.setup:
        setup_test_users(10)
        return

    # 检查服务器连接
    print(f"检查服务器连接: {SERVER_URL}...")
    try:
        resp = requests.get(f"{SERVER_URL}/api/friends?user_id=1", timeout=5)
        print(f"服务器连接正常\n")
    except Exception as e:
        print(f"无法连接到服务器: {e}")
        print("请先启动服务器: python server.py")
        sys.exit(1)

    if args.api in ("login", "all"):
        benchmark_login(args.concurrency, args.requests)

    if args.api in ("friends", "all"):
        benchmark_friends(args.concurrency, args.requests, args.user_id)


if __name__ == "__main__":
    main()