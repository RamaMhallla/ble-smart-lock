import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

class BLEDoorInsecureScreen extends StatefulWidget {
  const BLEDoorInsecureScreen({super.key});

  @override
  State<BLEDoorInsecureScreen> createState() => _BLEDoorInsecureScreenState();
}

class _BLEDoorInsecureScreenState extends State<BLEDoorInsecureScreen>
    with SingleTickerProviderStateMixin {
  // ================= BLE CONFIG =================
  static const String serviceUuid =
      "6e400001-b5a3-f393-e0a9-e50e24dcca9e";

  BluetoothDevice? device;
  BluetoothCharacteristic? rxChar;
  BluetoothCharacteristic? txChar;

  StreamSubscription<List<ScanResult>>? _scanSub;
  bool _connectingNow = false;

  // ================= UI STATE =================
  final TextEditingController pinController = TextEditingController();
  late final AnimationController _pulseController;

  bool _isConnecting = false;
  bool _isPinValid = false;
  bool _isPinObscured = true;
  bool doorOpened = false;

  String status = "Ready";

  @override
  void initState() {
    super.initState();

    _pulseController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1200),
    );

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

    await FlutterBluePlus.adapterState
        .where((s) => s == BluetoothAdapterState.on)
        .first;
  }

  // ================= SCAN & CONNECT =================
  Future<void> scanAndConnect() async {
    setState(() {
      status = "Scanning for device...";
      _isConnecting = true;
      doorOpened = false;
      device = null;
      rxChar = null;
      txChar = null;
      _connectingNow = false;
    });

    _pulseController.repeat(reverse: true);

    await _ensurePermissions();

    await FlutterBluePlus.stopScan();
    await _scanSub?.cancel();

    _scanSub = FlutterBluePlus.onScanResults.listen((results) async {
      if (_connectingNow) return;

      for (final r in results) {
        final uuids = r.advertisementData.serviceUuids;


        if (uuids.any((u) => u.toString().toLowerCase() == serviceUuid)
) {
          _connectingNow = true;
          await FlutterBluePlus.stopScan();
          await Future.delayed(const Duration(seconds: 1));

          setState(() => status = "Connecting...");
          await connectToDevice(r.device);
          return;
        }
      }
    });

    FlutterBluePlus.cancelWhenScanComplete(_scanSub!);

    await FlutterBluePlus.startScan(
      timeout: const Duration(seconds: 15),
      androidScanMode: AndroidScanMode.lowLatency,
    );
  }

  // ================= CONNECT =================
  Future<void> connectToDevice(BluetoothDevice d) async {
    try {
      device = d;

      await device!.connect(
        autoConnect: false,
        timeout: const Duration(seconds: 15),
        license: License.free,
      );

      setState(() => status = "Discovering services...");
      await discoverServices();
    } catch (e) {
      setState(() {
        status = "Connection failed";
        _isConnecting = false;
        _connectingNow = false;
      });
      _pulseController.stop();
    }
  }

  // ================= DISCOVER SERVICES =================
  Future<void> discoverServices() async {
    if (device == null) return;

    final services = await device!.discoverServices();

    for (final s in services) {
      if (s.uuid.toString().toLowerCase() == serviceUuid) {
        for (final c in s.characteristics) {
          final uuid = c.uuid.toString().toLowerCase();
          if (uuid ==
              "6e400002-b5a3-f393-e0a9-e50e24dcca9e") rxChar = c;
          if (uuid ==
              "6e400003-b5a3-f393-e0a9-e50e24dcca9e") txChar = c;
        }
      }
    }

    if (rxChar == null || txChar == null) {
      setState(() {
        status = "UART characteristics not found";
        _isConnecting = false;
        _connectingNow = false;
      });
      _pulseController.stop();
      return;
    }

    await txChar!.setNotifyValue(true);

    txChar!.lastValueStream.listen((value) {
      final msg = utf8.decode(value).trim();

      setState(() {
        status = "ESP32: $msg";
        if (msg == "OK") doorOpened = true;
        if (msg == "WRONG") doorOpened = false;
      });
    });

    setState(() {
      status = "Connected. Enter PIN (insecure mode)";
      _isConnecting = false;
    });

    _pulseController.stop();
  }

  // ================= SEND PIN (INSECURE) =================
  Future<void> sendPIN() async {
    if (rxChar == null) return;

    final pin = pinController.text.trim();
    if (pin.length != 4) return;

    await rxChar!.write(utf8.encode(pin));

    setState(() {
      status = "PIN sent. Waiting for response...";
      doorOpened = false;
    });

    pinController.clear();
  }

  bool get _readyToUnlock =>
      device != null && rxChar != null && txChar != null;

  // ================= UI =================
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text("Smart Lock (Insecure Mode)")),
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
                    _isPinObscured
                        ? Icons.visibility
                        : Icons.visibility_off,
                  ),
                  onPressed: () =>
                      setState(() => _isPinObscured = !_isPinObscured),
                ),
              ),
            ),
            const SizedBox(height: 20),
            ElevatedButton(
              onPressed: _readyToUnlock && _isPinValid ? sendPIN : null,
              child: const Text("UNLOCK"),
            ),
            const SizedBox(height: 20),
            ElevatedButton.icon(
              onPressed: _isConnecting ? null : scanAndConnect,
              icon: const Icon(Icons.bluetooth),
              label:
                  Text(_isConnecting ? "CONNECTING..." : "CONNECT"),
            ),
          ],
        ),
      ),
    );
  }

  @override
  void dispose() {
    _scanSub?.cancel();
    pinController.dispose();
    _pulseController.dispose();
    super.dispose();
  }
}
