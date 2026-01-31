import os
import json
import dateutil.parser
from datetime import datetime, timezone

from flask import Flask, request, jsonify
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS

app = Flask(__name__)

# ================= CONFIGURATION =================
INFLUX_URL    = os.getenv("INFLUX_URL", "http://influxdb:8086")
INFLUX_TOKEN  = os.getenv("INFLUX_TOKEN")
INFLUX_ORG    = os.getenv("INFLUX_ORG")
INFLUX_BUCKET = os.getenv("INFLUX_BUCKET")

# Setup InfluxDB
client = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
write_api = client.write_api(write_options=SYNCHRONOUS)

# ================= HELPERS =================

def log_alert(alert_type, details=None):
    """Helper to log alerts to InfluxDB."""
    print(f"ALERT: {alert_type} - {details}")
    if INFLUX_BUCKET:
        point = (
            Point("security_alerts")
            .tag("alert_type", alert_type)
            .tag("source_ip", request.remote_addr)
            .field("error_code", 400)
            .time(datetime.now(timezone.utc))
        )
        if details:
             point.field("details", str(details))
        try:
            write_api.write(bucket=INFLUX_BUCKET, record=point)
        except Exception as e:
            print(f"Influx Write Failed: {e}")

# ================= ROUTES =================

@app.route('/')
def index():
    return "CSP Unsecured Service is running."

@app.route('/storegas', methods=['POST'])
def storegas():
    # Expecting standard JSON: {"device_id": "...", "gas_level": 123, "event": "..."}
    data = request.get_json(silent=True)

    if not data:
        return jsonify({"error": "Invalid JSON body"}), 400

    try:
        device_id = data.get('device_id', '')

        if not device_id:
            log_alert("MISSING_DEVICE_ID", "device_id missing")
            return jsonify({"error": "Missing device_id"}), 400   
        
        # LOG TO INFLUXDB
        event     = data.get('event', '')
        gas_level = data.get('gas_level', 0)

        if INFLUX_BUCKET:
            try:
                point = (
                    Point("logs")
                    .tag("device", device_id)
                    .field("gas_level", gas_level)
                    .time(datetime.now(timezone.utc))
                )            
                write_api.write(bucket=INFLUX_BUCKET, record=point)
            except Exception as e:
                print(f"Influx Logging Error: {e}")

        return jsonify({"response": "success", "message": "Gas level stored"}), 200

    except Exception as e:
        log_alert("STOREGAS_FAILED", str(e))  
        return jsonify({"error": f"Store Gas Error: {str(e)}"}), 500
    

@app.route('/validate', methods=['POST'])
def validate_device():
    # Expecting standard JSON: {"device_id": "...", "user_id": "...", "otp": "...", "event": "..."}
    data = request.get_json(silent=True)

    if not data:
        return jsonify({"error": "Invalid JSON body"}), 400

    print(f"Received Request: {data}")

    # 1. EXTRACT FIELDS (Flattened, no nested 'data' field needed anymore)
    device_id = data.get('device_id', '')
    user_id   = data.get('user_id', '')
    otp_code  = data.get('otp', '')
    event     = data.get('event', '')

    # 2. Basic Validation
    if not device_id or not user_id:
        log_alert("MISSING_FIELDS", "device_id or user_id missing")
        return jsonify({"authorized": False, "reason": "Missing Fields"}), 400

    # 3. Verify OTP (Simple logic)
    is_valid = (otp_code == "1234")
    
    # 4. LOG TO INFLUXDB (Accounting)
    if INFLUX_BUCKET:
        try:
            point = (
                Point("logs")
                .tag("device", device_id)
                .tag("user", user_id)
                .tag("event", event)
                .field("authorized", 1 if is_valid else 0)
                .time(datetime.now(timezone.utc))
            )            
            write_api.write(bucket=INFLUX_BUCKET, record=point)
        except Exception as e:
            print(f"Influx Logging Error: {e}")

    # 5. SEND PLAIN JSON RESPONSE
    response_payload = {
        "authorized": is_valid,
        "reason": "Valid otp" if is_valid else "Invalid otp",
        "timestamp": datetime.now(timezone.utc).isoformat()
    }
    
    return jsonify({"response": response_payload, "status": 200})

if __name__ == '__main__':
    # SSL Context removed. Host 0.0.0.0 allows external access.
    app.run(
        debug=True,
        host="0.0.0.0", 
        port=5001
    )