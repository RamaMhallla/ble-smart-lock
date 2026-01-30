import os
import json
import base64
import hashlib
import hmac
import dateutil.parser
from datetime import datetime, timezone

from flask import Flask, request, jsonify
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import padding
from cryptography.hazmat.backends import default_backend

app = Flask(__name__)

# ================= CONFIGURATION =================
INFLUX_URL    = os.getenv("INFLUX_URL", "http://influxdb:8086")
INFLUX_TOKEN  = os.getenv("INFLUX_TOKEN")
INFLUX_ORG    = os.getenv("INFLUX_ORG")
INFLUX_BUCKET = os.getenv("INFLUX_BUCKET")

# SECURITY KEYS (MUST MATCH ESP32 & FLUTTER EXACTLY)
AES_KEY_STR = "1234567890123456" # 16 bytes
AES_IV_STR  = "abcdefghijklmnop" # 16 bytes
SHARED_SECRET = "SUPER_SECRET_KEY"

# Convert to bytes for cryptography lib
AES_KEY_BYTES = AES_KEY_STR.encode('utf-8')
AES_IV_BYTES  = AES_IV_STR.encode('utf-8')
SHARED_SECRET_BYTES = SHARED_SECRET.encode('utf-8')

# Setup InfluxDB
client = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
write_api = client.write_api(write_options=SYNCHRONOUS)

# ================= CRYPTO HELPER FUNCTIONS =================

def compute_hmac(data: str) -> str:
    """Calculates HMAC-SHA256 signature for integrity check."""
    # Matches the 'computeHmac' logic in Flutter/ESP32
    h = hmac.new(SHARED_SECRET_BYTES, data.encode('utf-8'), hashlib.sha256)
    return h.hexdigest()

def decrypt_aes(encrypted_base64: str) -> str:
    """Decrypts Base64 AES-128-CBC string to plaintext."""
    try:
        encrypted_bytes = base64.b64decode(encrypted_base64)
        
        cipher = Cipher(algorithms.AES(AES_KEY_BYTES), modes.CBC(AES_IV_BYTES), backend=default_backend())
        decryptor = cipher.decryptor()
        padded_plaintext = decryptor.update(encrypted_bytes) + decryptor.finalize()
        
        # Remove PKCS7 padding (Critical for compatibility)
        unpadder = padding.PKCS7(128).unpadder()
        plaintext = unpadder.update(padded_plaintext) + unpadder.finalize()
        
        return plaintext.decode('utf-8')
    except Exception as e:
        print(f"Decryption Error: {e}")
        return None

def encrypt_aes(plaintext: str) -> str:
    """Encrypts plaintext to Base64 AES-128-CBC string."""
    try:
        # Add PKCS7 padding
        padder = padding.PKCS7(128).padder()
        padded_data = padder.update(plaintext.encode('utf-8')) + padder.finalize()
        
        cipher = Cipher(algorithms.AES(AES_KEY_BYTES), modes.CBC(AES_IV_BYTES), backend=default_backend())
        encryptor = cipher.encryptor()
        encrypted_bytes = encryptor.update(padded_data) + encryptor.finalize()
        
        return base64.b64encode(encrypted_bytes).decode('utf-8')
    except Exception as e:
        print(f"Encryption Error: {e}")
        return ""

def create_secure_packet(json_data: dict) -> str:
    """Wraps a dict response into a signed 'SIGNATURE:CIPHERTEXT' packet."""
    # 1. Convert Dict to JSON String
    plain_text = json.dumps(json_data)
    
    # 2. Encrypt
    ciphertext = encrypt_aes(plain_text)
    
    # 3. Sign (HMAC) - Encrypt-then-MAC
    signature = compute_hmac(ciphertext)
    
    # 4. Combine
    return f"{signature}:{ciphertext}"

# ================= ROUTES =================

@app.route('/')
def index():
    return "CSP Secure AAA Service is running."

