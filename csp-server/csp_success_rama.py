# content security policy
import os
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

# ================= APP =================
app = Flask(__name__)

# ================= CONFIG =================
MQTT_BROKER = "10.146.61.134"
MQTT_PORT = 1883
MQTT_TOPIC_RESULT = "door_access/result"

FIREBASE_SERVICE_ACCOUNT = "./serviceAccountKey.json"

OTP_TTL_SECONDS = 60
OTP_MAX_ATTEMPTS = 3

# ================= MQTT =================
mqtt_client = mqtt.Client(
    callback_api_version=mqtt.CallbackAPIVersion.VERSION2
)

def on_connect(client, userdata, flags, reason_code, properties):
    print(f"[MQTT] Connected ✅ broker={MQTT_BROKER}:{MQTT_PORT}")

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

# ================= FIREBASE =================
if not firebase_admin._apps:
    cred = credentials.Certificate(FIREBASE_SERVICE_ACCOUNT)
    firebase_admin.initialize_app(cred)
    print("[FCM] Firebase Admin initialized ✅")

# ================= MEMORY STORES =================
USER_FCM_TOKENS = {}     # user_id -> fcm_token
OTP_SESSIONS = {}        # device_id -> otp session

# ================= ROUTES =================
@app.route('/')
def index():
    return "CSP AAA + OTP Service running."

# -------- Register FCM Token --------
@app.route('/register_token', methods=['POST'])
def register_token():
    data = request.get_json() or {}
    user_id = data.get("user_id")
    token = data.get("fcm_token")

    if not user_id or not token:
        return jsonify({"ok": False}), 400

    USER_FCM_TOKENS[user_id] = token
    print(f"[FCM] Token stored for user={user_id}")
    return jsonify({"ok": True}), 200

# -------- Utils --------
def send_fcm_notification(user_id, title, body):
    token = USER_FCM_TOKENS.get(user_id)
    if not token:
        print("[FCM] No token for user")
        return False

    msg = messaging.Message(
        token=token,
        notification=messaging.Notification(
            title=title,
            body=body
        ),
    )
    messaging.send(msg)
    print("[FCM] Notification sent ✅")
    return True

def generate_otp():
    return f"{random.randint(100000, 999999)}"

def otp_cleanup(device_id):
    sess = OTP_SESSIONS.get(device_id)
    if sess and datetime.now(timezone.utc) > sess["expires_at"]:
        del OTP_SESSIONS[device_id]

# -------- VALIDATE (AAA → OTP REQUIRED) --------
@app.route('/validate', methods=['POST'])
def validate_device():

    data = request.get_json()
    print("[/validate] HIT =", data)

    if not data:
        return jsonify({"authorized": False}), 400

    device_id = data.get("device_id")
    user_id   = data.get("user_id")
    event     = data.get("event")
    hmac_val  = data.get("otp")

    if not all([device_id, user_id, event, hmac_val]):
        return jsonify({"authorized": False}), 400

    otp_cleanup(device_id)

    # create / reuse OTP
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

    send_fcm_notification(
        user_id,
        "🔐 OTP Verification",
        f"Your OTP is {otp_code}"
    )

    # ✅ IMPORTANT RETURN
    return jsonify({
        "authorized": False,
        "reason": "OTP_REQUIRED"
    }), 202

# -------- SUBMIT OTP --------
@app.route('/submit_otp', methods=['POST'])
def submit_otp():

    data = request.get_json() or {}
    user_id = data.get("user_id")
    device_id = data.get("device_id")
    otp_input = data.get("otp")

    if not all([user_id, device_id, otp_input]):
        return jsonify({"authorized": False}), 400

    otp_cleanup(device_id)

    sess = OTP_SESSIONS.get(device_id)
    if not sess:
        publish_decision(device_id, "DENY")
        return jsonify({"authorized": False}), 401

    if sess["user_id"] != user_id:
        publish_decision(device_id, "DENY")
        return jsonify({"authorized": False}), 401

    if sess["attempts"] <= 0:
        del OTP_SESSIONS[device_id]
        publish_decision(device_id, "DENY")
        return jsonify({"authorized": False}), 401

    if otp_input != sess["otp"]:
        sess["attempts"] -= 1
        publish_decision(device_id, "DENY")
        return jsonify({"authorized": False}), 401

    # SUCCESS
    del OTP_SESSIONS[device_id]
    publish_decision(device_id, "ALLOW")

    send_fcm_notification(
        user_id,
        "✅ Access Granted",
        f"Device {device_id} unlocked"
    )

    return jsonify({"authorized": True}), 200

# ================= RUN =================
if __name__ == "__main__":
    app.run(
        host="0.0.0.0",
        port=5001,
        debug=True,
        ssl_context=("./certs/server.crt", "./certs/server.key")
    )
