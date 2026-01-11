import 'package:flutter/material.dart';
import 'package:flutter_iot_security/auth/login_screen.dart';

// Secure / Insecure screens
import '../secure/ble_secure_screen.dart';
import '../insecure/ble_insecure_screen.dart';
import 'package:firebase_auth/firebase_auth.dart';


class ModeSelectorScreen extends StatelessWidget {
  const ModeSelectorScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text("Smart Lock – Test Modes"),
        centerTitle: true,
         actions: [
                IconButton(
                  icon: const Icon(Icons.logout),
                  onPressed: () async {
                    await FirebaseAuth.instance.signOut();

                    Navigator.pushAndRemoveUntil(
                      context,
                      MaterialPageRoute(builder: (_) => const LoginScreen()),
                      (route) => false,
                    );
                  },
                ),
              ],
      ),
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            ElevatedButton.icon(
              icon: const Icon(Icons.lock),
              label: const Text("Secure Mode"),
              style: ElevatedButton.styleFrom(
                minimumSize: const Size(220, 50),
              ),
              onPressed: () {
                Navigator.push(
                  context,
                  MaterialPageRoute(
                    builder: (_) => const BLEDoorSecureScreen(),
                  ),
                );
              },
            ),
            const SizedBox(height: 20),
            ElevatedButton.icon(
              icon: const Icon(Icons.warning),
              label: const Text("Insecure Mode"),
              style: ElevatedButton.styleFrom(
                backgroundColor: Colors.orange,
                minimumSize: const Size(220, 50),
              ),
              onPressed: () {
                Navigator.push(
                  context,
                  MaterialPageRoute(
                    builder: (_) => const BLEDoorInsecureScreen(),
                  ),
                );
              },
            ),
          ],
        ),
      ),
    );
  }
}
