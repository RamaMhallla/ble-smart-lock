// to support StreamSubscription (subscribe with the result of scans)
import 'dart:async';
//Convert text to bytes. we are using utf8.encode and utf8.decode.
import 'dart:convert';

import 'package:flutter/material.dart';
//BLE core library:scan/connect/discover services/read-write characteristics.
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:flutter/services.dart';

class BLEDoorScreen extends StatefulWidget {
  const BLEDoorScreen({super.key});

  @override
  State<BLEDoorScreen> createState() => _BLEDoorScreenState();
}

class _BLEDoorScreenState extends State<BLEDoorScreen>
    with SingleTickerProviderStateMixin {
    //SingleTickerProviderStateMixin is required to make AnimationController (pulse).
  //  Colors
  static const Color primaryBlue = Color(0xFF2563EB);
  static const Color appBarBlue = Color(0xFF9CB6D8); 
  static const Color surfaceWhite = Color(0xFFFFFFFF);
  static const Color scaffoldBg = Color(0xFFF8FAFC);
  static const Color successGreen = Color(0xFF059669);
  static const Color warningOrange = Color(0xFFD97706);
  static const Color errorRed = Color(0xFFDC2626);
  static const Color neutralGray = Color(0xFF475569);
  static const Color lightGray = Color(0xFFE2E8F0);
  static const Color darkGray = Color(0xFF1F2937);

  BluetoothDevice? device; // the device I want to connect with
  BluetoothCharacteristic? rxChar;// to write the PIN
  BluetoothCharacteristic? txChar;//TO receive(notify) from ESP32

  final TextEditingController pinController = TextEditingController();

  //Subscribe to scan results; so you can stop and cancel it with dispose.
  StreamSubscription<List<ScanResult>>? _scanSub;

  String status = "Ready to connect";
  bool _isConnecting = false;
  bool doorOpened = false;
  bool _isPinObscured = true; // the PIN hidden or visible
  bool _isPinValid = false;//Is the PIN length 4 digits? (Only to activate the UNLOCK button if correct)

//Controller to create a pulsing effect (slight zoom in/out during the call).
  late final AnimationController _pulseController;

  @override
  void initState() {
    super.initState();//it is executed the first time the page is built.

    _pulseController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1200),
    );

    //Everything the user types/erases, we calculate if the length is 4.
    pinController.addListener(() {
      final valid = pinController.text.trim().length == 4;
      //If the PIN validity status changes, we use setState to update the UNLOCK button.
      if (valid != _isPinValid) {
        setState(() => _isPinValid = valid);
      }
    });
  }

  // ---------------- Permissions ----------------
  Future<void> _ensurePermissions() async {
    await Permission.bluetoothScan.request();
    await Permission.bluetoothConnect.request();
    await Permission.location.request();

    //Wait until the Bluetooth status turns ON.
    //first means when condition is met for the first time.
    await FlutterBluePlus.adapterState
        .where((s) => s == BluetoothAdapterState.on)
        .first;
  }

  // ---------------- Scan & Connect ----------------
  //Interface update: We started preparing Bluetooth 
  // We are in connection mode + We return the door closed.
  Future<void> scanAndConnect() async {
    setState(() {
      status = "Preparing Bluetooth...";
      _isConnecting = true;
      doorOpened = false;
    });

     //The effect of the pulse begins
    _pulseController.repeat(reverse: true);

    await _ensurePermissions();

    //Guarantee that no previous scan is working + cancellation of any previous subscription.
    await FlutterBluePlus.stopScan();
    await _scanSub?.cancel();

    setState(() => status = "Scanning for GarageDoorESP...");


     //Every time a set of results appears, it searches through them.
    //If the ad name (advName) = GarageDoorESP → it was found.
    _scanSub = FlutterBluePlus.onScanResults.listen((results) async {
      for (final r in results) {
        if (r.advertisementData.advName == "GarageDoorESP") {
          //Stops scan
          // Updates status
          // Connects to device
          // Breaks the loop
          await FlutterBluePlus.stopScan();
          setState(() => status = "Device found. Connecting...");
          await connectToDevice(r.device);
          break;
        }
      }
    });
    //automatic cancellation of the subscription when the scan is finished (cleaning).
    FlutterBluePlus.cancelWhenScanComplete(_scanSub!);

    await FlutterBluePlus.startScan(
      timeout: const Duration(seconds: 10),
      withNames: const ["GarageDoorESP"],
      androidScanMode: AndroidScanMode.lowLatency,
    );
  }

  // ---------------- Connect ----------------
  Future<void> connectToDevice(BluetoothDevice d) async {
    try {
      device = d;
      await device!.connect(autoConnect: false, license: License.free);
      setState(() => status = "Connected. Discovering services...");
      await discoverServices();
    } catch (_) {
      setState(() {
        status = "Connection failed";
        _isConnecting = false;
      });
      _pulseController.stop();
    }
  }

  // ---------------- Discover services ----------------
  Future<void> discoverServices() async {
    final services = await device!.discoverServices();

    for (final s in services) {
      for (final c in s.characteristics) {
        //If the characteristic supports write, store it as rxChar (to send a PIN).
        if (c.properties.write) rxChar = c;
        //If it supports notify, store it as txChar and enable notifications.
        if (c.properties.notify) {
          txChar = c;
          await txChar!.setNotifyValue(true);
          txChar!.lastValueStream.listen((value) {
            //We listen for any incoming bytes.
            //sWe decode the UTF-8 text, remove the spaces, and make it uppercase.
            final msg = utf8.decode(value).trim().toUpperCase();

            //We display the last message from the ESP32 in the status.
            //If the message is "OK" → we consider the door to be open.
            setState(() {
              status = "ESP32: $msg";
              doorOpened = msg == "OK";
            });
          });
        }
      }
    }
    //If write + notify is found → Ready.
    //If not -> There is a problem (no characteristics found).
    //       -> Connecting stops and the pulse stops.
    setState(() {
      status = (rxChar != null && txChar != null)
          ? "Ready. Enter PIN."
          : "Characteristics not found.";
      _isConnecting = false;
    });

    _pulseController.stop();
  }

  // ---------------- Send PIN ----------------
  Future<void> sendPIN() async {
    //Protection:if there is no characteristic for writing or the PIN is not 4, We will not send
    if (rxChar == null) return;
    if (pinController.text.trim().length != 4) return; 
    //Sends the PIN as bytes to the ESP32.
    await rxChar!.write(utf8.encode(pinController.text.trim()));
    setState(() => status = "PIN sent...");
    pinController.clear();
  }
  //It considers itself connected if we find rxChar
  bool get _isConnected => rxChar != null;

  Color _statusColor() {
    if (doorOpened) return successGreen;
    if (_isConnecting) return warningOrange;
    if (_isConnected) return primaryBlue;
    return neutralGray;
  }

  // ---------------- UI ----------------
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: scaffoldBg,
      appBar: _buildAppBar(context),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(20),
        child: Column(
          children: [
            //AnimatedBuilder rebuilds the section as the animation value changes.
            // If connecting ->s Enlarges the circle up to 5%.
            // Draws a circle + icon:
                // If doorOpened true → Door icon
                // Otherwise, lock
            AnimatedBuilder(
              animation: _pulseController,
              builder: (_, __) {
                final scale =
                    _isConnecting ? 1 + _pulseController.value * 0.05 : 1.0;
                return Transform.scale(
                  scale: scale,
                  child: Container(
                    width: 160,
                    height: 160,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      color: _statusColor().withOpacity(0.1),
                      border: Border.all(color: _statusColor(), width: 2),
                    ),
                    child: Icon(
                      doorOpened
                          ? Icons.door_front_door_rounded
                          : Icons.lock_rounded,
                      size: 60,
                      color: _statusColor(),
                    ),
                  ),
                );
              },
            ),
            const SizedBox(height: 20),
            Text(
              doorOpened ? "Door Unlocked" : "Door Locked",
              style: const TextStyle(
                fontSize: 20,
                fontWeight: FontWeight.w700,
                color: darkGray,
              ),
            ),
            const SizedBox(height: 8),
            Text(status, textAlign: TextAlign.center),
            const SizedBox(height: 30),
            TextField(
              controller: pinController,
              maxLength: 4,
              obscureText: _isPinObscured,
              textAlign: TextAlign.center,
              decoration: InputDecoration(
                hintText: "••••",
                counterText: "",
                suffixIcon: IconButton(
                  icon: Icon(_isPinObscured
                      ? Icons.visibility
                      : Icons.visibility_off),
                  onPressed: () =>
                      setState(() => _isPinObscured = !_isPinObscured),
                ),
              ),
            ),
            const SizedBox(height: 20),
            //The button activates only if: 
            //Connected (_isConnected)And the PIN is valid (4 digits)
            ElevatedButton(
              onPressed: _isConnected && _isPinValid ? sendPIN : null,
              child: const Text("UNLOCK DOOR"),
            ),
            const SizedBox(height: 20),
            ElevatedButton.icon(
              onPressed: _isConnecting ? null : scanAndConnect,
              icon: const Icon(Icons.bluetooth),
              label: Text(_isConnecting ? "CONNECTING..." : "CONNECT DEVICE"),
            ),
          ],
        ),
      ),
    );
  }

  PreferredSizeWidget _buildAppBar(BuildContext context) {
    return AppBar(
      elevation: 0,
      backgroundColor: appBarBlue,
      foregroundColor: darkGray,
      systemOverlayStyle: const SystemUiOverlayStyle(
        statusBarColor: appBarBlue,
        statusBarIconBrightness: Brightness.dark,
        statusBarBrightness: Brightness.light,
      ),
      title: const Text(
        "Smart Lock",
        style: TextStyle(fontWeight: FontWeight.w800),
      ),
    );
  }

  @override
  void dispose() {
    //Cancel subscription
    // Edit TextEditingController
    // Stop AnimationController
    // Then call super.dispose
    _scanSub?.cancel();
    pinController.dispose();
    _pulseController.dispose();
    super.dispose();
  }
}
