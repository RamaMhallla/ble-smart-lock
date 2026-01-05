//It provides async tools such as StreamSubscription, Future, and Timer.
// we will use it for Subscription for scan results.
import 'dart:async';
//String ⇄ bytes
import 'dart:convert';

//Cryptographic library (hashing/HMAC).
//Used to generate HMAC(sha256, key).
// (INSECURE MODE: we will NOT use crypto / HMAC, but keeping your comment)
// import 'package:crypto/crypto.dart';

import 'package:flutter/material.dart';
//BLE library: scan/connect/discover/notify/write.
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
//To request Bluetooth + location permissions on Android.
import 'package:permission_handler/permission_handler.dart';

class BLEDoorInsecureScreen extends StatefulWidget {
  //StatefulWidget because the state changes: connection, nonce, door state…
  const BLEDoorInsecureScreen({super.key});

  @override
  State<BLEDoorInsecureScreen> createState() => _BLEDoorScreenState();
}

class _BLEDoorScreenState extends State<BLEDoorInsecureScreen>
    with SingleTickerProviderStateMixin {
  //SingleTickerProviderStateMixin is necessary because wehave an AnimationController (pulse) operator.

  // ================= SECURITY =================
  //A shared secret key between the mobile phone and the ESP32.we'll use it to create an HMAC file.
  //NOTE: Storing secrets within the app is easily exposed (Reverse Engineering).
  // It's better to keep secrets within the ESP32 and on the mobile device for a more robust mechanism.
  // I WILL CHANGE IT LATER
  // (INSECURE MODE: not used)
  static const String sharedSecret = "SUPER_SECRET_KEY";

  // Nordic UART Service UUID
  //Use it to identify your device from advertising and to select a service during discovery.
  static const String serviceUuid =
      "6e400001-b5a3-f393-e0a9-e50e24dcca9e";

  // ================= BLE =================
  BluetoothDevice? device;
  BluetoothCharacteristic? rxChar; //for writing from the mobile device to the ESP32
  BluetoothCharacteristic? txChar; //For notifications/reading from the ESP32 mobile

  //Subscribe to the scan results stream.
  //Important: Disable it in dispose().
  StreamSubscription<List<ScanResult>>? _scanSub;

  String? currentNonce; //random number sent by the ESP32 (challenge)
  bool _connectingNow = false; //Prevents you from connecting to more than one device at the same time (lock)

  // ================= UI =================
  final TextEditingController pinController = TextEditingController(); //The PIN text was captured from TextField.
  late final AnimationController _pulseController;

  bool _isConnecting = false; //Disables the connect button and displays the status.
  bool _isPinValid = false;   //Valid if its length is 4.
  bool _isPinObscured = true; //Hides the PIN.
  bool doorOpened = false; //Changes the icon.

  String status = "Ready"; //Displays the user's status message.

  @override
  void initState() {
    super.initState();
    //I created an animation controller that lasted 1.2 seconds.
    //vsync: this because the State is set with SingleTickerProviderStateMixin.
    _pulseController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1200),
    );

    //Every time the text changes:
    // Calculates if the PIN length is 4.
    // If the status changes, setState triggers the UI (activating the UNLOCK button).
    pinController.addListener(() {
      final valid = pinController.text.trim().length == 4;
      if (valid != _isPinValid) {
        setState(() => _isPinValid = valid);
      }
    });
  }

  // ================= PERMISSIONS =================
  Future<void> _ensurePermissions() async {
    await Permission.bluetoothScan.request();
    await Permission.bluetoothConnect.request();
    await Permission.location.request();

    //Waits until Bluetooth turns ON.
    //.where(...).first means: the first time the condition is met.
    await FlutterBluePlus.adapterState
        .where((s) => s == BluetoothAdapterState.on)
        .first;
  }

  // ================= SCAN & CONNECT =================
  //Preparing the state before deletion:
  // Resets everything
  // Considers itself "connecting"
  // Deletes the old nonce
  Future<void> scanAndConnect() async {
    setState(() {
      status = "Scanning for ESP32...";
      _isConnecting = true;
      doorOpened = false;
      currentNonce = null;
      device = null;
      rxChar = null;
      txChar = null;
      _connectingNow = false;
    });

    _pulseController.repeat(reverse: true);//plays a pulse animation

    await _ensurePermissions();

    await FlutterBluePlus.stopScan();//Stops any previous scans.
    await _scanSub?.cancel();// Cancels any existing subscriptions.

    //It starts listening to the scan results.
    _scanSub = FlutterBluePlus.onScanResults.listen((results) async {
      if (_connectingNow) return; //If a connection is initiated, nothing happens (protection).

      //It scans each displayed device.
      // It reads the service UUIDs in the advertisement.
      // It prints the advertisement name, UUIDs, and MAC/ID.
      for (final r in results) {
        final uuids = r.advertisementData.serviceUuids;

        debugPrint(
          "FOUND => adv='${r.advertisementData.advName}' "
          "uuids=$uuids id=${r.device.remoteId}",
        );

        //If any UUID from the advertisement equals the UUID of the UART service
        if (uuids.any((u) => u.toString().toLowerCase() == serviceUuid)) {
          //Once connected:
          // Prevents duplicate  the connection
          // Stops scanning
          // Waits for a second (Fixes timing issues in Android before connecting)
          _connectingNow = true;

          debugPrint("ESP32 FOUND → stopping scan");

          await FlutterBluePlus.stopScan();

          // Android BLE timing fix
          await Future.delayed(const Duration(seconds: 1));

          //Update status
          // Call connectToDevice
          // Return to stop rotation immediately after the first compatible device.
          setState(() => status = "Connecting to ESP32...");
          await connectToDevice(r.device);
          return;
        }
      }
    });

    //If scan timeout ends, the subscription will be automatically cancelled.
    FlutterBluePlus.cancelWhenScanComplete(_scanSub!);

    //The scan lasts for 15 seconds.
    //LowLatency: Faster but consumes more battery.
    await FlutterBluePlus.startScan(
      timeout: const Duration(seconds: 15),
      androidScanMode: AndroidScanMode.lowLatency,
    );
  }

  // ================= CONNECT =================
  Future<void> connectToDevice(BluetoothDevice d) async {
    try {
      //The selected device is saved.
      device = d;

      // It hears and prints connection status changes (connected/disconnected…).
      device!.connectionState.listen((s) {
        debugPrint("BLE STATE => $s");
      });

      debugPrint(" Connecting to ${d.remoteId}");

      // ✅ FIX: remove invalid parameters (license)
      await device!.connect(
        autoConnect: false,//Direct connection
        timeout: const Duration(seconds: 15),
        license: License.free,
      );

      //After success:
      // Changes status
      // Discovers services and features.
      debugPrint(" BLE CONNECTED");

      setState(() => status = "Discovering services...");
      await discoverServices();
    } catch (e) {
      //If it fails:
      // Prints the error
      // Returns to normal
      // Stops animation
      debugPrint(" CONNECT ERROR: $e");

      setState(() {
        status = "Connection failed";
        _isConnecting = false;
        _connectingNow = false;
      });
      _pulseController.stop();
    }
  }

  // ================= DISCOVER SERVICES =================
  //Find RX/TX and enable notify
  Future<void> discoverServices() async {
    if (device == null) return; //Protection: If there is no device, exit.

    final services = await device!.discoverServices(); //request from BLE stack list of services.

    //It searches for the required UART service.
    // Then it searches for its characteristics.
    for (final s in services) {
      if (s.uuid.toString() == serviceUuid) {
        for (final c in s.characteristics) {
          //If the characteristic UUID is RX (for write) => store it in rxChar.
          if (c.uuid.toString() ==
              "6e400002-b5a3-f393-e0a9-e50e24dcca9e") {
            rxChar = c;
          }
          //If the characteristic UUID is TX (for notify) => store it in txChar.
          if (c.uuid.toString() ==
              "6e400003-b5a3-f393-e0a9-e50e24dcca9e") {
            txChar = c;
          }
        }
      }
    }
    //If RX or TX is found:
    // Indicates an error for the user
    // Disables connection/pulse
    // Exit
    if (rxChar == null || txChar == null) {
      setState(() {
        status = "UART characteristics not found";
        _isConnecting = false;
        _connectingNow = false;
      });
      _pulseController.stop();
      return;
    }

    //Enables notifications on the TX.
    // This allows it to receive messages from the ESP32 in real time.
    await txChar!.setNotifyValue(true);

    //  trigger nonce
    //Send "HELLO" to the ESP32.
    //The idea is: when the ESP32 receives HELLO, it responds with a nonce.
    // (INSECURE MODE: we don't need nonce, so we do NOT send HELLO)
    // await rxChar!.write(utf8.encode("HELLO"));

    //It retrieves the last received value from TX.
    // It converts bytes to a string.
    // It prints it.
    txChar!.lastValueStream.listen((value) {
      final msg = utf8.decode(value).trim();
      debugPrint("BLE IN => $msg");

      // Nonce
      //If you don't have a nonce yet (currentNonce == null)
      // and the message isn't empty and isn't one of the known responses,
      // then treat it as a nonce.
      // Save it and tell the user to enter their PIN.
      // (INSECURE MODE: not used, but keeping your comment/logic)
      if (currentNonce == null &&
          msg.isNotEmpty &&
          msg != "OK" &&
          msg != "WRONG" &&
          msg != "PENDING") {
        currentNonce = msg;
        setState(() => status = "Nonce received. Enter PIN.");
        return;
      }

      //Updates the status on the screen.
      // If OK, the door icon opens.
      // If WRONG, it closes.
      setState(() {
        status = "ESP32: $msg";
        if (msg == "OK") doorOpened = true;
        if (msg == "WRONG") doorOpened = false;
      });
    });

    //After enabling notify and sending a HELLO:
    // It is considered “Connected”
    // we stop the connection
    // The pulse stops
    setState(() {
      // (INSECURE MODE: no nonce)
      status = "Connected. Enter PIN (INSECURE)...";
      _isConnecting = false;
    });

    _pulseController.stop();
  }

  // ================= HMAC =================
  //`key`: Converts the secret to bytes.
  // `bytes`: Converts the message to bytes (the message = PIN + nonce).
  // `Generates an HMAC using SHA-256.
  // `Returns the result as a hex string.`
  // Security Concept: Instead of sending the PIN itself, you send the HMAC.
  // The ESP32 (which has the same sharedSecret and the nonce that was sent) calculates the same HMAC.
  // If they match → it means the user knows the correct PIN.
  // Important Security Note: If an attacker records an HMAC once, they cannot reuse it because the nonce changes each time (this is called a challenge-response).
  // (INSECURE MODE: computeHmac is not used)
  // String computeHmac(String pin, String nonce) { ... }

  // ================= SEND PIN =================
  //Send HMAC instead of PIN
  Future<void> sendPIN() async {
    if (rxChar == null) return; //INSECURE: no nonce required

    //It reads the PIN and confirms the 4 digits.
    final pin = pinController.text.trim();
    if (pin.length != 4) return;

    // (INSECURE MODE)
    // Instead of HMAC, we send PIN directly in plaintext to ESP32
    await rxChar!.write(utf8.encode(pin));

    //updates the user interface.
    setState(() {
      status = "PIN sent (INSECURE). Waiting decision...";
      doorOpened = false;
    });

    //The nonce become null (until a new session).
    // The PIN field is cleared.
    currentNonce = null;
    pinController.clear();
  }

  //Get returns true if:
  // Connected
  // You have RX/TX
  // You have received nonce
  // (INSECURE MODE: we don't require nonce)
  bool get _readyToUnlock =>
      device != null && rxChar != null && txChar != null;

  // ================= UI =================
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text("Smart Lock (INSECURE)")),
      body: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          children: [
            Icon(
              doorOpened ? Icons.lock_open : Icons.lock,
              size: 90,
              color: doorOpened ? Colors.green : Colors.blueGrey,
            ),
            const SizedBox(height: 20),
            Text(status, textAlign: TextAlign.center),
            const SizedBox(height: 30),
            TextField(
              controller: pinController,
              maxLength: 4,
              obscureText: _isPinObscured,
              keyboardType: TextInputType.number,
              decoration: InputDecoration(
                hintText: "Enter PIN",
                suffixIcon: IconButton(
                  icon: Icon(
                      _isPinObscured ? Icons.visibility : Icons.visibility_off),
                  onPressed: () =>
                      setState(() => _isPinObscured = !_isPinObscured),
                ),
              ),
            ),
            const SizedBox(height: 20),
            //If the conditions are met → it works and sends HMAC.
            // If not → null means disabled.
            ElevatedButton(
              onPressed: _readyToUnlock && _isPinValid ? sendPIN : null,
              child: const Text("UNLOCK"),
            ),
            const SizedBox(height: 20),
            //If it's not connecting → disabled
            // If it's not → scanAndConnect is working
            ElevatedButton.icon(
              onPressed: _isConnecting ? null : scanAndConnect,
              icon: const Icon(Icons.bluetooth),
              label: Text(_isConnecting ? "CONNECTING..." : "CONNECT"),
            ),
          ],
        ),
      ),
    );
  }

  @override
  void dispose() {
    //Cancels the scan subscription.
    // Releases the TextField controller.
    // Stops/releases the AnimationController.
    // Then calls super.dispose().
    _scanSub?.cancel();
    pinController.dispose();
    _pulseController.dispose();
    super.dispose();
  }
}


// TO do :
// 1- UUIDs: Keep the comparison lowercase for both sides.
// 2- Separate nonce messages from other messages: It's best for the ESP32 to send nonces in a clear format like NONCE:<value> instead of relying on "not OK/WRONG".
// 3- Shared secrets within the application are a risk:
// Storage the secret only on the ESP32 + use pairing/bonding + BLE secure connections.
// Or use a public-key challenge instead of a static secret.
