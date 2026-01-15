const functions = require("firebase-functions");
const admin = require("firebase-admin");
const nodemailer = require("nodemailer");

admin.initializeApp();
const db = admin.firestore();

// ⚠️ غيّري الإيميل تبعك
const transporter = nodemailer.createTransport({
  service: "gmail",
  auth: {
    user: "YOUR_EMAIL@gmail.com",
    pass: "YOUR_GMAIL_APP_PASSWORD",
  },
});

// ================================
// Generate OTP
// ================================
exports.generateOTP = functions.https.onCall(async (data, context) => {
  if (!context.auth) {
    throw new functions.https.HttpsError(
      "unauthenticated",
      "User not authenticated"
    );
  }

  const email = context.auth.token.email;
  const otp = Math.floor(100000 + Math.random() * 900000).toString();

  await db.collection("otp_codes").doc(email).set({
    otp: otp,
    createdAt: admin.firestore.FieldValue.serverTimestamp(),
  });

  await transporter.sendMail({
    to: email,
    subject: "Smart Lock OTP",
    text: `Your OTP code is: ${otp}`,
  });

  return { success: true };
});

// ================================
// Verify OTP
// ================================
exports.verifyOTP = functions.https.onCall(async (data, context) => {
  if (!context.auth) {
    throw new functions.https.HttpsError("unauthenticated");
  }

  const email = context.auth.token.email;
  const userOtp = data.otp;

  const doc = await db.collection("otp_codes").doc(email).get();
  if (!doc.exists) return { authorized: false };

  const storedOtp = doc.data().otp;

  if (storedOtp === userOtp) {
    await db.collection("otp_codes").doc(email).delete();
    return { authorized: true };
  }

  return { authorized: false };
});
