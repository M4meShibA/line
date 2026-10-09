"""設定の読み込み。本物のトピック名は公開しないファイル/環境変数に置く。

優先順: 環境変数 TRAIN_BUZZER_TOPIC → pc/config.json → 例の名前
"""
import json
import os

EXAMPLE_TOPIC = "your-unique-topic/status"


def load_topic():
    topic = os.environ.get("TRAIN_BUZZER_TOPIC")
    if topic:
        return topic
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)["topic"]
    except FileNotFoundError:
        print("config.json がないので、例のトピック名を使います")
        return EXAMPLE_TOPIC
