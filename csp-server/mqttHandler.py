import paho.mqtt.client as mqtt
import json
import ssl
import time

class MQTThandler:
    def __init__(self, addr_broker, port, topic, keepalive, ca_cert=None):
        self.addr_broker = addr_broker
        self.port = port
        self.topic = topic
        self.keepalive = keepalive
        
        # Initialize client with modern API version
        self.client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
        
    

        # Define Callbacks
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message

        # Establish connection
        self.client.connect(self.addr_broker, self.port, self.keepalive)

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        """Callback for when the client receives a CONNACK response from the server."""
        if reason_code == 0:
            print(f"Connected successfully to {self.addr_broker}")
            # Subscribing in on_connect ensures subscription is renewed after reconnect
            self.client.subscribe(self.topic)
        else:
            print(f"Connection failed with code {reason_code}")

    def _on_message(self, client, userdata, message):
        """Standard handler for all incoming messages."""
        try:
            payload = message.payload.decode("utf-8")
            print(f"Topic: {message.topic} | Message: {payload}")
            # You can add logic here to pass this to your CSP/Node-RED service
        except Exception as e:
            print(f"Error decoding message: {e}")

    def sendMessage(self, payload: dict):
        """Publishes a JSON payload."""
        payloadInJSON = json.dumps(payload)
        self.client.publish(self.topic, payloadInJSON)
        print("Message sent: ", payloadInJSON)

    def start(self):
        """Starts the background network loop."""
        self.client.loop_start()

    def stop(self):
        """Stops the loop and disconnects gracefully."""
        self.client.loop_stop()
        self.client.disconnect()

# --- Example Usage ---
if __name__ == "__main__":
    time.sleep(10)
    # Use Port 8883 for MQTTS
    handler = MQTThandler("mosquitto", 1883, "door_access/result", 60, ca_cert="ca.crt")
    
    handler.start()
    
    try:
        while True:
            # Keep the main thread alive to allow the background thread to work
            time.sleep(1)
    except KeyboardInterrupt:
        handler.stop()