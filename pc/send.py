"""1回だけ状態を送る: python send.py DELAY"""
import sys
import paho.mqtt.publish as publish
from config import load_topic

BROKER = "broker.hivemq.com"

state = sys.argv[1].upper() if len(sys.argv) > 1 else "DELAY"
publish.single(load_topic(), state, hostname=BROKER)
print("送信:", state)
