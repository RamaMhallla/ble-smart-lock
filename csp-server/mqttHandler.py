import paho.mqtt.client as mqtt
import json
import ssl
import time

class MQTThandler:
    def __init__(self, addr_broker, port, topic, keepalive, ca_cert=None, client_cert=None, client_key=None):
        self.addr_broker = addr_broker
        self.port = port
        self.topic = topic
        self.keepalive = keepalive
        
        # 1. Initialize client with modern API version
        self.client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
        
        # 2. Configure TLS for Mutual Authentication (mTLS)
        if ca_cert and client_cert and client_key:
            # This line links the certificates to the connection attempt
            self.client.tls_set(
                ca_certs=ca_cert,        # ca.crt to verify broker
                certfile=client_cert,    # client.crt to prove script's identity
                keyfile=client_key,      # client.key to sign the handshake
                cert_reqs=ssl.CERT_REQUIRED,
                tls_version=ssl.PROTOCOL_TLSv1_2
            )
            # Ensure we check that the broker's CN matches "mosquitto"
            self.client.tls_insecure_set(False) 

        # Define Callbacks
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message

        # 3. Establish connection (Must happen AFTER tls_set)
        self.client.connect(self.addr_broker, self.port, self.keepalive)

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            print(f"Securely connected to {self.addr_broker} as '{client._client_id.decode()}'")
            self.client.subscribe(self.topic)
        else:
            print(f"Connection failed with code {reason_code}")

    def _on_message(self, client, userdata, message):
        try:
            payload = message.payload.decode("utf-8")
            print(f"Topic: {message.topic} | Message: {payload}")
        except Exception as e:
            print(f"Error decoding message: {e}")

    def sendMessage(self, payload: dict):
        payloadInJSON = json.dumps(payload)
        self.client.publish(self.topic, payloadInJSON)
        print("Message sent: ", payloadInJSON)

    def start(self):
        self.client.loop_start()

    def stop(self):
        self.client.loop_stop()
        self.client.disconnect()

# --- Example Usage ---
if __name__ == "__main__":
    # Wait for Mosquitto to fully initialize in Docker
    time.sleep(10)

    # Note: Ensure these paths match your container's internal folder structure
    handler = MQTThandler(
        addr_broker="mosquitto", 
        port=8883, 
        topic="door_access/result", 
        keepalive=60, 
        ca_cert="./certs/ca.crt",     
        client_cert="./certs/client.crt", 
        client_key="./certs/client.key"  
    )
    
    handler.start()
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        handler.stop()