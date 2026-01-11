import 'package:flutter/material.dart';

class OtpDialog extends StatefulWidget {
  final Future<bool> Function(String otp) onSubmit;

  const OtpDialog({super.key, required this.onSubmit});

  @override
  State<OtpDialog> createState() => _OtpDialogState();
}

class _OtpDialogState extends State<OtpDialog> {
  final TextEditingController _otpController = TextEditingController();
  bool _loading = false;
  String? _error;

  Future<void> _handleSubmit() async {
    final otp = _otpController.text.trim();

    if (otp.length != 6) {
      setState(() => _error = "OTP must be 6 digits");
      return;
    }

    setState(() {
      _loading = true;
      _error = null;
    });

    final ok = await widget.onSubmit(otp);

    if (!mounted) return;

    if (ok) {
      Navigator.of(context).pop(true);
    } else {
      setState(() {
        _loading = false;
        _error = "Invalid or expired OTP";
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text("🔐 OTP Verification"),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Text(
            "Enter the OTP sent to your phone",
            style: TextStyle(fontSize: 14),
          ),
          const SizedBox(height: 12),
          TextField(
            controller: _otpController,
            keyboardType: TextInputType.number,
            maxLength: 6,
            decoration: const InputDecoration(
              labelText: "OTP Code",
              border: OutlineInputBorder(),
            ),
          ),
          if (_error != null)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(
                _error!,
                style: const TextStyle(color: Colors.red),
              ),
            ),
        ],
      ),
      actions: [
        TextButton(
          onPressed: _loading ? null : () => Navigator.pop(context, false),
          child: const Text("Cancel"),
        ),
        ElevatedButton(
          onPressed: _loading ? null : _handleSubmit,
          child: _loading
              ? const SizedBox(
                  width: 18,
                  height: 18,
                  child: CircularProgressIndicator(strokeWidth: 2),
                )
              : const Text("Verify"),
        ),
      ],
    );
  }
}
