"""運行情報ブザー: PC側(Python)の骨組み

役割: 運行状態を取得 → 前回から変わった時だけ MQTT で送る
最初は「模擬データ」(status.txt を書き換えて状態を切り替える)で動かし、
あとで本物の取得処理に差し替える。

事前準備: pip install paho-mqtt
"""
import time
import paho.mqtt.publish as publish
from config import load_topic

TOPIC = load_topic()  # 本物の名前は config.json か環境変数から読む(公開しない)
BROKER = "broker.hivemq.com"
INTERVAL_SEC = 10  # 模擬データ用。本物の取得に差し替えたら、5分(300)以上にする
STATUS_FILE = "status.txt"
VALID_STATES = {"NORMAL", "DELAY", "SUSPENDED"}


def get_status_mock():
    """status.txt の中身(NORMAL / DELAY / SUSPENDED)を状態として返す。
    ファイルがない、または中身が不正な時は None を返す(=前回の状態を保つ)。"""
    try:
        with open(STATUS_FILE, encoding="utf-8") as f:
            state = f.read().strip().upper()
    except FileNotFoundError:
        return None
    return state if state in VALID_STATES else None


# 本物の取得処理ができたら、ここを差し替える
get_status = get_status_mock


def main():
    last_state = None
    print(f"監視開始: {INTERVAL_SEC}秒おきに確認します (Ctrl+Cで終了)")
    while True:
        try:
            state = get_status()
            if state is not None and state != last_state:
                publish.single(TOPIC, state, hostname=BROKER)
                print(f"状態が変わったので送信: {last_state} -> {state}")
                last_state = state
        except Exception as e:
            # 取得や送信に失敗しても止まらず、次の周期でやり直す
            print("エラー:", e)
        time.sleep(INTERVAL_SEC)


if __name__ == "__main__":
    main()
