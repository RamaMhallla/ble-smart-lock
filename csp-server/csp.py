# content security policy
import json
import random
import dateutil.parser
from datetime import datetime, timezone, timedelta

from flask import Flask, request, jsonify

# MQTT
import paho.mqtt.client as mqtt

# Firebase Admin
import firebase_admin
from firebase_admin import credentials, messaging

# InfluxDB
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS

# APP 
app = Flask(__name__)

# CONFIG :

# MQTT
MQTT_BROKER = "10.146.61.134"
MQTT_PORT = 1883
MQTT_TOPIC_RESULT = "door_access/result"

# Firebase
FIREBASE_SERVICE_ACCOUNT = "./serviceAccountKey.json"

# OTP
OTP_TTL_SECONDS = 60
OTP_MAX_ATTEMPTS = 3

# Config from environment variables
INFLUX_URL = "http://10.146.61.134:8086"
INFLUX_TOKEN = "PUT_YOUR_REAL_TOKEN_HERE"
INFLUX_ORG = "my-org"
INFLUX_BUCKET = "security_logs"

influx_client = InfluxDBClient(
    url=INFLUX_URL,
    token=INFLUX_TOKEN,
    org=INFLUX_ORG
)
write_api = influx_client.write_api(write_options=SYNCHRONOUS)

print("[InfluxDB] Enabled (hardcoded config)")

# MQTT 
mqtt_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)

def on_connect(client, userdata, flags, reason_code, properties):
    print(f"[MQTT] Connected  broker={MQTT_BROKER}:{MQTT_PORT}")

mqtt_client.on_connect = on_connect
mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
mqtt_client.loop_start()

def publish_decision(device_id, decision):
    payload = {
        "device_id": device_id,
        "decision": decision
    }
    mqtt_client.publish(MQTT_TOPIC_RESULT, json.dumps(payload))
    print(f"[MQTT] Published → {payload}")

#FIREBASE
if not firebase_admin._apps:
    cred = credentials.Certificate(FIREBASE_SERVICE_ACCOUNT)
    firebase_admin.initialize_app(cred)
    print("[FCM] Firebase Admin initialized ✅")

# MEMORY STORES
USER_FCM_TOKENS = {}     # user_id -> fcm_token
OTP_SESSIONS = {}       # device_id -> session

# ================= UTILS =================
def safe_event_time(data):
    try:
        if data and "timestamp" in data:
            return dateutil.parser.isoparse(data["timestamp"])
    except Exception:
        pass
    return datetime.now(timezone.utc)

def log_malformed_request(data=None, reason="Invalid Request"):
    ip = request.remote_addr or "unknown"
    print(f"[SECURITY] MALFORMED_REQUEST from {ip}")

    point = (
        Point("security_alerts")
        .tag("alert_type", "MALFORMED_REQUEST")
        .tag("source_ip", ip)
        .field("reason", reason)
        .field("error_code", 400)
        .time(datetime.now(timezone.utc))
    )

    if data:
        for k, v in data.items():
            if isinstance(v, (str, int, float)):
                point.field(k, v)

    try:
        write_api.write(bucket=INFLUX_BUCKET, record=point)
    except Exception as e:
        print(f"[InfluxDB] Write failed ❌ {e}")


def log_auth_event(device_id, user_id, event, code, reason, when):
    print(f"[LOG] device={device_id} user={user_id} {reason}")

    point = (
        Point("logs")
        .tag("device", device_id)
        .tag("user", user_id)
        .tag("event", event)
        .field("authorized", code)
        .field("reason", reason)
        .time(when)
    )

    try:
        write_api.write(bucket=INFLUX_BUCKET, record=point)
    except Exception as e:
        print(f"[InfluxDB] Write failed  {e}")


def generate_otp():
    return f"{random.randint(100000, 999999)}"

def otp_cleanup(device_id):
    sess = OTP_SESSIONS.get(device_id)
    if sess and datetime.now(timezone.utc) > sess["expires_at"]:
        del OTP_SESSIONS[device_id]

