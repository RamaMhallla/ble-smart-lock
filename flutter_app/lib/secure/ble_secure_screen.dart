import 'dart:async';
import 'dart:convert';
import 'package:crypto/crypto.dart';
import 'package:encrypt/encrypt.dart' as encrypt;
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

// Update this import to point to your Base Class
import 'package:flutter_iot_security/insecure/ble_insecure_screen.dart'; 

class BLEDoorSecureScreen extends StatefulWidget {
  const BLEDoorSecureScreen({super.key});

  @override
  State<BLEDoorSecureScreen> createState() => _BLEDoorSecureScreenState();
}

class _BLEDoorSecureScreenState extends BLEDoorBaseState<BLEDoorSecureScreen> {
  
  // ================= SECURITY CONFIG =================
  static const String sharedSecret = "SUPER_SECRET_KEY";
  static const String aesKeyStr = "1234567890123456"; 
  static const String aesIvStr  = "abcdefghijklmnop"; 

  String? currentNonce;

  // ================= HELPER: AES ENCRYPTION =================
  String encryptAES(String plainText) {
    final key = encrypt.Key.fromUtf8(aesKeyStr);
    final iv = encrypt.IV.fromUtf8(aesIvStr);
    final encrypter = encrypt.Encrypter(encrypt.AES(key, mode: encrypt.AESMode.cbc));
    final encrypted = encrypter.encrypt(plainText, iv: iv);
    return encrypted.base64; 
  }

  // ================= HELPER: AES DECRYPTION =================
  String decryptAES(String encryptedBase64) {
    try {
      final key = encrypt.Key.fromUtf8(aesKeyStr);
      final iv = encrypt.IV.fromUtf8(aesIvStr);
      final encrypter = encrypt.Encrypter(encrypt.AES(key, mode: encrypt.AESMode.cbc));
      final encrypted = encrypt.Encrypted.fromBase64(encryptedBase64);
      return encrypter.decrypt(encrypted, iv: iv);
    } catch (e) {
      print("Decryption Failed: $e");
      return "";
    }
  }

  // ================= HELPER: HMAC CALCULATION =================
  String computeHmac(String pin, String nonce) {
    final key = utf8.encode(sharedSecret);
    final bytes = utf8.encode(pin + nonce);
    final hmac = Hmac(sha256, key);
    return hmac.convert(bytes).toString();
  }

  // ================= OVERRIDE: SCANNING (BROAD SCAN - FIXES VISIBILITY) =================
  @override
  Future<void> scanAndConnect() async {
    setState(() {
      status = "Scanning (Secure)...";
      isConnecting = true;
      doorOpened = false;
      currentNonce = null;
      connectingNow = false;
    });

    pulseController.repeat(reverse: true);
    
    // Ensure permissions
    await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.location,
    ].request();

    // Stop any previous scan
    await FlutterBluePlus.stopScan(); 
    await scanSub?.cancel();

    // Listen to results
    scanSub = FlutterBluePlus.onScanResults.listen((results) async {
      if (connectingNow) return;

      for (final r in results) {
        // MANUAL FILTER (Exactly like Insecure Mode)
        // This ensures the device is found even if OS filtering fails
        final hasUUID = r.advertisementData.serviceUuids
            .any((u) => u.str128.toLowerCase() == BLEDoorBaseState.serviceUuid);

        if (hasUUID) {
          connectingNow = true;
          await FlutterBluePlus.stopScan();
          
          setState(() => status = "Found ${r.device.platformName}. Connecting...");
          await connectToDevice(r.device);
          return;
        }
      }
    });

    // START BROAD SCAN (No Filters = Better Visibility)
    await FlutterBluePlus.startScan(
      timeout: const Duration(seconds: 15),
      androidScanMode: AndroidScanMode.lowLatency, 
    );
  }

  // ================= OVERRIDE: CONNECTION LOGIC =================
  @override
  Future<void> connectToDevice(BluetoothDevice d) async {
    try {
      device = d;
      
      // FIX: Removed invalid license parameter
      await device!.connect(
        autoConnect: false, 
        timeout: const Duration(seconds: 15),
        license: License.free
      );

      setState(() => status = "Discovering services...");
      await _secureDiscoverServices();
      
    } catch (e) {
      print("Connection Error: $e");
      setState(() {
        status = "Connection Failed";
        isConnecting = false;
      });
      pulseController.stop();
    }
  }

  Future<void> _secureDiscoverServices() async {
    final services = await device!.discoverServices();

    for (final s in services) {
      if (s.uuid.str128.toLowerCase() == BLEDoorBaseState.serviceUuid) {
        for (final c in s.characteristics) {
           final uuid = c.uuid.str128.toLowerCase();
           if (uuid.contains("6e400002")) rxChar = c;
           if (uuid.contains("6e400003")) txChar = c;
        }
      }
    }

    if (rxChar == null || txChar == null) {
      setState(() => status = "Secure Service Not Found");
      return;
    }

    await txChar!.setNotifyValue(true);

    txChar!.lastValueStream.listen((value) {
      final msg = utf8.decode(value).trim();
      print("Raw Received from ESP: $msg");

      setState(() {
        if (msg == "OK") {
          status = "UNLOCKED!";
          doorOpened = true;
          isConnecting = false;
          pulseController.stop();
        } else if (msg == "WRONG") {
          status = "Access Denied";
          doorOpened = false;
          isConnecting = false;
        } else {
          // Decrypt Nonce
          print("Decrypting incoming nonce...");
          //final decryptedNonce = decryptAES(msg);
          
        //  if (decryptedNonce.isNotEmpty) {
         //   currentNonce = decryptedNonce;
          if (msg.isNotEmpty) {
            currentNonce = msg;
            status = "Secure Channel Active.\nEnter PIN.";
            print("Decrypted Nonce: $currentNonce");
            isConnecting = false;

            pulseController.stop();
          } else {
            status = "Security Error: Decryption failed";
          }
        }
      });
    });

    print("Sending HELLO to start handshake...");
    await rxChar!.write(utf8.encode("HELLO"));
    setState(() => status = "Handshaking...");
  }

  @override
  Future<void> sendPIN() async {
    if (currentNonce == null) {
      setState(() => status = "Session Expired. Reconnecting...");
      await rxChar!.write(utf8.encode("HELLO"));
      return;
    }

    final pin = pinController.text.trim();
    if (pin.length != 4) return;

    // 1. Compute HMAC
    final hmacValue = computeHmac(pin, currentNonce!);
    
    // 2. Encrypt HMAC
    final encryptedPayload = encryptAES(hmacValue);

    // 3. Send
    await rxChar!.write(utf8.encode(encryptedPayload));

    setState(() {
      status = "Verifying...";
      currentNonce = null; 
    });
    
    pinController.clear();
  }
}