import 'dart:async';
import 'package:flutter/material.dart';
import 'package:firebase_auth/firebase_auth.dart';

class EmailVerificationScreen extends StatefulWidget {
  const EmailVerificationScreen({super.key});

  @override
  State<EmailVerificationScreen> createState() =>
      _EmailVerificationScreenState();
}

class _EmailVerificationScreenState extends State<EmailVerificationScreen> {
  Timer? _timer;
  String _status =
      "We sent a verification email.\nPlease verify your email to continue.";

  @override
  void initState() {
    super.initState();

    // ⏱️ Auto-check every 3 seconds
    _timer = Timer.periodic(const Duration(seconds: 3), (_) {
      _checkVerification();
    });
  }

  Future<void> _checkVerification() async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) return;

    await user.reload(); // 🔴 المفتاح
    final refreshedUser = FirebaseAuth.instance.currentUser;

    if (refreshedUser != null && refreshedUser.emailVerified) {
      _timer?.cancel();
      // AuthGate رح يلاحظ التغيير وينقلك تلقائيًا
    }
  }

  Future<void> _resendVerification() async {
    final user = FirebaseAuth.instance.currentUser;
    if (user != null) {
      await user.sendEmailVerification();
      setState(() {
        _status = "Verification email resent.\nCheck your inbox.";
      });
    }
  }

  Future<void> _logout() async {
    _timer?.cancel();
    await FirebaseAuth.instance.signOut();
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text("Verify Email"),
        actions: [
          IconButton(
            icon: const Icon(Icons.logout),
            onPressed: _logout,
          )
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const Icon(Icons.mark_email_unread_outlined, size: 90),
            const SizedBox(height: 20),
            Text(
              _status,
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 30),

            const CircularProgressIndicator(),
            const SizedBox(height: 20),

            TextButton.icon(
              icon: const Icon(Icons.send),
              label: const Text("Resend verification email"),
              onPressed: _resendVerification,
            ),
          ],
        ),
      ),
    );
  }
}
