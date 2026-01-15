import 'dart:io';

import 'package:flutter/material.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:flutter_iot_security/http_override.dart';
import 'package:flutter_iot_security/screens/mode_selector_screen.dart';
import 'package:flutter_iot_security/secure/ble_secure_screen.dart';
import 'firebase_options.dart';
import 'services/fcm_service.dart';
import 'package:firebase_core/firebase_core.dart';


void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  HttpOverrides.global = MyHttpOverrides();
  await Firebase.initializeApp(
    options: DefaultFirebaseOptions.currentPlatform,
  );

  await FCMService.init(); 

  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return const MaterialApp(
      debugShowCheckedModeBanner: false,
      home: ModeSelectorScreen(), 
    );
  }
}