def send_fcm_notification(user_id, title, body):
    token = USER_FCM_TOKENS.get(user_id)
    if not token:
        print("[FCM] No token for user")
        return False

    msg = messaging.Message(
        token=token,
        notification=messaging.Notification(title=title, body=body)
    )
    messaging.send(msg)
    print("[FCM] Notification sent ✅")
    return True


@app.route("/")
def index():
    return "CSP AAA + OTP + MQTT + FCM + InfluxDB running."

# Register FCM Token
@app.route("/register_token", methods=["POST"])
def register_token():
    data = request.get_json(silent=True)
    if not data:
        log_malformed_request()
        return jsonify({"ok": False}), 400

    user_id = data.get("user_id")
    token = data.get("fcm_token")

    if not user_id or not token:
        log_malformed_request(data)
        return jsonify({"ok": False}), 400

    USER_FCM_TOKENS[user_id] = token
    print(f"[FCM] Token stored for user={user_id}")
    return jsonify({"ok": True}), 200

# VALIDATE 
@app.route("/validate", methods=["POST"])
def validate_device():
     # Safely parse JSON
    data = request.get_json(silent=True)
    print("[/validate] HIT =", data)

    if not data:
        log_malformed_request()
        return jsonify({"authorized": False}), 400

    device_id = data.get("device_id")
    user_id = data.get("user_id")
    event = data.get("event")

    if not device_id or not user_id or not event:
        log_malformed_request(data)
        return jsonify({"authorized": False}), 400

    event_time = safe_event_time(data)
    otp_cleanup(device_id)

    if device_id in OTP_SESSIONS:
        otp_code = OTP_SESSIONS[device_id]["otp"]
    else:
        otp_code = generate_otp()
        OTP_SESSIONS[device_id] = {
            "otp": otp_code,
            "user_id": user_id,
            "attempts": OTP_MAX_ATTEMPTS,
            "expires_at": datetime.now(timezone.utc) + timedelta(seconds=OTP_TTL_SECONDS)
        }

    send_fcm_notification(user_id, "OTP Verification", f"Your OTP is {otp_code}")

    log_auth_event(device_id, user_id, event, 202, "OTP_REQUIRED", event_time)

    return jsonify({"authorized": False, "reason": "OTP_REQUIRED"}), 202

# SUBMIT OTP
@app.route("/submit_otp", methods=["POST"])
def submit_otp():
    data = request.get_json(silent=True)
    if not data:
        log_malformed_request()
        return jsonify({"authorized": False}), 400

    user_id = data.get("user_id")
    device_id = data.get("device_id")
    otp_input = data.get("otp")

    if not user_id or not device_id or not otp_input:
        log_malformed_request(data)
        return jsonify({"authorized": False}), 400

    event_time = safe_event_time(data)
    otp_cleanup(device_id)

    sess = OTP_SESSIONS.get(device_id)
    if not sess or sess["user_id"] != user_id:
        publish_decision(device_id, "DENY")
        log_auth_event(device_id, user_id, "submit_otp", 401, "OTP_INVALID", event_time)
        return jsonify({"authorized": False}), 401

    if sess["attempts"] <= 0:
        del OTP_SESSIONS[device_id]
        publish_decision(device_id, "DENY")
        log_auth_event(device_id, user_id, "submit_otp", 401, "OTP_BLOCKED", event_time)
        return jsonify({"authorized": False}), 401

    if otp_input != sess["otp"]:
        sess["attempts"] -= 1
        publish_decision(device_id, "DENY")
        log_auth_event(device_id, user_id, "submit_otp", 401, "OTP_WRONG", event_time)
        return jsonify({"authorized": False}), 401

    del OTP_SESSIONS[device_id]
    publish_decision(device_id, "ALLOW")
    send_fcm_notification(user_id, "Access Granted", f"Device {device_id} unlocked")

    log_auth_event(device_id, user_id, "submit_otp", 200, "OTP_SUCCESS", event_time)

    return jsonify({"authorized": True}), 200

# RUN
if __name__ == "__main__":
    app.run(
        host="0.0.0.0",
        port=5001,
        debug=True,
        ssl_context=("./certs/server.crt", "./certs/server.key")
    )
