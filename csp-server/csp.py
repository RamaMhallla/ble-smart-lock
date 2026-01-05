# content security policy
import os
import dateutil.parser
from datetime import datetime, timezone

from flask import Flask, request, jsonify
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS

app = Flask(__name__)

# Config from environment variables
INFLUX_URL = os.getenv("INFLUX_URL", "http://influxdb:8086")
INFLUX_TOKEN = os.getenv("INFLUX_TOKEN")
INFLUX_ORG = os.getenv("INFLUX_ORG")
INFLUX_BUCKET = os.getenv("INFLUX_BUCKET")

client = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
write_api = client.write_api(write_options=SYNCHRONOUS)

@app.route('/')
def index():
    return "CSP AAA Service is running."

@app.route('/validate', methods=['POST'])
def validate_device():
    # Safely parse JSON
    data = request.get_json(silent=True)
    if not data:
        return jsonify({"error": "Invalid JSON"}), 400

    device_id = data.get('device_id', 'unknown_device')
    user_id   = data.get('user_id', 'unknown_user')
    event     = data.get('event', 'door_access/request')
    otp_code  = data.get('otp', '')

    timestamp_str = data.get("timestamp")

    try:
        if timestamp_str:
            event_time = dateutil.parser.isoparse(timestamp_str)
        else:
            event_time = datetime.now(timezone.utc)
    except Exception:
        event_time = datetime.now(timezone.utc)

    # Simple AAA Logic
    is_valid = (otp_code == "123456")
    status = "SUCCESS" if is_valid else "FAILED"
    print(f"Authentication {status} for OTP: {otp_code}")

    # Accounting: Log to InfluxDB (ONLY if bucket exists)
    if INFLUX_BUCKET:
        point = (
            Point("logs")
            .tag("device", device_id)
            .tag("user", user_id)
            .tag("event", event)
            .field("authorized", 200 if is_valid else 401)
            .time(event_time)
        )
        write_api.write(bucket=INFLUX_BUCKET, record=point)
    else:
        print(" INFLUX_BUCKET not set — skipping InfluxDB write")

    return jsonify({"authorized": is_valid}), (200 if is_valid else 401)

if __name__ == '__main__':
    app.run(
        debug=True,
        host="0.0.0.0",
        port=5001,
        ssl_context=('certs/server.crt', 'certs/server.key')
    )