def log_alert(alert_type, details=None):
    """Helper to log security alerts to InfluxDB."""
    print(f"SECURITY ALERT: {alert_type} - {details}")
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

def extract_dataOTP(raw_data: str):
        if not raw_data or ":" not in raw_data:
            log_alert("MALFORMED_PACKET", "Missing separator or empty body")
            raise Exception("Malformed Packet")

        separator_index = raw_data.index(":")
        received_signature = raw_data[:separator_index]
        ciphertext = raw_data[separator_index + 1:]
        
        # Calculate expected HMAC based on the received ciphertext
        expected_signature = compute_hmac(ciphertext)
        
        # Secure comparison to prevent timing attacks
        if not hmac.compare_digest(received_signature, expected_signature):
            log_alert("TAMPERING_DETECTED", f"Sig mismatch. Exp: {expected_signature} vs Rec: {received_signature}")
            # Reject immediately. Do NOT decrypt.
            raise Exception("Integrity Check Failed")

        # 3. DECRYPT PAYLOAD
        # Now it is safe to decrypt because we know the sender has the secret key
        decrypted_json_str = decrypt_aes(ciphertext)

        if not decrypted_json_str:
            log_alert("DECRYPTION_FAILED", "Invalid ciphertext or padding error")
            raise Exception("Decryption failed")

    
        # 4. PARSE INTERNAL JSON
        data = json.loads(decrypted_json_str)
        return data

    

@app.route('/validate', methods=['POST'])
def validate_device():
    # 1. READ RAW BODY (Expect format: "SIGNATURE:CIPHERTEXT")
    # We do NOT use request.get_json() because the payload is encrypted string, not JSON.
    raw_data = request.data.decode('utf-8').strip()
    try:
        data = extract_dataOTP(raw_data)
    except Exception as e:
        log_alert("PACKET_EXTRACTION_FAILED", str(e))
        return jsonify({"error": f"Packet Extraction Error: {str(e)}"}), 500    

    print(f"Decrypted Request: {data}")

    # 2. EXTRACT FIELDS
    device_id = data.get('device_id', '')
    dataOTP   = data.get('data', '')
    event     = data.get('event', '')
    sensor_data = data.get('sensor_data', {})

    try:
        dataInfo = extract_dataOTP(dataOTP)  
        user_id = dataInfo.get('user_id', '')
        otp_code  = dataInfo.get('otp', '')
    except Exception as e:
        log_alert("OTP_EXTRACTION_FAILED", str(e))
        return jsonify({"error": f"OTP Extraction Error: {str(e)}"}), 500    
    
    # Basic Validation
    if not device_id or not user_id:
        log_alert("MISSING_FIELDS", "device_id or user_id missing")
        response_payload = {"authorized": False, "reason": "Missing Fields"}
        return create_secure_packet(response_payload), 400

    # Verify OTP (Simple logic)
    is_valid = (otp_code == "1234")
    
    # 5. LOG TO INFLUXDB (Accounting)
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
            for k, v in sensor_data.items():
                if isinstance(v, (int, float)):
                    point.field(k, v)
            
            write_api.write(bucket=INFLUX_BUCKET, record=point)
        except Exception as e:
            print(f"Influx Logging Error: {e}")

    # 6. ENCRYPT & SIGN RESPONSE
    # We must send a secure packet back so the client trusts us
    response_payload = {
        "authorized": is_valid,
        "reason":" Valid otp" if is_valid else "invalid otp",
        "timestamp": datetime.now(timezone.utc).isoformat()
    }
    
    secure_response = create_secure_packet(response_payload)
    
    # Return string response directly (status 200)
    return jsonify({"response": secure_response, "status": 200})

if __name__ == '__main__':
    app.run(
        debug=True,
        host="0.0.0.0", 
        port=5001,
        ssl_context=('./certs/server.crt', './certs/server.key')
    )