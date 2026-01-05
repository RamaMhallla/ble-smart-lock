from datetime import datetime, timezone
from mqttHandler import MQTThandler
import time
if __name__ == "__main__":
    time.sleep(10)
    addr_broker = "mosquitto"
    port = 1883
    topic = "door_access/request"
    keepalive = 60
    publisherMQTT = MQTThandler(addr_broker, port, topic, keepalive)

    # Generate current time in ISO 8601 format with 'Z' suffix
    # Example: 2026-01-03T18:16:10.123456Z
    for i in range(3):
        time.sleep(10)  # Send a message every 5 seconds
        current_time_iso = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")

        payload = {
            "event": "door_access/request",
            "device_id": "ESP32_GARAGE_01",
            "user_id": "mobile_user_01",
            "method": "BLE",
            "otp": "123456",
            "timestamp": current_time_iso
        }
    
        publisherMQTT.sendMessage(payload)
        print(f"Message sent with timestamp: {current_time_iso}")