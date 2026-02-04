import 'dart:async';
import 'dart:convert';
import 'package:crypto/crypto.dart';
import 'package:encrypt/encrypt.dart' as encrypt;
import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';


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
  String computeHmac(String pin) {
    final key = utf8.encode(sharedSecret);
    final bytes = utf8.encode(pin);
    final hmac = Hmac(sha256, key);
    return hmac.convert(bytes).toString();
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
    print("Discovered ${services.length} services.");
    for (final s in services) {
      if (s.uuid.str128.toLowerCase() == BLEDoorBaseState.serviceUuid) {
        for (final c in s.characteristics) {
           final uuid = c.uuid.str128.toLowerCase();
           if (uuid ==
              "6e400002-b5a3-f393-e0a9-e50e24dcca9e") rxChar = c;
          if (uuid ==
              "6e400003-b5a3-f393-e0a9-e50e24dcca9e") txChar = c;
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
        final separatorIndex = msg.indexOf(':');
        if (separatorIndex == -1) {
          status = "Security Error: Invalid message format";
          return;
        }
        String clientSignature = msg.substring(0, separatorIndex);
        String ciphertext = msg.substring(separatorIndex + 1);

        if (clientSignature != computeHmac(ciphertext)) {
          status = "Security Error: Invalid HMAC";
          return;
        }
        final decryptedtmsg= decryptAES(ciphertext);
        if (decryptedtmsg == "OK") {
          status = "UNLOCKED!";
          doorOpened = true;
          isConnecting = false;
          pulseController.stop();
        } else if (decryptedtmsg == "WRONG") {
          status = "Access Denied";
          doorOpened = false;
          isConnecting = false;
        } else if (decryptedtmsg == "PENDING") {
          status = "Waiting for server replay...";
        } else if (decryptedtmsg.isNotEmpty && decryptedtmsg.contains("NONCE-")) {
            currentNonce = decryptedtmsg.substring(6); // Remove "NONCE-" prefix
            status = "Secure Channel Active.\nEnter PIN.";
            print("Decrypted Nonce: $currentNonce");
            isConnecting = false;
            pulseController.stop();
        } else {
            status = "Security Error: Decryption failed";
        }
      });
    });

    print("Sending HELLO to start handshake...");
    await sendSecurePacket("HELLO");
    setState(() => status = "Handshaking...");
  }

  Future<void> sendSecurePacket(String s) async {   
    // 1. Encrypt HMAC
    final encryptedPayload = encryptAES(s);
    // 2. Compute HMAC
    final hmacValue = computeHmac(encryptedPayload);
    // 3. Send
    await rxChar!.write(utf8.encode(hmacValue+":"+ encryptedPayload));
  }
  

  @override
  Future<void> sendPIN() async {
    if (currentNonce == null) {
      setState(() => status = "Session Expired. Reconnecting...");
      await sendSecurePacket("HELLO");
      return;
    }

    final pin = pinController.text.trim();
    if (pin.length != 4) return;
    String mes=computeHmac(pin);

    final Map<String, dynamic> payloadMap = {
      'otp': mes,
      'user_id': FirebaseAuth.instance.currentUser!.email,
      'nonce': currentNonce,
    };
    final String jsonPayload = jsonEncode(payloadMap);
    await sendSecurePacket(jsonPayload);

    setState(() {
      status = "Verifying...";
      currentNonce = null; 
    });
    
    pinController.clear();
  }
}