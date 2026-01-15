import 'package:flutter/material.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'package:flutter_iot_security/screens/mode_selector_screen.dart';

import 'login_screen.dart';
import 'email_verification_screen.dart';
import '../screens/mode_selector_screen.dart';

class AuthGate extends StatelessWidget {
  const AuthGate({super.key});

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<User?>(
      stream: FirebaseAuth.instance.authStateChanges(),
      builder: (context, snapshot) {
        // Loading state
        if (snapshot.connectionState == ConnectionState.waiting) {
          return const Scaffold(
            body: Center(child: CircularProgressIndicator()),
          );
        }

        // User not logged in
        if (!snapshot.hasData) {
          return const LoginScreen();
        }

        final user = snapshot.data!;

        // User logged in but email not verified
        if (!user.emailVerified) {
          return const EmailVerificationScreen();
        }

        // User logged in and verified
        return const ModeSelectorScreen();
      },
    );
  }
}
