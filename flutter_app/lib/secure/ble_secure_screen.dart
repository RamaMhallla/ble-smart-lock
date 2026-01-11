import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:crypto/crypto.dart';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:http/io_client.dart';

class BLEDoorSecureScreen extends StatefulWidget {
  const BLEDoorSecureScreen({super.key});

  @override
  State<BLEDoorSecureScreen> createState() => _BLEDoorSecureScreenState();
}

class _BLEDoorSecureScreenState extends State<BLEDoorSecureScreen>
    with SingleTickerProviderStateMixin {
  // ================= SECURITY =================
  static const String sharedSecret = "SUPER_SECRET_KEY";

  static const String serviceUuid =
      "6e400001-b5a3-f393-e0a9-e50e24dcca9e";

  // ================= BLE =================
  BluetoothDevice? device;
  BluetoothCharacteristic? rxChar;
  BluetoothCharacteristic? txChar;

  StreamSubscription<List<ScanResult>>? _scanSub;

  String? currentNonce;
  bool _connectingNow = false;

  // ================= UI =================
  final TextEditingController pinController = TextEditingController();
  late final AnimationController _pulseController;

  bool _isConnecting = false;
  bool _isPinValid = false;
  bool _isPinObscured = true;
  bool doorOpened = false;
  bool _waitingForOTP = false;

  String status = "Ready";

  // ================= CSP CONFIG =================
  static const String CSP_BASE_URL = "https://10.146.61.134:5001";
  static const String USER_ID = "mobile_user_01";
  static const String DEVICE_ID = "ESP32_GARAGE_01";

  late final IOClient _httpClient;

  @override
  void initState() {
    super.initState();

    final HttpClient httpClient = HttpClient();
    httpClient.badCertificateCallback =
        (X509Certificate cert, String host, int port) {
      return host == "10.146.61.134";
    };
    _httpClient = IOClient(httpClient);

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
      currentNonce = null;
      _waitingForOTP = false;
    });

    _pulseController.repeat(reverse: true);
    await _ensurePermissions();

    await FlutterBluePlus.stopScan();
    await _scanSub?.cancel();

    _scanSub = FlutterBluePlus.onScanResults.listen((results) async {
      if (_connectingNow) return;

      for (final r in results) {
        if (r.device.name == DEVICE_ID) {
          _connectingNow = true;
          await FlutterBluePlus.stopScan();
          setState(() => status = "Connecting...");
          await connectToDevice(r.device);
          return;
        }
      }
    });

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
    final services = await device!.discoverServices();

    for (final s in services) {
      if (s.uuid.toString().toLowerCase() == serviceUuid.toLowerCase()) {
        for (final c in s.characteristics) {
          final uuid = c.uuid.toString().toLowerCase();
          if (uuid == "6e400002-b5a3-f393-e0a9-e50e24dcca9e") rxChar = c;
          if (uuid == "6e400003-b5a3-f393-e0a9-e50e24dcca9e") txChar = c;
        }
      }
    }

    if (rxChar == null || txChar == null) {
      setState(() => status = "UART service not found");
      _pulseController.stop();
      return;
    }

    await txChar!.setNotifyValue(true);
    await rxChar!.write(utf8.encode("HELLO"));

    txChar!.lastValueStream.listen((value) {
      final msg = utf8.decode(value).trim();

      setState(() {
        if (currentNonce == null &&
            msg.isNotEmpty &&
            msg != "OK" &&
            msg != "WRONG" &&
            msg != "PENDING") {
          currentNonce = msg;
          status = "Nonce received. Enter PIN.";
          return;
        }

        if (msg == "PENDING") {
          status = "OTP verification required";
          _waitingForOTP = true;
          _showOtpDialog();
          return;
        }

        if (msg == "OK") {
          status = "Access granted";
          doorOpened = true;
          _waitingForOTP = false;
          return;
        }

        if (msg == "WRONG") {
          status = "Access denied";
          doorOpened = false;
          _waitingForOTP = false;
        }
      });
    });

    setState(() {
      status = "Connected. Waiting for nonce...";
      _isConnecting = false;
    });

    _pulseController.stop();
  }

  // ================= OTP DIALOG =================
  Future<void> _showOtpDialog() async {
    final controller = TextEditingController();

    await showDialog(
      context: context,
      barrierDismissible: false,
      builder: (_) => AlertDialog(
        title: const Text("OTP Verification"),
        content: TextField(
          controller: controller,
          maxLength: 6,
          keyboardType: TextInputType.number,
          decoration: const InputDecoration(labelText: "OTP"),
        ),
        actions: [
          ElevatedButton(
            onPressed: () async {
              final otp = controller.text.trim();
              await _submitOtpToCSP(otp);
              if (mounted) Navigator.pop(context);
            },
            child: const Text("Verify"),
          ),
        ],
      ),
    );
  }

  // ================= SUBMIT OTP =================
  Future<void> _submitOtpToCSP(String otp) async {
    if (otp.isEmpty) {
      setState(() => status = "OTP is required");
      return;
    }

    setState(() => status = "Submitting OTP...");

    try {
      final res = await _httpClient.post(
        Uri.parse("$CSP_BASE_URL/submit_otp"),
        headers: {"Content-Type": "application/json"},
        body: jsonEncode({
          "user_id": USER_ID,
          "device_id": DEVICE_ID,
          "otp": otp,
        }),
      );

      if (res.statusCode == 200) {
        setState(() => status = "OTP verified. Waiting for device response...");
      } else {
        setState(() => status = "OTP verification failed");
      }
    } catch (e) {
      setState(() => status = "Network error while sending OTP");
    }
  }

  // ================= HMAC =================
  String computeHmac(String pin, String nonce) {
    final key = utf8.encode(sharedSecret);
    final bytes = utf8.encode(pin + nonce);
    return Hmac(sha256, key).convert(bytes).toString();
  }

  // ================= SEND PIN =================
  Future<void> sendPIN() async {
    if (currentNonce == null) {
      setState(() => status = "Nonce not available");
      return;
    }

    final hmacValue = computeHmac(pinController.text, currentNonce!);
    await rxChar!.write(utf8.encode(hmacValue));

    setState(() => status = "Credentials sent. Waiting response...");
    pinController.clear();
    currentNonce = null;
  }

  // ================= UI =================
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text("Smart Lock")),
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
            Text(status),
            const SizedBox(height: 20),
            TextField(
              controller: pinController,
              maxLength: 4,
              obscureText: _isPinObscured,
              keyboardType: TextInputType.number,
              decoration: InputDecoration(
                labelText: "PIN",
                suffixIcon: IconButton(
                  icon: Icon(
                    _isPinObscured ? Icons.visibility : Icons.visibility_off,
                  ),
                  onPressed: () {
                    setState(() => _isPinObscured = !_isPinObscured);
                  },
                ),
              ),
            ),
            const SizedBox(height: 20),
            ElevatedButton(
              onPressed:
                  (!_waitingForOTP && _isPinValid && currentNonce != null)
                      ? sendPIN
                      : null,
              child: const Text("UNLOCK"),
            ),
            const SizedBox(height: 20),
            ElevatedButton.icon(
              onPressed: _isConnecting ? null : scanAndConnect,
              icon: const Icon(Icons.bluetooth),
              label: const Text("CONNECT"),
            ),
          ],
        ),
      ),
    );
  }

  @override
  void dispose() {
    _httpClient.close();
    _scanSub?.cancel();
    pinController.dispose();
    _pulseController.dispose();
    super.dispose();
  }
}
