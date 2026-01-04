#content security policy 
import datetime
import os
import dateutil
from flask import Flask, request
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
    # Data sent from Node-RED (e.g., username and OTP)
    data = request.json
    device_id = data.get('device_id')
    user_id = data.get('user_id')
    event= data.get('event')
    otp_code = data.get('otp')

    timestamp_str = data.get("timestamp")
    # 2. Convert string to a Python datetime object
    # This handles the 'Z' (UTC) and ISO format correctly
    try:
        event_time = dateutil.parser.isoparse(timestamp_str)
    except Exception:
        # Fallback to current time if the timestamp is missing or malformed
        event_time = datetime.utcnow()
    
    # Simple AAA Logic
    is_valid = (otp_code == "123456") # Replace with real DB check
    status = "SUCCESS" if is_valid else "FAILED"
    print(f"Authentication {status} for OTP: {otp_code}")

    # Accounting: Log to InfluxDB
   
    point = Point("logs") \
            .tag("device", device_id) \
            .tag("user", user_id) \
            .tag("event", event) \
            .field("authorized", 200 if is_valid else 401)\
            .time(event_time)
    
        
        # 'bucket' refers to the 'security_logs' name in your Docker config
    write_api.write(bucket=INFLUX_BUCKET, record=point)


    return {"authorized": is_valid}, 200 if is_valid else 401

if __name__ == '__main__':
    app.run(debug=True,host="0.0.0.0", port=5001,ssl_context=('server.crt', 'server.key'))
