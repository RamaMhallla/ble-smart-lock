// fcm_service.dart
import 'dart:convert';

import 'package:firebase_messaging/firebase_messaging.dart';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';
import 'package:http/http.dart' as http;

class FCMService {
  // ================= CONFIG =================
  static const String CSP_BASE_URL = "https://10.146.61.134:5001";
  static const String USER_ID = "mobile_user_01";

  static final FlutterLocalNotificationsPlugin _localNotifications =
      FlutterLocalNotificationsPlugin();

  // ================= INIT =================
  static Future<void> init() async {
    FirebaseMessaging messaging = FirebaseMessaging.instance;

    // Request notification permissions
    NotificationSettings settings = await messaging.requestPermission(
      alert: true,
      badge: true,
      sound: true,
      provisional: false,
    );

    if (settings.authorizationStatus != AuthorizationStatus.authorized) {
      print("[FCM] Notification permission not granted");
      return;
    }

    print("[FCM] Notification permission granted");

    // Get FCM token
    final token = await messaging.getToken();
    if (token == null) {
      print("[FCM] Failed to obtain FCM token");
      return;
    }

    print("[FCM] Token: $token");

    // Register token with CSP
    await _registerTokenWithCSP(token);

    // Initialize local notifications (Android)
    const androidInit = AndroidInitializationSettings('@mipmap/ic_launcher');
    const initSettings = InitializationSettings(android: androidInit);

    await _localNotifications.initialize(initSettings);

    // Foreground message handler
    FirebaseMessaging.onMessage.listen((RemoteMessage message) {
      print("[FCM] Foreground message received");

      final notification = message.notification;
      if (notification != null) {
        _showNotification(
          title: notification.title ?? 'Security Alert',
          body: notification.body ?? '',
        );
      }
    });

    // App opened from notification
    FirebaseMessaging.onMessageOpenedApp.listen((RemoteMessage message) {
      print("[FCM] Notification opened by user");
    });
  }

  // ================= REGISTER TOKEN =================
  static Future<void> _registerTokenWithCSP(String token) async {
    try {
      final res = await http.post(
        Uri.parse("$CSP_BASE_URL/register_token"),
        headers: {"Content-Type": "application/json"},
        body: jsonEncode({
          "user_id": USER_ID,
          "fcm_token": token,
        }),
      );

      if (res.statusCode == 200) {
        print("[FCM] Token successfully registered with CSP");
      } else {
        print("[FCM] Token registration failed (status ${res.statusCode})");
      }
    } catch (e) {
      print("[FCM] Error while registering token with CSP: $e");
    }
  }

  // ================= LOCAL NOTIFICATION =================
  static Future<void> _showNotification({
    required String title,
    required String body,
  }) async {
    const androidDetails = AndroidNotificationDetails(
      'otp_channel',
      'OTP Notifications',
      channelDescription: 'OTP and security notifications',
      importance: Importance.max,
      priority: Priority.high,
      playSound: true,
    );

    const details = NotificationDetails(android: androidDetails);

    await _localNotifications.show(
      DateTime.now().millisecondsSinceEpoch ~/ 1000,
      title,
      body,
      details,
    );
  }
}
